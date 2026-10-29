#include "db/protocol/tcp_server.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/protocol/frame_codec.hpp"
#include "db/protocol/request_dispatcher.hpp"
#include "db/protocol/response_encoder.hpp"

#include "timed_tcp_stream.hpp"

namespace db::protocol {
namespace {

inline constexpr std::size_t kConnectionReadBufferSize = std::size_t{32U} * std::size_t{1024U};

auto runtime_error(
    const base::ErrorCode code,
    std::string message,
    const std::string_view operation
) -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "protocol.tcp_server",
        std::string{operation}
    );
}

auto exception_error(const std::exception& exception) -> base::Error {
    return runtime_error(
        base::ErrorCode::kInternal,
        std::string{"unexpected exception escaped a protocol port: "}.append(exception.what()),
        "run connection"
    );
}

auto unknown_exception_error() -> base::Error {
    return runtime_error(
        base::ErrorCode::kInternal,
        "unknown exception escaped a protocol port",
        "run connection"
    );
}

struct WorkerState final {
    std::size_t active{0U};
    std::optional<base::Error> first_runtime_failure;
};

struct WorkerOutcome final {
    base::Status status{};
    bool is_peer_failure{false};
    bool is_shutdown{false};
};

struct WorkerCompletion final {
    std::atomic_bool completed{false};
};

struct WorkerHandle final {
    std::thread thread;
    std::shared_ptr<WorkerCompletion> completion;
};

// Guarantees one best-effort session cleanup even when a third-party port
// unexpectedly throws. A failed explicit close is still considered attempted:
// retrying a rollback through an unknown port state would be less safe than
// reporting the structured error to the server owner.
class SessionCleanup final {
public:
    explicit SessionCleanup(std::unique_ptr<SessionPort> session) noexcept
        : session_{std::move(session)} {}

    SessionCleanup(const SessionCleanup&) = delete;
    auto operator=(const SessionCleanup&) -> SessionCleanup& = delete;
    SessionCleanup(SessionCleanup&&) = delete;
    auto operator=(SessionCleanup&&) -> SessionCleanup& = delete;

    ~SessionCleanup() noexcept {
        if (!close_attempted_) {
            try {
                static_cast<void>(close());
            } catch (...) {  // NOLINT(bugprone-empty-catch) -- protocol containment boundary.
                // SessionPort exceptions may not escape a protocol boundary.
            }
        }
    }

    auto close() -> base::Status {
        close_attempted_ = true;
        return session_->close();
    }

    auto session() noexcept -> SessionPort& {
        return *session_;
    }

private:
    std::unique_ptr<SessionPort> session_;
    bool close_attempted_{false};
};

class TcpResponseSink final : public ResponseSink {
public:
    explicit TcpResponseSink(internal::TimedTcpStream& stream) noexcept
        : stream_{&stream} {}

    auto write_encoded_frame(const base::Bytes frame) -> base::Status override {
        return stream_->write_all(frame, "write RMDB response frame");
    }

private:
    internal::TimedTcpStream* stream_{nullptr};
};

auto is_peer_transport_failure(const base::Error& error) noexcept -> bool {
    return error.code() == base::ErrorCode::kIo || error.code() == base::ErrorCode::kCorruption
           || error.code() == base::ErrorCode::kUnavailable;
}

auto is_shutdown_error(const base::Error& error) noexcept -> bool {
    return error.code() == base::ErrorCode::kCancelled;
}

struct RequestLoopOutcome final {
    base::Status status{};
    bool is_peer_failure{false};
    bool is_shutdown{false};
};

auto request_loop_failure(base::Error error) -> RequestLoopOutcome {
    const bool shutdown = is_shutdown_error(error);
    const bool peer_failure = !shutdown && is_peer_transport_failure(error);
    return RequestLoopOutcome{
        .status = base::fail(std::move(error)),
        .is_peer_failure = peer_failure,
        .is_shutdown = shutdown,
    };
}

