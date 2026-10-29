#pragma once

#include <filesystem>
#include <string_view>

#include "db/base/result.hpp"

namespace db::test_support {

class TestDirectory final {
public:
    TestDirectory(const TestDirectory&) = delete;
    auto operator=(const TestDirectory&) -> TestDirectory& = delete;
    TestDirectory(TestDirectory&& other) noexcept;
    auto operator=(TestDirectory&& other) noexcept -> TestDirectory&;
    ~TestDirectory();

    static auto create(std::string_view label) -> db::base::Result<TestDirectory>;

    auto path() const noexcept -> const std::filesystem::path&;
    auto join(const std::filesystem::path& child) const -> std::filesystem::path;
    auto cleanup() -> db::base::Status;

private:
    explicit TestDirectory(std::filesystem::path path) noexcept;
    void cleanup_noexcept() noexcept;

    std::filesystem::path path_;
    bool owns_path_{true};
};

}  // namespace db::test_support
