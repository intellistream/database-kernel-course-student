#include "db/base/crc32c.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include "db/base/error.hpp"

namespace db::base {
namespace {

inline constexpr std::uint32_t kPolynomial = 0x82F63B78U;

consteval auto make_table() -> std::array<std::uint32_t, 256> {
    std::array<std::uint32_t, 256> table{};
    for (std::size_t index = 0; index < table.size(); ++index) {
        auto value = static_cast<std::uint32_t>(index);
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const auto mask = 0U - (value & 1U);
            value = (value >> 1U) ^ (kPolynomial & mask);
        }
        table.at(index) = value;
    }
    return table;
}

inline constexpr auto kTable = make_table();

void update_software(std::uint32_t& checksum, const Bytes bytes) noexcept {
    for (const std::byte byte : bytes) {
        const auto index = static_cast<std::size_t>(
            static_cast<std::uint8_t>(checksum) ^ std::to_integer<std::uint8_t>(byte)
        );
        checksum = kTable.at(index) ^ (checksum >> 8U);
    }
}

using UpdateFunction = void (*)(std::uint32_t&, Bytes) noexcept;

#if defined(__x86_64__) && defined(__GNUC__)
[[gnu::target("sse4.2")]] void update_sse42(std::uint32_t& checksum, const Bytes bytes) noexcept {
    std::size_t offset = 0U;
    std::uint64_t accumulator = checksum;
    while (bytes.size() - offset >= sizeof(std::uint64_t)) {
        std::uint64_t word = 0U;
        std::memcpy(&word, bytes.subspan(offset, sizeof(word)).data(), sizeof(word));
        accumulator = __builtin_ia32_crc32di(accumulator, word);
        offset += sizeof(word);
    }
    checksum = static_cast<std::uint32_t>(accumulator);
    for (const std::byte byte : bytes.subspan(offset)) {
        checksum = __builtin_ia32_crc32qi(checksum, std::to_integer<std::uint8_t>(byte));
    }
}
#endif

auto select_update() noexcept -> UpdateFunction {
#if defined(__x86_64__) && defined(__GNUC__)
    __builtin_cpu_init();
    const bool sse42_available = __builtin_cpu_supports("sse4.2");
    if (sse42_available) {
        return update_sse42;
    }
#endif
    return update_software;
}

auto update_function() noexcept -> UpdateFunction {
    static const UpdateFunction kFunction = select_update();
    return kFunction;
}

void update_zeroes(
    std::uint32_t& checksum,
    std::size_t length,
    const UpdateFunction update
) noexcept {
    constexpr std::array<std::byte, 16> zeroes{};
    std::size_t offset = 0U;
    while (offset < length) {
        const auto chunk = std::min(length - offset, zeroes.size());
        update(checksum, Bytes{zeroes}.first(chunk));
        offset += chunk;
    }
}

auto invalid_range(const std::string& operation) -> Error {
    return Error{
        ErrorCode::kInvalidArgument,
        "CRC-32C field lies outside the protected bytes",
    }
        .with_context("base.crc32c", operation);
}

}  // namespace

auto crc32c(const Bytes bytes) noexcept -> std::uint32_t {
    auto checksum = std::numeric_limits<std::uint32_t>::max();
    update_function()(checksum, bytes);
    return ~checksum;
}

auto crc32c_with_zeroed(const Bytes bytes, const std::size_t offset, const std::size_t length)
    -> Result<std::uint32_t> {
    if (offset > bytes.size() || length > bytes.size() - offset) {
        return fail(invalid_range("compute with zeroed field"));
    }
    auto checksum = std::numeric_limits<std::uint32_t>::max();
    const auto update = update_function();
    update(checksum, bytes.first(offset));
    update_zeroes(checksum, length, update);
    update(checksum, bytes.subspan(offset + length));
    return ~checksum;
}

auto seal_crc32c(const MutableBytes bytes, const std::size_t checksum_offset) -> Status {
    if (checksum_offset > bytes.size() || kCrc32cSize > bytes.size() - checksum_offset) {
        return fail(invalid_range("seal"));
    }
    std::fill_n(
        bytes.begin() + static_cast<std::ptrdiff_t>(checksum_offset),
        kCrc32cSize,
        std::byte{0}
    );
    const auto checksum = crc32c(Bytes{bytes});
    LittleEndianWriter writer{bytes.subspan(checksum_offset, kCrc32cSize)};
    auto status = writer.write_u32(checksum);
    if (!status) {
        return fail(std::move(status).error().with_context("base.crc32c", "seal"));
    }
    return {};
}

auto verify_crc32c(const Bytes bytes, const std::size_t checksum_offset) -> Status {
    if (checksum_offset > bytes.size() || kCrc32cSize > bytes.size() - checksum_offset) {
        return fail(invalid_range("verify"));
    }
    LittleEndianReader reader{bytes.subspan(checksum_offset, kCrc32cSize)};
    auto stored = reader.read_u32();
    auto computed = crc32c_with_zeroed(bytes, checksum_offset, kCrc32cSize);
    if (!stored || !computed) {
        return fail(
            Error{
                ErrorCode::kCorruption,
                "could not read CRC-32C field",
            }
                .with_context("base.crc32c", "verify")
        );
    }
    if (stored.value() != computed.value()) {
        return fail(
            Error{
                ErrorCode::kCorruption,
                "CRC-32C mismatch",
            }
                .with_context("base.crc32c", "verify")
        );
    }
    return {};
}

}  // namespace db::base
