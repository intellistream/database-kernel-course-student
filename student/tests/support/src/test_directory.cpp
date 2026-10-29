#include "db/test_support/test_directory.hpp"

#include <atomic>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/result.hpp"

namespace db::test_support {
namespace {

auto directory_sequence() -> std::atomic<std::uint64_t>& {
    static std::atomic<std::uint64_t> sequence{0};
    return sequence;
}

auto sanitize_label(const std::string_view label) -> std::string {
    std::string sanitized;
    sanitized.reserve(label.size());
    for (const char character : label) {
        const auto byte = static_cast<unsigned char>(character);
        sanitized.push_back(
            std::isalnum(byte) != 0 || character == '-' || character == '_' ? character : '_'
        );
    }
    return sanitized.empty() ? "test" : sanitized;
}

}  // namespace

TestDirectory::TestDirectory(std::filesystem::path path) noexcept
    : path_{std::move(path)} {}

TestDirectory::TestDirectory(TestDirectory&& other) noexcept
    : path_{std::move(other.path_)},
      owns_path_{std::exchange(other.owns_path_, false)} {}

auto TestDirectory::operator=(TestDirectory&& other) noexcept -> TestDirectory& {
    if (this != &other) {
        cleanup_noexcept();
        path_ = std::move(other.path_);
        owns_path_ = std::exchange(other.owns_path_, false);
    }
    return *this;
}

TestDirectory::~TestDirectory() {
    cleanup_noexcept();
}

auto TestDirectory::create(const std::string_view label) -> db::base::Result<TestDirectory> {
    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path(error);
    if (error) {
        return db::base::fail(
            db::base::Error{db::base::ErrorCode::kIo, "find temporary directory", error}
        );
    }

    const std::string safe_label = sanitize_label(label);
    for (std::size_t attempt = 0; attempt < 1024; ++attempt) {
        const std::uint64_t sequence = directory_sequence().fetch_add(1, std::memory_order_relaxed);
        const std::filesystem::path candidate =
            root
            / ("db-" + std::to_string(static_cast<std::uint64_t>(::getpid())) + "-"
               + std::to_string(sequence) + "-" + safe_label);
        error.clear();
        if (std::filesystem::create_directory(candidate, error)) {
            return TestDirectory{candidate};
        }
        if (error) {
            return db::base::fail(
                db::base::Error{
                    db::base::ErrorCode::kIo,
                    "create temporary directory",
                    error,
                }
            );
        }
    }
    return db::base::fail(
        db::base::Error{
            db::base::ErrorCode::kResourceExhausted,
            "could not allocate a unique temporary directory",
        }
    );
}

auto TestDirectory::path() const noexcept -> const std::filesystem::path& {
    return path_;
}

auto TestDirectory::join(const std::filesystem::path& child) const -> std::filesystem::path {
    return path_ / child;
}

auto TestDirectory::cleanup() -> db::base::Status {
    if (!owns_path_) {
        return {};
    }
    std::error_code error;
    std::filesystem::remove_all(path_, error);
    if (error) {
        return db::base::fail(
            db::base::Error{db::base::ErrorCode::kIo, "remove temporary directory", error}
        );
    }
    owns_path_ = false;
    return {};
}

void TestDirectory::cleanup_noexcept() noexcept {
    if (!owns_path_) {
        return;
    }
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
    owns_path_ = false;
}

}  // namespace db::test_support