auto handle_complete_request(
    const FrameView& frame,
    RequestDispatcher& request_dispatcher,
    ResponseEncoder& response_encoder,
    TcpResponseSink& response_sink
) -> std::optional<RequestLoopOutcome> {
    auto response = request_dispatcher.dispatch(frame);
    if (!response) {
        return RequestLoopOutcome{
            .status = base::fail(std::move(response).error()),
            .is_peer_failure = false,
            .is_shutdown = false,
        };
    }
    auto emitted = response_encoder.emit(response.value(), response_sink);
    if (emitted) {
        return std::nullopt;
    }

    base::Error encoding_error = std::move(emitted).error();
    if (is_shutdown_error(encoding_error) || is_peer_transport_failure(encoding_error)) {
        return request_loop_failure(std::move(encoding_error));
    }
    auto fallback = response_encoder.emit_terminal_error(
        TerminalResponseKind::kError,
        encoding_error,
        response_sink
    );
    if (!fallback) {
        return request_loop_failure(std::move(fallback).error());
    }
    return std::nullopt;
}

auto consume_request_buffer(
    const base::Bytes input,
    FrameDecoder& frame_decoder,
    std::optional<std::chrono::steady_clock::time_point>& partial_frame_deadline,
    RequestDispatcher& request_dispatcher,
    ResponseEncoder& response_encoder,
    TcpResponseSink& response_sink,
    const TcpServerOptions& options
) -> std::optional<RequestLoopOutcome> {
    base::Bytes remaining = input;
    while (!remaining.empty()) {
        const bool frame_was_incomplete = frame_decoder.has_incomplete_frame();
        auto decoded = frame_decoder.consume(remaining);
        if (!decoded) {
            return request_loop_failure(std::move(decoded).error());
        }
        if (decoded.value().consumed == 0U) {
            return RequestLoopOutcome{
                .status = base::fail(runtime_error(
                    base::ErrorCode::kInvariantViolation,
                    "RMDB frame decoder made no progress",
                    "read request frame"
                )),
                .is_peer_failure = false,
                .is_shutdown = false,
            };
        }
        remaining = remaining.subspan(decoded.value().consumed);
        if (!frame_was_incomplete && frame_decoder.has_incomplete_frame()) {
            partial_frame_deadline = std::chrono::steady_clock::now() + options.request_timeout;
        }
        if (!decoded.value().frame.has_value()) {
            continue;
        }
        partial_frame_deadline.reset();

        auto handled = handle_complete_request(
            *decoded.value().frame,
            request_dispatcher,
            response_encoder,
            response_sink
        );
        if (handled.has_value()) {
            return handled;
        }
    }
    return std::nullopt;
}

// Drives one already handshaken connection. This owns only wire buffers and
// protocol adapters; the caller retains session cleanup and worker ownership.
auto serve_request_loop(
    internal::TimedTcpStream& stream,
    RequestDispatcher& request_dispatcher,
    const TcpServerOptions& options
) -> RequestLoopOutcome {
    ResponseEncoder response_encoder;
    TcpResponseSink response_sink{stream};
    FrameDecoder frame_decoder;
    std::array<std::byte, kConnectionReadBufferSize> input{};
    std::optional<std::chrono::steady_clock::time_point> partial_frame_deadline;

    while (true) {
        const auto deadline = partial_frame_deadline.value_or(
            std::chrono::steady_clock::now() + options.idle_timeout
        );
        auto received = stream.read_some_until(input, deadline, "read RMDB request frame");
        if (!received) {
            return request_loop_failure(std::move(received).error());
        }
        if (received.value() == 0U) {
            if (frame_decoder.has_incomplete_frame()) {
                return RequestLoopOutcome{
                    .status = base::fail(runtime_error(
                        base::ErrorCode::kCorruption,
                        "RMDB peer closed during an incomplete frame",
                        "read request frame"
                    )),
                    .is_peer_failure = true,
                    .is_shutdown = false,
                };
            }
            return {};
        }

        auto failure = consume_request_buffer(
            base::Bytes{input.data(), received.value()},
            frame_decoder,
            partial_frame_deadline,
            request_dispatcher,
            response_encoder,
            response_sink,
            options
        );
        if (failure.has_value()) {
            return std::move(*failure);
        }
    }
}

