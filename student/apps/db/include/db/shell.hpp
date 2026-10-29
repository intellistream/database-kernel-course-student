#pragma once

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "db/api/api.hpp"
#include "db/base/result.hpp"

namespace db::shell {

struct ShellOptions final {
    std::filesystem::path data_directory{"db-data"};
    std::size_t buffer_pool_pages{256U};
    std::size_t wal_ring_pages{128U};
};

// Parses the executable name plus one optional nonempty data-directory
// argument. Allocation and path conversion remain inside the shell's
// structured application boundary.
auto parse_shell_options(std::span<char*> arguments) -> base::Result<ShellOptions>;

auto statement_complete(std::string_view sql) noexcept -> bool;
auto is_quit_command(std::string_view line) noexcept -> bool;
auto format_cell(const api::Value& value) -> std::string;

void print_result_set(const api::ResultSet& result_set, std::ostream& output);
void print_dispatch_result(const api::DispatchResult& result, std::ostream& output);

// Interactive application facade. Database/session ownership and SQL source
// nodes remain private so consumers depend only on the stable embedded API.
// Move construction transfers the only fallible lifecycle owner; assignment
// over a live shell is disabled because its shutdown error cannot be discarded.
class Shell final {
public:
    static auto open(const ShellOptions& options = {}) -> base::Result<Shell>;

    Shell(Shell&& other) noexcept;
    auto operator=(Shell&& other) -> Shell& = delete;
    Shell(const Shell&) = delete;
    auto operator=(const Shell&) -> Shell& = delete;
    ~Shell();

    auto run(std::istream& input, std::ostream& output, std::ostream& errors) -> base::Status;

private:
    struct Impl;

    explicit Shell(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
};

}  // namespace db::shell
