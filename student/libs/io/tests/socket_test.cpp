#include <array>
#include <chrono>
#include <cstddef>
#include <fcntl.h>
#include <future>
#include <thread>
#include <utility>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/io/socket.hpp"

namespace db::io {
namespace {

using namespace std::chrono_literals;

template <typename Value>
void expect_unavailable(const base::Result<Value>& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kUnavailable);
}

void expect_closed(const TcpStream& stream) {
    // This helper also validates the class's documented moved-from state.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(stream.is_open());
    EXPECT_EQ(stream.native_handle(), -1);

    std::array<std::byte, 1> byte{};
    expect_unavailable(stream.read_some(byte));
    expect_unavailable(stream.try_read_some(byte));
    expect_unavailable(stream.read_exact(byte));
    expect_unavailable(stream.write_some(byte));
    expect_unavailable(stream.try_write_some(byte));
    expect_unavailable(stream.write_all(byte));
    expect_unavailable(stream.wait_readable(0ms));
    expect_unavailable(stream.wait_writable(0ms));
    expect_unavailable(stream.set_no_delay());
}

void expect_closed(const TcpListener& listener) {
    // This helper also validates the class's documented moved-from state.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(listener.is_open());
    EXPECT_EQ(listener.native_handle(), -1);
    expect_unavailable(listener.local_endpoint());
    expect_unavailable(listener.wait_readable(0ms));
    expect_unavailable(listener.accept());
    expect_unavailable(listener.try_accept());
}

TEST(TcpEndpointTest, ParsesNumericIpv4EndpointsAndRejectsAmbiguousSpellings) {
    const auto endpoint = TcpEndpoint::parse("127.0.0.1:8765");
    ASSERT_TRUE(endpoint);
    EXPECT_EQ(endpoint.value(), TcpEndpoint::loopback(8765U));
    EXPECT_EQ(endpoint.value().to_string(), "127.0.0.1:8765");

    for (const char* const invalid : {
             "",
             "127.0.0.1",
             ":8765",
             "127.0.0.1:",
             "localhost:8765",
             "127.0.0.1:not-a-port",
             "127.0.0.1:65536",
         }) {
        const auto parsed = TcpEndpoint::parse(invalid);
        EXPECT_FALSE(parsed) << invalid;
        if (!parsed) {
            EXPECT_EQ(parsed.error().code(), base::ErrorCode::kInvalidArgument) << invalid;
        }
    }
}

TEST(TcpSocketTest, AcceptsBlockingLoopbackStreamsWithExactReadAndWrite) {
    auto listener = TcpListener::bind(TcpEndpoint::loopback());
    ASSERT_TRUE(listener);
    const auto endpoint = listener.value().local_endpoint();
    ASSERT_TRUE(endpoint);
    EXPECT_EQ(endpoint.value().address, TcpEndpoint::loopback().address);
    ASSERT_NE(endpoint.value().port, 0U);

    std::promise<base::Status> completion;
    auto completed = completion.get_future();
    std::thread server{[listener = std::move(listener).value(),
                        completion = std::move(completion)]() mutable {
        auto accepted = listener.accept();
        if (!accepted) {
            completion.set_value(base::fail(std::move(accepted).error()));
            return;
        }
        auto no_delay = accepted.value().set_no_delay();
        if (!no_delay) {
            completion.set_value(std::move(no_delay));
            return;
        }
        std::array<std::byte, 4> received{};
        auto read = accepted.value().read_exact(received);
        if (!read) {
            completion.set_value(std::move(read));
            return;
        }
        completion.set_value(accepted.value().write_all(received));
    }};

    auto client = TcpStream::connect(endpoint.value());
    ASSERT_TRUE(client);
    ASSERT_TRUE(client.value().set_no_delay());
    const std::array<std::byte, 4> request{
        std::byte{0x01U},
        std::byte{0x02U},
        std::byte{0x03U},
        std::byte{0x04U},
    };
    ASSERT_TRUE(client.value().write_all(request));
    std::array<std::byte, 4> response{};
    ASSERT_TRUE(client.value().read_exact(response));
    EXPECT_EQ(response, request);

    server.join();
    EXPECT_TRUE(completed.get());

    TcpStream source_client{std::move(client).value()};
    TcpStream moved_client{std::move(source_client)};
    // Inspecting the documented closed source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_closed(source_client);
    EXPECT_TRUE(moved_client.close());
    expect_closed(moved_client);
}

TEST(TcpSocketTest, PreservesMoveOnlyOwnershipAndRejectsClosedOperations) {
    auto opened = TcpListener::bind(TcpEndpoint::loopback());
    ASSERT_TRUE(opened);
    TcpListener source{std::move(opened).value()};
    TcpListener listener{std::move(source)};
    // Inspecting the documented closed source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_closed(source);
    EXPECT_TRUE(listener.is_open());
    EXPECT_GE(listener.native_handle(), 0);
    EXPECT_TRUE(listener.close());
    expect_closed(listener);
    EXPECT_TRUE(listener.close());
}

TEST(TcpSocketTest, WaitsForReadinessWithoutChangingBlockingOwnership) {
    auto listener = TcpListener::bind(TcpEndpoint::loopback());
    ASSERT_TRUE(listener);
    const auto endpoint = listener.value().local_endpoint();
    ASSERT_TRUE(endpoint);

    const auto idle_listener = listener.value().wait_readable(5ms);
    ASSERT_TRUE(idle_listener);
    EXPECT_EQ(idle_listener.value(), TcpWaitResult::kTimedOut);
    const auto negative_listener_timeout = listener.value().wait_readable(-1ms);
    ASSERT_FALSE(negative_listener_timeout);
    EXPECT_EQ(negative_listener_timeout.error().code(), base::ErrorCode::kInvalidArgument);
    const auto unavailable_accept = listener.value().try_accept();
    ASSERT_FALSE(unavailable_accept);
    EXPECT_EQ(unavailable_accept.error().code(), base::ErrorCode::kUnavailable);

    auto client = TcpStream::connect(endpoint.value());
    ASSERT_TRUE(client);
    const auto listener_ready = listener.value().wait_readable(1s);
    ASSERT_TRUE(listener_ready);
    ASSERT_EQ(listener_ready.value(), TcpWaitResult::kReady);
    auto accepted = listener.value().try_accept();
    ASSERT_TRUE(accepted);
    // POSIX exposes fcntl(2) as a variadic C API.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const int accepted_flags = ::fcntl(accepted.value().native_handle(), F_GETFL);
    ASSERT_NE(accepted_flags, -1);
    EXPECT_EQ(accepted_flags & O_NONBLOCK, 0);
    const auto idle_stream = accepted.value().wait_readable(5ms);
    ASSERT_TRUE(idle_stream);
    EXPECT_EQ(idle_stream.value(), TcpWaitResult::kTimedOut);
    std::array<std::byte, 1> unavailable_buffer{};
    const auto unavailable_read = accepted.value().try_read_some(unavailable_buffer);
    ASSERT_FALSE(unavailable_read);
    EXPECT_EQ(unavailable_read.error().code(), base::ErrorCode::kUnavailable);

    std::promise<base::Status> completion;
    auto completed = completion.get_future();
    std::thread server{[stream = std::move(accepted).value(),
                        completion = std::move(completion)]() mutable {
        auto readable = stream.wait_readable(1s);
        if (!readable) {
            completion.set_value(base::fail(std::move(readable).error()));
            return;
        }
        if (readable.value() != TcpWaitResult::kReady) {
            completion.set_value(
                base::fail(
                    base::Error{
                        base::ErrorCode::kUnavailable,
                        "TCP stream did not become readable after a client write",
                    }
                )
            );
            return;
        }
        std::array<std::byte, 1> byte{};
        auto read = stream.try_read_some(byte);
        if (!read) {
            completion.set_value(base::fail(std::move(read).error()));
            return;
        }
        if (read.value() != byte.size()) {
            completion.set_value(
                base::fail(
                    base::Error{
                        base::ErrorCode::kIo,
                        "TCP nonblocking read did not make progress",
                    }
                )
            );
            return;
        }
        auto writable = stream.wait_writable(1s);
        if (!writable) {
            completion.set_value(base::fail(std::move(writable).error()));
            return;
        }
        if (writable.value() != TcpWaitResult::kReady) {
            completion.set_value(
                base::fail(
                    base::Error{
                        base::ErrorCode::kUnavailable,
                        "TCP stream did not become writable",
                    }
                )
            );
            return;
        }
        auto wrote = stream.try_write_some(byte);
        if (!wrote) {
            completion.set_value(base::fail(std::move(wrote).error()));
            return;
        }
        if (wrote.value() != byte.size()) {
            completion.set_value(
                base::fail(
                    base::Error{
                        base::ErrorCode::kIo,
                        "TCP nonblocking write did not make progress",
                    }
                )
            );
            return;
        }
        completion.set_value({});
    }};

    const std::array<std::byte, 1> request{std::byte{0x7FU}};
    ASSERT_TRUE(client.value().write_all(request));
    const auto readable = client.value().wait_readable(1s);
    ASSERT_TRUE(readable);
    EXPECT_EQ(readable.value(), TcpWaitResult::kReady);
    std::array<std::byte, 1> response{};
    const auto received = client.value().try_read_some(response);
    ASSERT_TRUE(received);
    EXPECT_EQ(received.value(), response.size());
    EXPECT_EQ(response, request);

    server.join();
    EXPECT_TRUE(completed.get());
}

}  // namespace
}  // namespace db::io