auto reserve_worker_handles(std::vector<WorkerHandle>& workers, const std::size_t connection_count)
    -> base::Status {
    try {
        workers.reserve(connection_count);
    } catch (const std::bad_alloc&) {
        return base::fail(runtime_error(
            base::ErrorCode::kResourceExhausted,
            "could not reserve TCP worker handles",
            "serve connections"
        ));
    } catch (const std::length_error&) {
        return base::fail(runtime_error(
            base::ErrorCode::kResourceExhausted,
            "requested TCP worker count is unsupported",
            "serve connections"
        ));
    }
    return {};
}

auto reserve_worker_slot(
    WorkerState& state,
    std::mutex& worker_mutex,
    std::condition_variable& worker_changed,
    const std::size_t maximum_connections,
    const std::atomic_bool& shutdown_requested
) -> bool {
    std::unique_lock lock{worker_mutex};
    worker_changed.wait(lock, [&state, maximum_connections, &shutdown_requested] {
        return state.active < maximum_connections
               || shutdown_requested.load(std::memory_order_acquire);
    });
    if (shutdown_requested.load(std::memory_order_acquire)) {
        return false;
    }
    ++state.active;
    return true;
}

void release_worker_slot(
    WorkerState& state,
    std::mutex& worker_mutex,
    std::condition_variable& worker_changed
) {
    {
        std::lock_guard lock{worker_mutex};
        --state.active;
    }
    worker_changed.notify_all();
}

void record_worker_outcome(
    WorkerState& state,
    std::mutex& worker_mutex,
    std::condition_variable& worker_changed,
    WorkerCompletion& completion,
    WorkerOutcome outcome
) {
    {
        std::lock_guard lock{worker_mutex};
        if (!outcome.status && !outcome.is_peer_failure && !outcome.is_shutdown
            && !state.first_runtime_failure.has_value()) {
            state.first_runtime_failure.emplace(std::move(outcome.status).error());
        }
        // Publish completed protocol work before making the capacity slot
        // reusable. The accept loop reaps a marked handle before spawning its
        // replacement, preserving serve_forever()'s fixed worker capacity.
        completion.completed.store(true, std::memory_order_release);
        --state.active;
    }
    worker_changed.notify_all();
}

template <typename Runner>
auto start_worker(
    WorkerState& state,
    std::mutex& worker_mutex,
    std::condition_variable& worker_changed,
    std::vector<WorkerHandle>& workers,
    io::TcpStream stream,
    Runner&& runner
) -> base::Status {
    try {
        auto completion = std::make_shared<WorkerCompletion>();
        workers.emplace_back(
            WorkerHandle{
                .thread =
                    std::thread{
                        [stream = std::move(stream),
                         &state,
                         &worker_mutex,
                         &worker_changed,
                         completion,
                         runner = std::forward<Runner>(runner)]() mutable {
                            WorkerOutcome outcome{};
                            try {
                                outcome = runner(std::move(stream));
                            } catch (const std::exception& exception) {
                                outcome.status = base::fail(exception_error(exception));
                            } catch (...) {
                                outcome.status = base::fail(unknown_exception_error());
                            }
                            record_worker_outcome(
                                state,
                                worker_mutex,
                                worker_changed,
                                *completion,
                                std::move(outcome)
                            );
                        },
                    },
                .completion = completion,
            }
        );
    } catch (const std::system_error& error) {
        release_worker_slot(state, worker_mutex, worker_changed);
        return base::fail(
            base::Error{
                base::ErrorCode::kResourceExhausted,
                "could not start TCP connection worker",
                error.code(),
            }
                .with_context("protocol.tcp_server", "serve connections")
        );
    } catch (const std::bad_alloc&) {
        release_worker_slot(state, worker_mutex, worker_changed);
        return base::fail(runtime_error(
            base::ErrorCode::kResourceExhausted,
            "could not allocate TCP connection worker",
            "serve connections"
        ));
    }
    return {};
}

