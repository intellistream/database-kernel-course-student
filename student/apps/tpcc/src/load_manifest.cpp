#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/exception_boundary.hpp"

#include "array_access.hpp"
#include "load.hpp"
#include "load_contract.hpp"

namespace db::tpcc {
namespace {

inline constexpr std::string_view kDatasetContractVersion = "tpcc-5.11-f32-nonull-2025-01-01-v1";
inline constexpr std::uintmax_t kMaximumManifestBytes = std::uintmax_t{64U} * std::uintmax_t{1024U};
inline constexpr base::ExceptionBoundaryPolicy kLoadManifestBoundary{
    .component = "tpcc.load",
    .allocation_message = "TPC-C loader could not allocate manifest state",
    .exception_prefix = "unexpected exception while inspecting the TPC-C dataset: ",
    .unknown_exception_message = "unknown exception while inspecting the TPC-C dataset",
};

using ManifestFields = std::map<std::string, std::string, std::less<>>;

struct FileRecord final {
    std::string_view name;
    std::uint64_t size{0U};
};

auto manifest_error(std::string message) -> base::detail::Failure {
    return base::fail(
        base::Error{base::ErrorCode::kInvalidArgument, std::move(message)}
            .with_context("tpcc.load", "inspect dataset manifest")
    );
}

auto manifest_io_error(
    std::string message,
    const std::filesystem::path& path,
    const std::error_code error
) -> base::detail::Failure {
    message.append(": ").append(path.string());
    return base::fail(
        base::Error{base::ErrorCode::kIo, std::move(message), error}
            .with_context("tpcc.load", "inspect dataset manifest")
    );
}

auto read_manifest(const std::filesystem::path& path) -> base::Result<std::string> {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        return manifest_io_error("could not stat TPC-C cache manifest", path, error);
    }
    if (size == 0U || size > kMaximumManifestBytes) {
        return manifest_error("TPC-C cache manifest must contain 1..65536 bytes");
    }

