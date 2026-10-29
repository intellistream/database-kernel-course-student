#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <sys/socket.h>

#include <gtest/gtest.h>

#include "db/base/bytes.hpp"
#include "db/base/error.hpp"
#include "db/io/socket.hpp"
#include "db/protocol/frame_codec.hpp"
#include "db/protocol/tcp_server.hpp"
#include "db/types/value.hpp"

#include "rmdb_v3_fixture_corpus.hpp"

namespace db::protocol {
namespace {

using namespace std::chrono_literals;

struct SessionCounters final {
    std::atomic<std::size_t> opened{0U};
    std::atomic<std::size_t> closed{0U};
    std::atomic<std::size_t> live{0U};
    std::atomic<std::size_t> maximum_live{0U};

    auto wait_for_opened(const std::size_t expected) const -> bool {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (opened.load(std::memory_order_acquire) < expected) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    }
};

void record_opened_session(SessionCounters& counters) {
    counters.opened.fetch_add(1U, std::memory_order_acq_rel);
    const std::size_t live = counters.live.fetch_add(1U, std::memory_order_acq_rel) + 1U;
    std::size_t maximum = counters.maximum_live.load(std::memory_order_acquire);
    while (maximum < live
           && !counters.maximum_live.compare_exchange_weak(
               maximum,
               live,
               std::memory_order_acq_rel,
               std::memory_order_acquire
           )) {
        // compare_exchange_weak refreshes maximum after a concurrent update.
    }
}

class RecordingSession final : public SessionPort {
public:
    explicit RecordingSession(std::shared_ptr<SessionCounters> counters)
        : counters_{std::move(counters)} {}

    auto execute_stream(const std::string_view ignored_sql)
        -> base::Result<ExecutionResult> override {
        static_cast<void>(ignored_sql);
        return base::fail(
            base::Error{
                base::ErrorCode::kUnsupported,
                "TCP lifetime test does not execute SQL",
            }
        );
    }

    auto prepare(
        const std::string_view ignored_sql,
        const std::span<const WireDataType> ignored_parameter_types
    ) -> base::Result<std::unique_ptr<PreparedStatementPort>> override {
        static_cast<void>(ignored_sql);
        static_cast<void>(ignored_parameter_types);
        return base::fail(
            base::Error{
                base::ErrorCode::kUnsupported,
                "TCP lifetime test does not prepare SQL",
            }
        );
    }

    auto begin() -> base::Status override {
        return {};
    }

    auto commit() -> base::Status override {
        return {};
    }

    auto rollback() -> base::Status override {
        return {};
    }

    auto close() -> base::Status override {
        if (closed_) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvariantViolation,
                    "protocol session closed more than once",
                }
            );
        }
        closed_ = true;
        counters_->closed.fetch_add(1U, std::memory_order_acq_rel);
        counters_->live.fetch_sub(1U, std::memory_order_acq_rel);
        return {};
    }

private:
    std::shared_ptr<SessionCounters> counters_;
    bool closed_{false};
};

class RecordingSessionFactory final : public SessionFactoryPort {
public:
    explicit RecordingSessionFactory(std::shared_ptr<SessionCounters> counters)
        : counters_{std::move(counters)} {}

    auto open_session() -> base::Result<std::unique_ptr<SessionPort>> override {
        std::unique_ptr<SessionPort> session = std::make_unique<RecordingSession>(counters_);
        record_opened_session(*counters_);
        return session;
    }

private:
    std::shared_ptr<SessionCounters> counters_;
};

auto two_result_columns() -> std::vector<ColumnMetadata> {
    return {
        ColumnMetadata{.name = "total", .data_type = WireDataType::kInt32},
        ColumnMetadata{.name = "note", .data_type = WireDataType::kChar},
    };
}

auto two_value_query() -> ExecutionResult {
    auto result = ExecutionResult::query(
        two_result_columns(),
        {{types::Value::integer(42), types::Value::character("hi")}}
    );
    if (!result) {
        ADD_FAILURE() << result.error().to_string();
        std::terminate();
    }
    return std::move(result).value();
}

class ProtocolPreparedStatement final : public PreparedStatementPort {
public:
    auto parameter_types() const noexcept -> std::span<const WireDataType> override {
        return parameter_types_;
    }

    auto result_columns() const noexcept -> std::span<const ColumnMetadata> override {
        return columns_;
    }

    auto execute(const std::span<const types::Value> parameters)
        -> base::Result<ExecutionResult> override {
        if (parameters.size() != parameter_types_.size()) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvariantViolation,
                    "protocol test prepared statement received the wrong parameter count",
                }
            );
        }
        return two_value_query();
    }