void reap_finished_workers(std::vector<WorkerHandle>& workers) {
    auto current = workers.begin();
    while (current != workers.end()) {
        if (!current->completion->completed.load(std::memory_order_acquire)) {
            ++current;
            continue;
        }
        current->thread.join();
        current = workers.erase(current);
    }
}

void join_workers(std::vector<WorkerHandle>& workers) {
    for (WorkerHandle& worker : workers) {
        worker.thread.join();
    }
    workers.clear();
}

auto poll_next_connection(
    io::TcpListener& listener,
    const std::atomic_bool& shutdown_requested,
    const std::chrono::milliseconds shutdown_poll_interval
) -> base::Result<std::optional<io::TcpStream>> {
    if (shutdown_requested.load(std::memory_order_acquire)) {
        return base::fail(runtime_error(
            base::ErrorCode::kCancelled,
            "RMDB server shutdown was requested",
            "accept connection"
        ));
    }
    auto ready = listener.wait_readable(shutdown_poll_interval);
    if (!ready) {
        return base::fail(
            std::move(ready).error().with_context("protocol.tcp_server", "accept connection")
        );
    }
    if (ready.value() == io::TcpWaitResult::kTimedOut) {
        return std::optional<io::TcpStream>{};
    }
    if (shutdown_requested.load(std::memory_order_acquire)) {
        return base::fail(runtime_error(
            base::ErrorCode::kCancelled,
            "RMDB server shutdown was requested",
            "accept connection"
        ));
    }
    auto accepted = listener.try_accept();
    if (!accepted) {
        if (accepted.error().code() == base::ErrorCode::kUnavailable) {
            return std::optional<io::TcpStream>{};
        }
        return base::fail(
            std::move(accepted).error().with_context("protocol.tcp_server", "accept connection")
        );
    }
    return std::optional<io::TcpStream>{std::move(accepted).value()};
}

auto accept_next_connection(
    io::TcpListener& listener,
    const std::atomic_bool& shutdown_requested,
    const std::chrono::milliseconds shutdown_poll_interval
) -> base::Result<io::TcpStream> {
    while (true) {
        auto accepted = poll_next_connection(listener, shutdown_requested, shutdown_poll_interval);
        if (!accepted) {
            return base::fail(std::move(accepted).error());
        }
        if (accepted.value().has_value()) {
            return std::move(*accepted).value();
        }
    }
}

auto validate_positive_timeout(const std::chrono::milliseconds timeout, const std::string_view name)
    -> base::Status {
    if (timeout.count() <= 0) {
        return base::fail(runtime_error(
            base::ErrorCode::kInvalidArgument,
            std::string{"TCP server "}.append(name).append(" must be positive"),
            "bind server"
        ));
    }
    return {};
}

auto validate_server_options(const TcpServerOptions& options) -> base::Status {
    if (options.maximum_concurrent_connections == 0U) {
        return base::fail(runtime_error(
            base::ErrorCode::kInvalidArgument,
            "TCP server maximum concurrent connections must be positive",
            "bind server"
        ));
    }
    for (const auto& [timeout, name] : {
             std::pair{options.handshake_timeout, std::string_view{"handshake timeout"}},
             std::pair{options.idle_timeout, std::string_view{"idle timeout"}},
             std::pair{options.request_timeout, std::string_view{"request timeout"}},
             std::pair{options.response_write_timeout, std::string_view{"response write timeout"}},
             std::pair{options.shutdown_poll_interval, std::string_view{"shutdown poll interval"}},
         }) {
        auto valid = validate_positive_timeout(timeout, name);
        if (!valid) {
            return base::fail(std::move(valid).error());
        }
    }
    return {};
}