    std::ifstream input{path, std::ios::binary};
    if (!input.is_open()) {
        return manifest_io_error(
            "could not open TPC-C cache manifest",
            path,
            std::make_error_code(std::errc::io_error)
        );
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (input.gcount() != static_cast<std::streamsize>(text.size()) || input.bad()) {
        return manifest_io_error(
            "could not read complete TPC-C cache manifest",
            path,
            std::make_error_code(std::errc::io_error)
        );
    }
    return text;
}

auto parse_fields(const std::string& text) -> base::Result<ManifestFields> {
    ManifestFields fields;
    std::istringstream lines{text};
    std::string line;
    while (std::getline(lines, line)) {
        const std::size_t separator = line.find('=');
        if (line.empty() || separator == std::string::npos || separator == 0U
            || separator + 1U == line.size()) {
            return manifest_error("TPC-C cache manifest contains a malformed field");
        }
        std::string key = line.substr(0U, separator);
        std::string value = line.substr(separator + 1U);
        if (!fields.emplace(std::move(key), std::move(value)).second) {
            return manifest_error("TPC-C cache manifest contains a duplicate field");
        }
    }
    return fields;
}

auto require_field(const ManifestFields& fields, const std::string_view name)
    -> base::Result<std::string_view> {
    const auto field = fields.find(name);
    if (field == fields.end()) {
        return manifest_error("TPC-C cache manifest omitted " + std::string{name});
    }
    return std::string_view{field->second};
}

auto expect_field(
    const ManifestFields& fields,
    const std::string_view name,
    const std::string& expected
) -> base::Status {
    auto actual = require_field(fields, name);
    if (!actual) {
        return base::fail(std::move(actual).error());
    }
    if (actual.value() != expected) {
        return manifest_error("TPC-C cache manifest mismatch for " + std::string{name});
    }
    return {};
}

auto parse_unsigned(const std::string_view value, const std::string_view label)
    -> base::Result<std::uint64_t> {
    std::uint64_t parsed = 0U;
    const auto result = std::from_chars(value.begin(), value.end(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.end()) {
        return manifest_error("TPC-C cache manifest has invalid " + std::string{label});
    }
    return parsed;
}

auto parse_unsigned_field(const ManifestFields& fields, const std::string_view name)
    -> base::Result<std::uint64_t> {
    auto value = require_field(fields, name);
    if (!value) {
        return base::fail(std::move(value).error());
    }
    return parse_unsigned(value.value(), name);
}

auto validate_scale_fields(const ManifestFields& fields, const DatasetScale& scale)
    -> base::Status {
    const std::array<std::pair<std::string_view, std::uint32_t>, 7> expected{
        std::pair{"warehouses", scale.warehouses},
        std::pair{"districts", scale.districts_per_warehouse},
        std::pair{"customers", scale.customers_per_district},
        std::pair{"orders", scale.orders_per_district},
        std::pair{"first_unprocessed", scale.first_unprocessed_order_id},
        std::pair{"items", scale.items},
        std::pair{"stock", scale.stock_per_warehouse},
    };
    for (const auto& [name, value] : expected) {
        auto matched = expect_field(fields, name, std::to_string(value));
        if (!matched) {
            return matched;
        }
    }
    return {};
}

auto expected_row_counts(const DatasetScale& scale, const std::uint64_t order_line_count)
    -> std::array<std::uint64_t, kLoadTables.size()> {
    const std::uint64_t warehouses = scale.warehouses;
    const std::uint64_t partitions = warehouses * scale.districts_per_warehouse;
    const std::uint64_t customers = partitions * scale.customers_per_district;
    const std::uint64_t orders = partitions * scale.orders_per_district;
    const std::uint64_t new_orders =
        partitions * (scale.orders_per_district - scale.first_unprocessed_order_id + 1U);
    return {
        warehouses,
        partitions,
        customers,
        customers,
        new_orders,
        orders,
        order_line_count,
        scale.items,
        warehouses * scale.stock_per_warehouse,
    };
}

auto validate_order_line_count(const DatasetScale& scale, const std::uint64_t count)
    -> base::Status {
    const std::uint64_t order_count =
        std::uint64_t{scale.warehouses} * scale.districts_per_warehouse * scale.orders_per_district;
    if (count < order_count * 5U || count > order_count * 15U) {
        return manifest_error("TPC-C order_line_count is outside the canonical 5..15 range");
    }
    return {};
}

auto valid_cache_filename(const std::string_view name) noexcept -> bool {
    return !name.empty() && std::ranges::all_of(name, [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return (byte >= static_cast<unsigned char>('a') && byte <= static_cast<unsigned char>('z'))
               || (byte >= static_cast<unsigned char>('A')
                   && byte <= static_cast<unsigned char>('Z'))
               || (byte >= static_cast<unsigned char>('0')
                   && byte <= static_cast<unsigned char>('9'))
               || character == '_' || character == '-' || character == '.';
    });
}

auto valid_load_path(const std::filesystem::path& path) -> bool {
    const std::string text = path.string();
    return !text.empty() && text.front() == '/'
           && std::ranges::all_of(text, [](const char character) {
                  const auto byte = static_cast<unsigned char>(character);
                  return (byte >= static_cast<unsigned char>('a')
                          && byte <= static_cast<unsigned char>('z'))
                         || (byte >= static_cast<unsigned char>('A')
                             && byte <= static_cast<unsigned char>('Z'))
                         || (byte >= static_cast<unsigned char>('0')
                             && byte <= static_cast<unsigned char>('9'))
                         || character == '_' || character == '-' || character == '.'
                         || character == '/';
              });
}

auto parse_file_record(const ManifestFields& fields, const std::string_view table)
    -> base::Result<FileRecord> {
    const std::string field_name = "file." + std::string{table};
    auto encoded = require_field(fields, field_name);
    if (!encoded) {
        return base::fail(std::move(encoded).error());
    }
    const std::size_t separator = encoded->rfind(':');
    if (separator == std::string_view::npos || separator == 0U
        || separator + 1U == encoded->size()) {
        return manifest_error("TPC-C cache manifest has malformed " + field_name);
    }
    const std::string_view name = encoded->substr(0U, separator);
    if (!valid_cache_filename(name)) {
        return manifest_error("TPC-C cache manifest has unsafe filename for " + std::string{table});
    }
    auto size = parse_unsigned(encoded->substr(separator + 1U), field_name + " size");
    if (!size) {
        return base::fail(std::move(size).error());
    }
    if (size.value() == 0U) {
        return manifest_error(
            "TPC-C cache manifest records an empty file for " + std::string{table}
        );
    }
    return FileRecord{.name = name, .size = size.value()};
}

auto validate_file(
    const std::filesystem::path& path,
    const std::uint64_t expected_size,
    const std::string_view table
) -> base::Status {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error) {
        return manifest_io_error("could not stat TPC-C CSV", path, error);
    }
    if (!std::filesystem::is_regular_file(status)) {
        return manifest_error("TPC-C CSV is not a regular file for " + std::string{table});
    }
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        return manifest_io_error("could not read TPC-C CSV size", path, error);
    }
    if (size != expected_size) {
        return manifest_error("TPC-C CSV size does not match manifest for " + std::string{table});
    }
    if (!valid_load_path(path)) {
        return manifest_error(
            "TPC-C CSV absolute path contains bytes unsupported by RMDB LOAD: " + path.string()
        );
    }
    return {};
}

auto resolve_root(const std::filesystem::path& directory) -> base::Result<std::filesystem::path> {
    std::error_code error;
    auto root = std::filesystem::canonical(directory, error);
    if (error) {
        return manifest_io_error("could not resolve TPC-C dataset directory", directory, error);
    }
    const auto status = std::filesystem::status(root, error);
    if (error) {
        return manifest_io_error("could not stat TPC-C dataset directory", root, error);
    }
    if (!std::filesystem::is_directory(status)) {
        return manifest_error("TPC-C dataset path is not a directory");
    }
    return root;
}

auto inspect_load_dataset_impl(const std::filesystem::path& directory, const DatasetScale& scale)
    -> base::Result<LoadDatasetSource> {
    auto root = resolve_root(directory);
    if (!root) {
        return base::fail(std::move(root).error());
    }
    auto manifest_text = read_manifest(*root / "cache.manifest");
    if (!manifest_text) {
        return base::fail(std::move(manifest_text).error());
    }
    auto fields = parse_fields(manifest_text.value());
    if (!fields) {
        return base::fail(std::move(fields).error());
    }
    auto version = expect_field(*fields, "version", std::string{kDatasetContractVersion});
    if (!version) {
        return base::fail(std::move(version).error());
    }
    auto scale_status = validate_scale_fields(*fields, scale);
    if (!scale_status) {
        return base::fail(std::move(scale_status).error());
    }
    auto order_line_count = parse_unsigned_field(*fields, "order_line_count");
    if (!order_line_count) {
        return base::fail(std::move(order_line_count).error());
    }
    auto valid_order_lines = validate_order_line_count(scale, order_line_count.value());
    if (!valid_order_lines) {
        return base::fail(std::move(valid_order_lines).error());
    }
    const auto row_counts = expected_row_counts(scale, order_line_count.value());

    LoadDatasetSource source{.root = std::move(root).value(), .tables = {}};
    std::set<std::string> filenames;
    for (std::size_t index = 0U; index < kLoadTables.size(); ++index) {
        const auto& contract = array_element(kLoadTables, index);
        auto record = parse_file_record(*fields, contract.name);
        if (!record) {
            return base::fail(std::move(record).error());
        }
        if (!filenames.emplace(record->name).second) {
            return manifest_error("TPC-C cache manifest maps multiple tables to one CSV");
        }
        std::filesystem::path path = source.root / record->name;
        auto valid_file = validate_file(path, record->size, contract.name);
        if (!valid_file) {
            return base::fail(std::move(valid_file).error());
        }
        array_element(source.tables, index) = LoadTableSource{
            .table = contract.name,
            .csv_path = std::move(path),
            .row_count = array_element(row_counts, index),
        };
    }
    return source;
}

}  // namespace

auto LoadDatasetSource::total_rows() const noexcept -> std::uint64_t {
    std::uint64_t total = 0U;
    for (const LoadTableSource& table : tables) {
        total += table.row_count;
    }
    return total;
}

auto inspect_load_dataset(const std::filesystem::path& directory, const DatasetScale& scale)
    -> base::Result<LoadDatasetSource> {
    return base::invoke_exception_boundary(kLoadManifestBoundary, "inspect dataset", [&] {
        return inspect_load_dataset_impl(directory, scale);
    });
}

}  // namespace db::tpcc