private:
    std::vector<WireDataType> parameter_types_{
        WireDataType::kInt32,
        WireDataType::kChar,
    };
    std::vector<ColumnMetadata> columns_{two_result_columns()};
};

class ProtocolSession final : public SessionPort {
public:
    explicit ProtocolSession(std::shared_ptr<SessionCounters> counters)
        : counters_{std::move(counters)} {}

    auto execute_stream(const std::string_view sql) -> base::Result<ExecutionResult> override {
        if (sql != "SELECT 1;") {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvariantViolation,
                    "protocol test session received an unexpected stream SQL request",
                }
            );
        }
        return two_value_query();
    }

    auto prepare(const std::string_view sql, const std::span<const WireDataType> parameter_types)
        -> base::Result<std::unique_ptr<PreparedStatementPort>> override {
        const std::array<WireDataType, 2U> expected_types{
            WireDataType::kInt32,
            WireDataType::kChar,
        };
        if (sql != "SELECT $1, $2;"
            || !std::equal(
                parameter_types.begin(),
                parameter_types.end(),
                expected_types.begin(),
                expected_types.end()
            )) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvariantViolation,
                    "protocol test session received an unexpected prepared SQL request",
                }
            );
        }
        std::unique_ptr<PreparedStatementPort> statement =
            std::make_unique<ProtocolPreparedStatement>();
        return statement;
    }

    auto begin() -> base::Status override {
        return {};
    }

    auto commit() -> base::Status override {
        return {};
    }

    auto rollback() -> base::Status override {
        return {};
    }

    auto close() -> base::Status override {
        if (closed_) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvariantViolation,
                    "protocol session closed more than once",
                }
            );
        }
        closed_ = true;
        counters_->closed.fetch_add(1U, std::memory_order_acq_rel);
        counters_->live.fetch_sub(1U, std::memory_order_acq_rel);
        return {};
    }

private:
    std::shared_ptr<SessionCounters> counters_;
    bool closed_{false};
};

class ProtocolSessionFactory final : public SessionFactoryPort {
public:
    explicit ProtocolSessionFactory(std::shared_ptr<SessionCounters> counters)
        : counters_{std::move(counters)} {}

    auto open_session() -> base::Result<std::unique_ptr<SessionPort>> override {
        std::unique_ptr<SessionPort> session = std::make_unique<ProtocolSession>(counters_);
        record_opened_session(*counters_);
        return session;
    }

private:
    std::shared_ptr<SessionCounters> counters_;
};

class BackpressureSession final : public SessionPort {
public:
    explicit BackpressureSession(std::shared_ptr<SessionCounters> counters)
        : counters_{std::move(counters)} {}

    auto execute_stream(const std::string_view sql) -> base::Result<ExecutionResult> override {
        if (sql != "SELECT 1;") {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvariantViolation,
                    "backpressure session received an unexpected stream SQL request",
                }
            );
        }

        // Eight 512 KiB rows exceed a loopback sender's bounded socket queue
        // after the peer advertises a tiny receive window, without making the
        // test spend most of its time materializing hundreds of small rows.
        constexpr std::size_t row_count = 8U;
        constexpr std::size_t value_length = std::size_t{512U} * std::size_t{1024U};
        const std::string value(value_length, 'x');
        std::vector<std::vector<types::Value>> rows;
        rows.reserve(row_count);
        for (std::size_t row = 0U; row < row_count; ++row) {
            rows.push_back({types::Value::character(value)});
        }
        return ExecutionResult::query(
            {ColumnMetadata{.name = "payload", .data_type = WireDataType::kChar}},
            std::move(rows)
        );
    }

    auto prepare(
        const std::string_view ignored_sql,
        const std::span<const WireDataType> ignored_parameter_types
    ) -> base::Result<std::unique_ptr<PreparedStatementPort>> override {
        static_cast<void>(ignored_sql);
        static_cast<void>(ignored_parameter_types);
        return base::fail(
            base::Error{
                base::ErrorCode::kUnsupported,
                "backpressure session does not prepare statements",
            }
        );
    }

    auto begin() -> base::Status override {
        return {};
    }

    auto commit() -> base::Status override {
        return {};
    }

    auto rollback() -> base::Status override {
        return {};
    }

    auto close() -> base::Status override {
        if (closed_) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvariantViolation,
                    "backpressure session closed more than once",
                }
            );
        }
        closed_ = true;
        counters_->closed.fetch_add(1U, std::memory_order_acq_rel);
        counters_->live.fetch_sub(1U, std::memory_order_acq_rel);
        return {};
    }