auto shutdown_requested_error(const std::string_view operation) -> base::Error {
    return runtime_error(
        base::ErrorCode::kCancelled,
        "RMDB server shutdown was requested",
        operation
    );
}

auto take_runtime_failure(WorkerState& state, std::mutex& worker_mutex)
    -> std::optional<base::Error> {
    std::lock_guard lock{worker_mutex};
    if (!state.first_runtime_failure.has_value()) {
        return std::nullopt;
    }
    return std::move(state.first_runtime_failure);
}

}  // namespace

struct TcpServer::Control final {
    std::atomic_bool shutdown_requested{false};
    std::mutex worker_mutex;
    std::condition_variable worker_changed;
};

struct TcpServer::ConnectionOutcome final {
    base::Status status{};
    bool is_peer_failure{false};
    bool is_shutdown{false};
};

TcpServer::TcpServer(
    io::TcpListener listener,
    SessionFactoryPort& session_factory,
    const TcpServerOptions options,
    std::unique_ptr<Control> control
)
    : listener_{std::move(listener)},
      session_factory_{&session_factory},
      options_{options},
      control_{std::move(control)} {}

TcpServer::TcpServer(TcpServer&&) noexcept = default;

auto TcpServer::operator=(TcpServer&&) noexcept -> TcpServer& = default;

TcpServer::~TcpServer() = default;

auto TcpServer::bind(
    const io::TcpEndpoint& endpoint,
    SessionFactoryPort& session_factory,
    const TcpServerOptions options
) -> base::Result<TcpServer> {
    auto valid_options = validate_server_options(options);
    if (!valid_options) {
        return base::fail(std::move(valid_options).error());
    }
    auto listener = io::TcpListener::bind(endpoint, options.listen_backlog);
    if (!listener) {
        return base::fail(std::move(listener).error());
    }
    try {
        return TcpServer{
            std::move(listener).value(),
            session_factory,
            options,
            std::make_unique<Control>(),
        };
    } catch (const std::bad_alloc&) {
        return base::fail(runtime_error(
            base::ErrorCode::kResourceExhausted,
            "could not allocate TCP server control state",
            "bind server"
        ));
    }
}

auto TcpServer::local_endpoint() const -> base::Result<io::TcpEndpoint> {
    return listener_.local_endpoint();
}

auto TcpServer::serve_one() -> base::Status {
    if (shutdown_requested()) {
        return base::fail(shutdown_requested_error("serve one connection"));
    }
    auto stream = accept_next_connection(
        listener_,
        control_->shutdown_requested,
        options_.shutdown_poll_interval
    );
    if (!stream) {
        return base::fail(std::move(stream).error());
    }
    auto outcome = serve_connection(std::move(stream).value());
    if (!outcome.status) {
        return base::fail(std::move(outcome.status).error());
    }
    return {};
}

auto TcpServer::serve_n(const std::size_t connection_count) -> base::Status {
    if (connection_count == 0U) {
        return {};
    }
    if (shutdown_requested()) {
        return base::fail(shutdown_requested_error("serve connections"));
    }

    std::vector<WorkerHandle> workers;
    auto reserved = reserve_worker_handles(workers, connection_count);
    if (!reserved) {
        return base::fail(std::move(reserved).error());
    }

    WorkerState state;
    std::optional<base::Error> accept_failure;
    for (std::size_t accepted_count = 0U; accepted_count < connection_count; ++accepted_count) {
        if (!reserve_worker_slot(
                state,
                control_->worker_mutex,
                control_->worker_changed,
                options_.maximum_concurrent_connections,
                control_->shutdown_requested
            )) {
            accept_failure.emplace(shutdown_requested_error("serve connections"));
            break;
        }

        auto stream = accept_next_connection(
            listener_,
            control_->shutdown_requested,
            options_.shutdown_poll_interval
        );
        if (!stream) {
            release_worker_slot(state, control_->worker_mutex, control_->worker_changed);
            accept_failure.emplace(std::move(stream).error());
            break;
        }

        auto started = start_worker(
            state,
            control_->worker_mutex,
            control_->worker_changed,
            workers,
            std::move(stream).value(),
            [this](io::TcpStream connection) {
                auto outcome = serve_connection(std::move(connection));
                return WorkerOutcome{
                    .status = std::move(outcome.status),
                    .is_peer_failure = outcome.is_peer_failure,
                    .is_shutdown = outcome.is_shutdown,
                };
            }
        );
        if (!started) {
            accept_failure.emplace(std::move(started).error());
            break;
        }
    }

    join_workers(workers);
    if (accept_failure.has_value()) {
        return base::fail(std::move(*accept_failure));
    }
    if (auto runtime_failure = take_runtime_failure(state, control_->worker_mutex);
        runtime_failure.has_value()) {
        return base::fail(std::move(*runtime_failure));
    }
    return {};
}

auto TcpServer::serve_forever() -> base::Status {
    if (control_ == nullptr) {
        return base::fail(shutdown_requested_error("serve forever"));
    }
    std::vector<WorkerHandle> workers;
    auto reserved = reserve_worker_handles(workers, options_.maximum_concurrent_connections);
    if (!reserved) {
        return base::fail(std::move(reserved).error());
    }

    WorkerState state;
    std::optional<base::Error> terminal_failure;
    while (!shutdown_requested()) {
        reap_finished_workers(workers);
        if (auto runtime_failure = take_runtime_failure(state, control_->worker_mutex);
            runtime_failure.has_value()) {
            terminal_failure.emplace(std::move(*runtime_failure));
            request_shutdown();
            break;
        }
        if (!reserve_worker_slot(
                state,
                control_->worker_mutex,
                control_->worker_changed,
                options_.maximum_concurrent_connections,
                control_->shutdown_requested
            )) {
            break;
        }
        if (auto runtime_failure = take_runtime_failure(state, control_->worker_mutex);
            runtime_failure.has_value()) {
            release_worker_slot(state, control_->worker_mutex, control_->worker_changed);
            terminal_failure.emplace(std::move(*runtime_failure));
            request_shutdown();
            break;
        }
        auto accepted = poll_next_connection(
            listener_,
            control_->shutdown_requested,
            options_.shutdown_poll_interval
        );
        if (!accepted) {
            release_worker_slot(state, control_->worker_mutex, control_->worker_changed);
            base::Error error = std::move(accepted).error();
            if (is_shutdown_error(error)) {
                break;
            }
            terminal_failure.emplace(std::move(error));
            request_shutdown();
            break;
        }
        if (!accepted.value().has_value()) {
            release_worker_slot(state, control_->worker_mutex, control_->worker_changed);
            continue;
        }
        if (shutdown_requested()) {
            release_worker_slot(state, control_->worker_mutex, control_->worker_changed);
            break;
        }
        reap_finished_workers(workers);
        auto started = start_worker(
            state,
            control_->worker_mutex,
            control_->worker_changed,
            workers,
            std::move(*accepted).value(),
            [this](io::TcpStream connection) {
                auto outcome = serve_connection(std::move(connection));
                return WorkerOutcome{
                    .status = std::move(outcome.status),
                    .is_peer_failure = outcome.is_peer_failure,
                    .is_shutdown = outcome.is_shutdown,
                };
            }
        );
        if (!started) {
            terminal_failure.emplace(std::move(started).error());
            request_shutdown();
            break;
        }
    }

    request_shutdown();
    join_workers(workers);
    if (auto runtime_failure = take_runtime_failure(state, control_->worker_mutex);
        runtime_failure.has_value() && !terminal_failure.has_value()) {
        terminal_failure.emplace(std::move(*runtime_failure));
    }
    auto closed = listener_.close();
    if (terminal_failure.has_value()) {
        return base::fail(std::move(*terminal_failure));
    }
    if (!closed) {
        return base::fail(std::move(closed).error());
    }
    return {};
}