private:
    std::shared_ptr<SessionCounters> counters_;
    bool closed_{false};
};

class BackpressureSessionFactory final : public SessionFactoryPort {
public:
    explicit BackpressureSessionFactory(std::shared_ptr<SessionCounters> counters)
        : counters_{std::move(counters)} {}

    auto open_session() -> base::Result<std::unique_ptr<SessionPort>> override {
        std::unique_ptr<SessionPort> session = std::make_unique<BackpressureSession>(counters_);
        record_opened_session(*counters_);
        return session;
    }

private:
    std::shared_ptr<SessionCounters> counters_;
};

auto bounded_connection_options() -> TcpServerOptions {
    return TcpServerOptions{
        .maximum_concurrent_connections = 4U,
        .listen_backlog = 16U,
        .handshake_timeout = 250ms,
        .idle_timeout = 250ms,
        .request_timeout = 250ms,
        .response_write_timeout = 250ms,
        .shutdown_poll_interval = 5ms,
    };
}

struct ObservedFrame final {
    FrameHeader header;
    std::vector<std::byte> payload;
};

auto read_server_frame(io::TcpStream& stream) -> base::Result<ObservedFrame> {
    std::array<std::byte, kRmdbFrameHeaderSize> header_bytes{};
    auto read_header = stream.read_exact(header_bytes);
    if (!read_header) {
        return base::fail(std::move(read_header).error());
    }
    base::BigEndianReader reader{header_bytes};
    auto payload_length = reader.read_u32();
    auto tag = reader.read_u8();
    auto flags = reader.read_u8();
    auto reserved = reader.read_u16();
    if (!payload_length || !tag || !flags || !reserved || !reader.finish()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvariantViolation,
                "protocol test could not decode its fixed server frame header",
            }
        );
    }
    std::vector<std::byte> payload(payload_length.value());
    auto read_payload = stream.read_exact(payload);
    if (!read_payload) {
        return base::fail(std::move(read_payload).error());
    }
    return ObservedFrame{
        .header =
            FrameHeader{
                .payload_length = payload_length.value(),
                .tag = tag.value(),
                .flags = flags.value(),
                .reserved = reserved.value(),
            },
        .payload = std::move(payload),
    };
}

auto connect_and_complete_preface(const io::TcpEndpoint& endpoint) -> base::Result<io::TcpStream> {
    auto client = io::TcpStream::connect(endpoint);
    if (!client) {
        return base::fail(std::move(client).error());
    }
    auto wrote = client.value().write_all(kRmdbV3Preface);
    if (!wrote) {
        return base::fail(std::move(wrote).error());
    }
    std::array<std::byte, kRmdbV3PrefaceSize> echoed{};
    auto read = client.value().read_exact(echoed);
    if (!read) {
        return base::fail(std::move(read).error());
    }
    if (echoed != kRmdbV3Preface) {
        return base::fail(
            base::Error{
                base::ErrorCode::kCorruption,
                "TCP server echoed an unexpected RMDB preface",
            }
        );
    }
    return std::move(client).value();
}