void TcpServer::request_shutdown() noexcept {
    if (control_ != nullptr) {
        {
            std::lock_guard lock{control_->worker_mutex};
            control_->shutdown_requested.store(true, std::memory_order_release);
        }
        control_->worker_changed.notify_all();
    }
}

auto TcpServer::shutdown_requested() const noexcept -> bool {
    return control_ == nullptr || control_->shutdown_requested.load(std::memory_order_acquire);
}

auto TcpServer::serve_connection(io::TcpStream stream) -> TcpServer::ConnectionOutcome {
    try {
        const auto failure = [](base::Error error) {
            const bool shutdown = is_shutdown_error(error);
            const bool peer_failure = !shutdown && is_peer_transport_failure(error);
            return ConnectionOutcome{
                .status = base::fail(std::move(error)),
                .is_peer_failure = peer_failure,
                .is_shutdown = shutdown,
            };
        };
        if (shutdown_requested()) {
            return failure(shutdown_requested_error("start connection"));
        }
        auto no_delay = stream.set_no_delay();
        if (!no_delay) {
            return failure(std::move(no_delay).error());
        }

        internal::TimedTcpStream timed_stream{
            stream,
            control_->shutdown_requested,
            options_.response_write_timeout,
            options_.shutdown_poll_interval,
        };
        std::array<std::byte, kRmdbV3PrefaceSize> preface{};
        const auto handshake_deadline =
            std::chrono::steady_clock::now() + options_.handshake_timeout;
        auto read_preface =
            timed_stream.read_exact_until(preface, handshake_deadline, "read RMDB preface");
        if (!read_preface) {
            return failure(std::move(read_preface).error());
        }
        auto valid_preface = validate_rmdb_v3_preface(preface);
        if (!valid_preface) {
            return failure(std::move(valid_preface).error());
        }
        auto write_preface = timed_stream.write_all(kRmdbV3Preface, "write RMDB preface");
        if (!write_preface) {
            return failure(std::move(write_preface).error());
        }
        if (shutdown_requested()) {
            return failure(shutdown_requested_error("open session"));
        }

        auto session = session_factory_->open_session();
        if (!session) {
            return ConnectionOutcome{
                .status = base::fail(std::move(session).error()),
                .is_peer_failure = false,
                .is_shutdown = false,
            };
        }
        if (!session.value()) {
            return ConnectionOutcome{
                .status = base::fail(runtime_error(
                    base::ErrorCode::kInvariantViolation,
                    "session factory returned a null connection session",
                    "open session"
                )),
                .is_peer_failure = false,
                .is_shutdown = false,
            };
        }

        SessionCleanup cleanup{std::move(session).value()};
        ConnectionOutcome outcome{};
        {
            auto dispatcher = RequestDispatcher::create(cleanup.session());
            if (!dispatcher) {
                outcome = ConnectionOutcome{
                    .status = base::fail(std::move(dispatcher).error()),
                    .is_peer_failure = false,
                    .is_shutdown = false,
                };
            } else {
                RequestDispatcher request_dispatcher = std::move(dispatcher).value();
                RequestLoopOutcome request_outcome =
                    serve_request_loop(timed_stream, request_dispatcher, options_);
                outcome = ConnectionOutcome{
                    .status = std::move(request_outcome.status),
                    .is_peer_failure = request_outcome.is_peer_failure,
                    .is_shutdown = request_outcome.is_shutdown,
                };
            }
        }
        auto close_session = cleanup.close();
        if (!close_session) {
            return ConnectionOutcome{
                .status = base::fail(std::move(close_session).error()),
                .is_peer_failure = false,
                .is_shutdown = false,
            };
        }
        return outcome;
    } catch (const std::exception& exception) {
        return ConnectionOutcome{
            .status = base::fail(exception_error(exception)),
            .is_peer_failure = false,
            .is_shutdown = false,
        };
    } catch (...) {
        return ConnectionOutcome{
            .status = base::fail(unknown_exception_error()),
            .is_peer_failure = false,
            .is_shutdown = false,
        };
    }
}

}  // namespace db::protocol