TEST(TcpServerTest, EchoesPrefaceAndClosesOneIndependentSessionAtPeerEof) {
    auto counters = std::make_shared<SessionCounters>();
    RecordingSessionFactory factory{counters};
    auto server = TcpServer::bind(io::TcpEndpoint::loopback(), factory);
    ASSERT_TRUE(server);
    const auto endpoint = server.value().local_endpoint();
    ASSERT_TRUE(endpoint);

    std::promise<base::Status> completion;
    auto completed = completion.get_future();
    std::thread worker{[server = std::move(server).value(),
                        completion = std::move(completion)]() mutable {
        completion.set_value(server.serve_one());
    }};

    auto client = connect_and_complete_preface(endpoint.value());
    ASSERT_TRUE(client);
    EXPECT_TRUE(counters->wait_for_opened(1U));
    EXPECT_TRUE(client.value().close());

    worker.join();
    EXPECT_TRUE(completed.get());
    EXPECT_EQ(counters->opened.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(counters->closed.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(counters->live.load(std::memory_order_acquire), 0U);
}

TEST(TcpServerTest, DispatchesStreamFramesAndWritesTheirCanonicalResponseSequence) {
    auto counters = std::make_shared<SessionCounters>();
    ProtocolSessionFactory factory{counters};
    auto server = TcpServer::bind(io::TcpEndpoint::loopback(), factory);
    ASSERT_TRUE(server);
    const auto endpoint = server.value().local_endpoint();
    ASSERT_TRUE(endpoint);

    std::promise<base::Status> completion;
    auto completed = completion.get_future();
    std::thread worker{[server = std::move(server).value(),
                        completion = std::move(completion)]() mutable {
        completion.set_value(server.serve_one());
    }};

    auto client = connect_and_complete_preface(endpoint.value());
    ASSERT_TRUE(client);
    ASSERT_TRUE(client.value().write_all(test::rmdb_v3_fixture::kExecStreamSelectOne));

    std::array<std::byte, test::rmdb_v3_fixture::kMetaTwoColumns.size()> meta{};
    std::array<std::byte, test::rmdb_v3_fixture::kRowTwoValues.size()> row{};
    std::array<std::byte, test::rmdb_v3_fixture::kResultEndOne.size()> result_end{};
    ASSERT_TRUE(client.value().read_exact(meta));
    ASSERT_TRUE(client.value().read_exact(row));
    ASSERT_TRUE(client.value().read_exact(result_end));
    EXPECT_EQ(meta, test::rmdb_v3_fixture::kMetaTwoColumns);
    EXPECT_EQ(row, test::rmdb_v3_fixture::kRowTwoValues);
    EXPECT_EQ(result_end, test::rmdb_v3_fixture::kResultEndOne);
    ASSERT_TRUE(client.value().close());

    worker.join();
    EXPECT_TRUE(completed.get());
    EXPECT_EQ(counters->opened.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(counters->closed.load(std::memory_order_acquire), 1U);
}

TEST(TcpServerTest, ProcessesBufferedPrepareAndBatchFramesInOneConnection) {
    auto counters = std::make_shared<SessionCounters>();
    ProtocolSessionFactory factory{counters};
    auto server = TcpServer::bind(io::TcpEndpoint::loopback(), factory);
    ASSERT_TRUE(server);
    const auto endpoint = server.value().local_endpoint();
    ASSERT_TRUE(endpoint);

    std::promise<base::Status> completion;
    auto completed = completion.get_future();
    std::thread worker{[server = std::move(server).value(),
                        completion = std::move(completion)]() mutable {
        completion.set_value(server.serve_one());
    }};

    auto client = connect_and_complete_preface(endpoint.value());
    ASSERT_TRUE(client);
    std::vector<std::byte> requests;
    requests.insert(
        requests.end(),
        test::rmdb_v3_fixture::kPrepareSetTwoParameters.begin(),
        test::rmdb_v3_fixture::kPrepareSetTwoParameters.end()
    );
    requests.insert(
        requests.end(),
        test::rmdb_v3_fixture::kExecBatchTwoParameters.begin(),
        test::rmdb_v3_fixture::kExecBatchTwoParameters.end()
    );
    ASSERT_TRUE(client.value().write_all(requests));

    std::array<std::byte, test::rmdb_v3_fixture::kPrepareOkTwoColumns.size()> prepare_ok{};
    std::array<std::byte, test::rmdb_v3_fixture::kBatchResultSuccess.size()> batch_result{};
    ASSERT_TRUE(client.value().read_exact(prepare_ok));
    ASSERT_TRUE(client.value().read_exact(batch_result));
    EXPECT_EQ(prepare_ok, test::rmdb_v3_fixture::kPrepareOkTwoColumns);
    EXPECT_EQ(batch_result, test::rmdb_v3_fixture::kBatchResultSuccess);
    ASSERT_TRUE(client.value().close());

    worker.join();
    EXPECT_TRUE(completed.get());
    EXPECT_EQ(counters->opened.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(counters->closed.load(std::memory_order_acquire), 1U);
}

TEST(TcpServerTest, SendsErrorForOneMalformedRequestAndKeepsTheConnectionReusable) {
    auto counters = std::make_shared<SessionCounters>();
    ProtocolSessionFactory factory{counters};
    auto server = TcpServer::bind(io::TcpEndpoint::loopback(), factory);
    ASSERT_TRUE(server);
    const auto endpoint = server.value().local_endpoint();
    ASSERT_TRUE(endpoint);

    std::promise<base::Status> completion;
    auto completed = completion.get_future();
    std::thread worker{[server = std::move(server).value(),
                        completion = std::move(completion)]() mutable {
        completion.set_value(server.serve_one());
    }};

    auto client = connect_and_complete_preface(endpoint.value());
    ASSERT_TRUE(client);
    ASSERT_TRUE(client.value().write_all(test::rmdb_v3_fixture::kRequestReservedNonzero));
    const auto error = read_server_frame(client.value());
    ASSERT_TRUE(error);
    EXPECT_EQ(error.value().header.tag, static_cast<std::uint8_t>(FrameTag::kError));
    EXPECT_EQ(error.value().header.flags, kRmdbNoFrameFlags);
    EXPECT_EQ(error.value().header.reserved, 0U);
    EXPECT_FALSE(error.value().payload.empty());

    ASSERT_TRUE(client.value().write_all(test::rmdb_v3_fixture::kExecStreamSelectOne));
    std::array<std::byte, test::rmdb_v3_fixture::kMetaTwoColumns.size()> meta{};
    std::array<std::byte, test::rmdb_v3_fixture::kRowTwoValues.size()> row{};
    std::array<std::byte, test::rmdb_v3_fixture::kResultEndOne.size()> result_end{};
    ASSERT_TRUE(client.value().read_exact(meta));
    ASSERT_TRUE(client.value().read_exact(row));
    ASSERT_TRUE(client.value().read_exact(result_end));
    EXPECT_EQ(meta, test::rmdb_v3_fixture::kMetaTwoColumns);
    EXPECT_EQ(row, test::rmdb_v3_fixture::kRowTwoValues);
    EXPECT_EQ(result_end, test::rmdb_v3_fixture::kResultEndOne);
    ASSERT_TRUE(client.value().close());

    worker.join();
    EXPECT_TRUE(completed.get());
    EXPECT_EQ(counters->closed.load(std::memory_order_acquire), 1U);
}

TEST(TcpServerTest, TreatsEofInsideAFrameAsAnIsolatedPeerFailure) {
    auto counters = std::make_shared<SessionCounters>();
    ProtocolSessionFactory factory{counters};
    auto server = TcpServer::bind(io::TcpEndpoint::loopback(), factory);
    ASSERT_TRUE(server);
    const auto endpoint = server.value().local_endpoint();
    ASSERT_TRUE(endpoint);

    std::promise<base::Status> completion;
    auto completed = completion.get_future();
    std::thread worker{[server = std::move(server).value(),
                        completion = std::move(completion)]() mutable {
        completion.set_value(server.serve_one());
    }};

    auto client = connect_and_complete_preface(endpoint.value());
    ASSERT_TRUE(client);
    ASSERT_TRUE(
        client.value().write_all(base::Bytes{test::rmdb_v3_fixture::kExecStreamSelectOne}.first(3U))
    );
    ASSERT_TRUE(client.value().close());

    worker.join();
    const auto outcome = completed.get();
    ASSERT_FALSE(outcome);
    EXPECT_EQ(outcome.error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(counters->opened.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(counters->closed.load(std::memory_order_acquire), 1U);
}

TEST(TcpServerTest, RejectsNonpositiveDeadlineOptionsBeforeBinding) {
    auto counters = std::make_shared<SessionCounters>();
    RecordingSessionFactory factory{counters};
    auto options = bounded_connection_options();
    options.request_timeout = 0ms;

    const auto server = TcpServer::bind(io::TcpEndpoint::loopback(), factory, options);
    ASSERT_FALSE(server);
    EXPECT_EQ(server.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(TcpServerTest, BoundsHandshakeAndIdleConnectionsAndCleansSessions) {
    auto handshake_counters = std::make_shared<SessionCounters>();
    RecordingSessionFactory handshake_factory{handshake_counters};
    auto handshake_server = TcpServer::bind(
        io::TcpEndpoint::loopback(),
        handshake_factory,
        bounded_connection_options()
    );
    ASSERT_TRUE(handshake_server);
    const auto handshake_endpoint = handshake_server.value().local_endpoint();
    ASSERT_TRUE(handshake_endpoint);

    std::promise<base::Status> handshake_completion;
    auto handshake_completed = handshake_completion.get_future();
    std::thread handshake_worker{[server = std::move(handshake_server).value(),
                                  completion = std::move(handshake_completion)]() mutable {
        completion.set_value(server.serve_one());
    }};
    auto preface_stall = io::TcpStream::connect(handshake_endpoint.value());
    ASSERT_TRUE(preface_stall);
    handshake_worker.join();
    const auto handshake_result = handshake_completed.get();
    ASSERT_FALSE(handshake_result);
    EXPECT_EQ(handshake_result.error().code(), base::ErrorCode::kUnavailable);
    EXPECT_EQ(handshake_counters->opened.load(std::memory_order_acquire), 0U);
    EXPECT_TRUE(preface_stall.value().close());

    auto idle_counters = std::make_shared<SessionCounters>();
    ProtocolSessionFactory idle_factory{idle_counters};
    auto idle_server =
        TcpServer::bind(io::TcpEndpoint::loopback(), idle_factory, bounded_connection_options());
    ASSERT_TRUE(idle_server);
    const auto idle_endpoint = idle_server.value().local_endpoint();
    ASSERT_TRUE(idle_endpoint);

    std::promise<base::Status> idle_completion;
    auto idle_completed = idle_completion.get_future();
    std::thread idle_worker{[server = std::move(idle_server).value(),
                             completion = std::move(idle_completion)]() mutable {
        completion.set_value(server.serve_one());
    }};
    auto idle_client = connect_and_complete_preface(idle_endpoint.value());
    ASSERT_TRUE(idle_client);
    ASSERT_TRUE(idle_counters->wait_for_opened(1U));
    idle_worker.join();
    const auto idle_result = idle_completed.get();
    ASSERT_FALSE(idle_result);
    EXPECT_EQ(idle_result.error().code(), base::ErrorCode::kUnavailable);
    EXPECT_EQ(idle_counters->closed.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(idle_counters->live.load(std::memory_order_acquire), 0U);
    EXPECT_TRUE(idle_client.value().close());
}

TEST(TcpServerTest, BoundsTrickledPartialRequestByOneAbsoluteDeadline) {
    auto counters = std::make_shared<SessionCounters>();
    ProtocolSessionFactory factory{counters};
    auto server =
        TcpServer::bind(io::TcpEndpoint::loopback(), factory, bounded_connection_options());
    ASSERT_TRUE(server);
    const auto endpoint = server.value().local_endpoint();
    ASSERT_TRUE(endpoint);

    std::promise<base::Status> completion;
    auto completed = completion.get_future();
    std::thread worker{[server = std::move(server).value(),
                        completion = std::move(completion)]() mutable {
        completion.set_value(server.serve_one());
    }};

    auto client = connect_and_complete_preface(endpoint.value());
    ASSERT_TRUE(client);
    ASSERT_TRUE(counters->wait_for_opened(1U));
    const base::Bytes request{test::rmdb_v3_fixture::kExecStreamSelectOne};
    ASSERT_GT(request.size(), 6U);
    ASSERT_TRUE(client.value().write_all(request.first(3U)));
    // Every byte arrives inside the 250 ms request interval. A sliding idle
    // deadline would therefore run about 250 ms after the final write; the
    // implementation must instead retain the deadline established by byte 1.
    for (std::size_t position = 3U; position < 6U; ++position) {
        std::this_thread::sleep_for(60ms);
        ASSERT_TRUE(client.value().write_all(request.subspan(position, 1U)));
    }
    EXPECT_EQ(completed.wait_for(200ms), std::future_status::ready);
    worker.join();
    const auto result = completed.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kUnavailable);
    EXPECT_EQ(counters->closed.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(counters->live.load(std::memory_order_acquire), 0U);
    EXPECT_TRUE(client.value().close());
}

TEST(TcpServerTest, AppliesResponseWriteBackpressureToANonreadingPeer) {
    auto counters = std::make_shared<SessionCounters>();
    BackpressureSessionFactory factory{counters};
    auto server =
        TcpServer::bind(io::TcpEndpoint::loopback(), factory, bounded_connection_options());
    ASSERT_TRUE(server);
    const auto endpoint = server.value().local_endpoint();
    ASSERT_TRUE(endpoint);

    std::promise<base::Status> completion;
    auto completed = completion.get_future();
    std::thread worker{[server = std::move(server).value(),
                        completion = std::move(completion)]() mutable {
        completion.set_value(server.serve_one());
    }};

    auto client = connect_and_complete_preface(endpoint.value());
    ASSERT_TRUE(client);
    const int receive_buffer_size = 1024;
    ASSERT_EQ(
        ::setsockopt(
            client.value().native_handle(),
            SOL_SOCKET,
            SO_RCVBUF,
            &receive_buffer_size,
            static_cast<socklen_t>(sizeof(receive_buffer_size))
        ),
        0
    );
    ASSERT_TRUE(client.value().write_all(test::rmdb_v3_fixture::kExecStreamSelectOne));
    ASSERT_TRUE(counters->wait_for_opened(1U));

    worker.join();
    const auto result = completed.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kUnavailable);
    EXPECT_NE(result.error().to_string().find("write RMDB response frame"), std::string::npos);
    EXPECT_EQ(counters->closed.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(counters->live.load(std::memory_order_acquire), 0U);
    EXPECT_TRUE(client.value().close());
}

TEST(TcpServerTest, TreatsClientAbortDuringResponseAsAnIsolatedPeerFailure) {
    auto counters = std::make_shared<SessionCounters>();
    ProtocolSessionFactory factory{counters};
    auto server =
        TcpServer::bind(io::TcpEndpoint::loopback(), factory, bounded_connection_options());
    ASSERT_TRUE(server);
    const auto endpoint = server.value().local_endpoint();
    ASSERT_TRUE(endpoint);

    std::promise<base::Status> completion;
    auto completed = completion.get_future();
    std::thread worker{[server = std::move(server).value(),
                        completion = std::move(completion)]() mutable {
        completion.set_value(server.serve_one());
    }};

    auto client = connect_and_complete_preface(endpoint.value());
    ASSERT_TRUE(client);
    ASSERT_TRUE(counters->wait_for_opened(1U));
    linger abort_linger{};
    abort_linger.l_onoff = 1;
    abort_linger.l_linger = 0;
    ASSERT_EQ(
        ::setsockopt(
            client.value().native_handle(),
            SOL_SOCKET,
            SO_LINGER,
            &abort_linger,
            static_cast<socklen_t>(sizeof(abort_linger))
        ),
        0
    );
    ASSERT_TRUE(client.value().write_all(test::rmdb_v3_fixture::kExecStreamSelectOne));
    ASSERT_TRUE(client.value().close());

    worker.join();
    const auto result = completed.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(counters->opened.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(counters->closed.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(counters->live.load(std::memory_order_acquire), 0U);
}

TEST(TcpServerTest, ServeForeverStopsIdleWorkersAndClosesTheListener) {
    auto counters = std::make_shared<SessionCounters>();
    RecordingSessionFactory factory{counters};
    auto bound_server =
        TcpServer::bind(io::TcpEndpoint::loopback(), factory, bounded_connection_options());
    ASSERT_TRUE(bound_server);
    TcpServer server{std::move(bound_server).value()};
    const auto endpoint = server.local_endpoint();
    ASSERT_TRUE(endpoint);

    std::promise<base::Status> completion;
    auto completed = completion.get_future();
    std::thread worker{[&server, completion = std::move(completion)]() mutable {
        completion.set_value(server.serve_forever());
    }};

    auto client = connect_and_complete_preface(endpoint.value());
    ASSERT_TRUE(client);
    ASSERT_TRUE(counters->wait_for_opened(1U));
    server.request_shutdown();
    worker.join();

    EXPECT_TRUE(completed.get());
    EXPECT_TRUE(server.shutdown_requested());
    EXPECT_FALSE(server.local_endpoint());
    EXPECT_EQ(counters->opened.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(counters->closed.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(counters->live.load(std::memory_order_acquire), 0U);
    EXPECT_TRUE(client.value().close());
}

TEST(TcpServerTest, ServeForeverReapsFinishedWorkersBeforeTheirSlotIsReused) {
    auto counters = std::make_shared<SessionCounters>();
    RecordingSessionFactory factory{counters};
    auto options = bounded_connection_options();
    options.maximum_concurrent_connections = 1U;
    auto bound_server = TcpServer::bind(io::TcpEndpoint::loopback(), factory, options);
    ASSERT_TRUE(bound_server);
    TcpServer server{std::move(bound_server).value()};
    const auto endpoint = server.local_endpoint();
    ASSERT_TRUE(endpoint);

    std::promise<base::Status> completion;
    auto completed = completion.get_future();
    std::thread worker{[&server, completion = std::move(completion)]() mutable {
        completion.set_value(server.serve_forever());
    }};

    auto first = connect_and_complete_preface(endpoint.value());
    ASSERT_TRUE(first);
    ASSERT_TRUE(counters->wait_for_opened(1U));
    ASSERT_TRUE(first.value().close());

    auto second = connect_and_complete_preface(endpoint.value());
    ASSERT_TRUE(second);
    ASSERT_TRUE(counters->wait_for_opened(2U));
    ASSERT_TRUE(second.value().close());
    server.request_shutdown();
    worker.join();

    EXPECT_TRUE(completed.get());
    EXPECT_EQ(counters->opened.load(std::memory_order_acquire), 2U);
    EXPECT_EQ(counters->closed.load(std::memory_order_acquire), 2U);
    EXPECT_EQ(counters->maximum_live.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(counters->live.load(std::memory_order_acquire), 0U);
}

TEST(TcpServerTest, RejectsBadPrefaceBeforeCreatingAConnectionSession) {
    auto counters = std::make_shared<SessionCounters>();
    RecordingSessionFactory factory{counters};
    auto server = TcpServer::bind(io::TcpEndpoint::loopback(), factory);
    ASSERT_TRUE(server);
    const auto endpoint = server.value().local_endpoint();
    ASSERT_TRUE(endpoint);

    std::promise<base::Status> completion;
    auto completed = completion.get_future();
    std::thread worker{[server = std::move(server).value(),
                        completion = std::move(completion)]() mutable {
        completion.set_value(server.serve_one());
    }};

    auto client = io::TcpStream::connect(endpoint.value());
    ASSERT_TRUE(client);
    auto malformed = kRmdbV3Preface;
    malformed.back() = std::byte{0x01U};
    ASSERT_TRUE(client.value().write_all(malformed));
    ASSERT_TRUE(client.value().close());

    worker.join();
    const auto result = completed.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(counters->opened.load(std::memory_order_acquire), 0U);
    EXPECT_EQ(counters->closed.load(std::memory_order_acquire), 0U);
}

TEST(TcpServerTest, ServeNBoundsConcurrentSessionsAndJoinsEveryWorker) {
    auto counters = std::make_shared<SessionCounters>();
    RecordingSessionFactory factory{counters};
    auto server = TcpServer::bind(
        io::TcpEndpoint::loopback(),
        factory,
        TcpServerOptions{.maximum_concurrent_connections = 1U, .listen_backlog = 8U}
    );
    ASSERT_TRUE(server);
    const auto endpoint = server.value().local_endpoint();
    ASSERT_TRUE(endpoint);

    std::promise<base::Status> completion;
    auto completed = completion.get_future();
    std::thread worker{[server = std::move(server).value(),
                        completion = std::move(completion)]() mutable {
        completion.set_value(server.serve_n(2U));
    }};

    auto first = connect_and_complete_preface(endpoint.value());
    ASSERT_TRUE(first);
    ASSERT_TRUE(counters->wait_for_opened(1U));

    auto second = io::TcpStream::connect(endpoint.value());
    ASSERT_TRUE(second);
    ASSERT_TRUE(second.value().write_all(kRmdbV3Preface));
    ASSERT_TRUE(first.value().close());

    std::array<std::byte, kRmdbV3PrefaceSize> echoed{};
    ASSERT_TRUE(second.value().read_exact(echoed));
    EXPECT_EQ(echoed, kRmdbV3Preface);
    ASSERT_TRUE(counters->wait_for_opened(2U));
    EXPECT_TRUE(second.value().close());

    worker.join();
    EXPECT_TRUE(completed.get());
    EXPECT_EQ(counters->opened.load(std::memory_order_acquire), 2U);
    EXPECT_EQ(counters->closed.load(std::memory_order_acquire), 2U);
    EXPECT_EQ(counters->maximum_live.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(counters->live.load(std::memory_order_acquire), 0U);
}

TEST(TcpServerTest, ServeNIsolatesMalformedPeerFailuresFromLaterConnections) {
    auto counters = std::make_shared<SessionCounters>();
    RecordingSessionFactory factory{counters};
    auto server = TcpServer::bind(io::TcpEndpoint::loopback(), factory);
    ASSERT_TRUE(server);
    const auto endpoint = server.value().local_endpoint();
    ASSERT_TRUE(endpoint);

    std::promise<base::Status> completion;
    auto completed = completion.get_future();
    std::thread worker{[server = std::move(server).value(),
                        completion = std::move(completion)]() mutable {
        completion.set_value(server.serve_n(2U));
    }};

    auto malformed = io::TcpStream::connect(endpoint.value());
    ASSERT_TRUE(malformed);
    const std::array<std::byte, kRmdbV3PrefaceSize> bad_preface{};
    ASSERT_TRUE(malformed.value().write_all(bad_preface));
    ASSERT_TRUE(malformed.value().close());

    auto valid = connect_and_complete_preface(endpoint.value());
    ASSERT_TRUE(valid);
    ASSERT_TRUE(valid.value().close());

    worker.join();
    EXPECT_TRUE(completed.get());
    EXPECT_EQ(counters->opened.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(counters->closed.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(counters->live.load(std::memory_order_acquire), 0U);
}

}  // namespace
}  // namespace db::protocol
