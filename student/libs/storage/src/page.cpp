#include "db/storage/page.hpp"

#include <cstddef>
#include <cstdint>

namespace db::storage {
namespace {

constexpr std::size_t kFlagsOffset = 12U;
constexpr std::size_t kLsnOffset = 24U;

auto load_u16_le(const base::Bytes bytes, const std::size_t offset) noexcept -> std::uint16_t {
    const auto low = std::to_integer<std::uint16_t>(bytes[offset]);
    const auto high = std::to_integer<std::uint16_t>(bytes[offset + 1U]);
    return static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8U));
}

auto load_u32_le(const base::Bytes bytes, const std::size_t offset) noexcept -> std::uint32_t {
    const auto first = std::to_integer<std::uint32_t>(bytes[offset]);
    const auto second = std::to_integer<std::uint32_t>(bytes[offset + 1U]);
    const auto third = std::to_integer<std::uint32_t>(bytes[offset + 2U]);
    const auto fourth = std::to_integer<std::uint32_t>(bytes[offset + 3U]);
    return first | (second << 8U) | (third << 16U) | (fourth << 24U);
}

auto load_u64_le(const base::Bytes bytes, const std::size_t offset) noexcept -> std::uint64_t {
    const auto low = static_cast<std::uint64_t>(load_u32_le(bytes, offset));
    const auto high = static_cast<std::uint64_t>(load_u32_le(bytes, offset + 4U));
    return low | (high << 32U);
}

void store_u16_le(
    const base::MutableBytes bytes,
    const std::size_t offset,
    const std::uint16_t value
) noexcept {
    bytes[offset] = static_cast<std::byte>(static_cast<std::uint8_t>(value));
    bytes[offset + 1U] = static_cast<std::byte>(static_cast<std::uint8_t>(value >> 8U));
}

void store_u32_le(
    const base::MutableBytes bytes,
    const std::size_t offset,
    const std::uint32_t value
) noexcept {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(static_cast<std::uint8_t>(value >> (index * 8U)));
    }
}

void store_u64_le(
    const base::MutableBytes bytes,
    const std::size_t offset,
    const std::uint64_t value
) noexcept {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(static_cast<std::uint8_t>(value >> (index * 8U)));
    }
}

}  // namespace

auto PageHeader::encode(const base::MutableBytes output) const -> base::Status {
    store_u32_le(output, 0U, magic);
    store_u16_le(output, 4U, version);
    store_u16_le(output, 6U, header_size);
    store_u32_le(output, 8U, checksum);
    store_u16_le(output, 12U, flags);
    store_u16_le(output, 14U, static_cast<std::uint16_t>(kind));
    store_u16_le(output, 16U, lower);
    store_u16_le(output, 18U, upper);
    store_u16_le(output, 20U, special);
    store_u16_le(output, 22U, reserved);
    store_u64_le(output, 24U, lsn.value());
    return {};
}

auto PageHeader::decode(const base::Bytes input) -> base::Result<PageHeader> {
    return PageHeader{
        .magic = load_u32_le(input, 0U),
        .version = load_u16_le(input, 4U),
        .header_size = load_u16_le(input, 6U),
        .checksum = load_u32_le(input, 8U),
        .flags = load_u16_le(input, 12U),
        .kind = static_cast<PageKind>(load_u16_le(input, 14U)),
        .lower = load_u16_le(input, 16U),
        .upper = load_u16_le(input, 18U),
        .special = load_u16_le(input, 20U),
        .reserved = load_u16_le(input, 22U),
        .lsn = base::Lsn{load_u64_le(input, 24U)},
    };
}

auto Page::create(const PageKind kind, const PageAddress address, const std::size_t special_size)
    -> base::Result<Page> {
    static_cast<void>(address);
    Storage bytes{};
    const auto special = static_cast<std::uint16_t>(kPageSize - special_size);
    const PageHeader header{
        .kind = kind,
        .lower = kPageHeaderSize,
        .upper = special,
        .special = special,
    };
    static_cast<void>(header.encode(base::MutableBytes{bytes}.first(kPageHeaderSize)));
    return Page{bytes};
}

auto Page::load(const Storage& bytes, const PageAddress address) -> base::Result<Page> {
    static_cast<void>(address);
    return Page{bytes};
}

Page::Page(const Storage& bytes) noexcept
    : bytes_{bytes} {}

auto Page::bytes() const noexcept -> const Page::Storage& {
    return bytes_;
}

auto Page::view() const noexcept -> PageView {
    return PageView::from_storage(bytes_);
}

auto Page::mutable_view() noexcept -> MutablePageView {
    return MutablePageView::from_storage(bytes_);
}

auto Page::header() const -> base::Result<PageHeader> {
    return view().header();
}

auto Page::lsn() const noexcept -> base::Lsn {
    return view().lsn();
}

auto Page::set_lsn(const base::Lsn lsn) -> base::Status {
    mutable_view().set_lsn(lsn);
    return {};
}

auto Page::flags() const -> base::Result<std::uint16_t> {
    return view().flags();
}

auto Page::set_flags(const std::uint16_t flags_value) -> base::Status {
    mutable_view().set_flags(flags_value);
    return {};
}

auto Page::view(const std::size_t offset, const std::size_t length) const
    -> base::Result<base::Bytes> {
    return view().view(offset, length);
}

auto Page::mutable_view(const std::size_t offset, const std::size_t length)
    -> base::Result<base::MutableBytes> {
    return mutable_view().mutable_view(offset, length);
}

auto Page::free_space_view() const -> base::Result<base::Bytes> {
    return view().free_space_view();
}

auto Page::special_view() const -> base::Result<base::Bytes> {
    return view().special_view();
}

auto Page::mutable_special_view() -> base::Result<base::MutableBytes> {
    return mutable_view().mutable_special_view();
}

PageView::PageView(const base::Bytes bytes) noexcept
    : bytes_{bytes} {}

auto PageView::from_storage(const Page::Storage& storage) noexcept -> PageView {
    return PageView{base::Bytes{storage}};
}

auto PageView::bytes() const noexcept -> base::Bytes {
    return bytes_;
}

auto PageView::header() const -> base::Result<PageHeader> {
    return PageHeader::decode(bytes_.first(kPageHeaderSize));
}

auto PageView::lsn() const noexcept -> base::Lsn {
    return base::Lsn{load_u64_le(bytes_, kLsnOffset)};
}

auto PageView::flags() const noexcept -> std::uint16_t {
    return load_u16_le(bytes_, kFlagsOffset);
}

auto PageView::view(const std::size_t offset, const std::size_t length) const
    -> base::Result<base::Bytes> {
    return bytes_.subspan(offset, length);
}

auto PageView::free_space_view() const -> base::Result<base::Bytes> {
    const PageHeader header_value = header().value();
    return bytes_.subspan(header_value.lower, header_value.upper - header_value.lower);
}

auto PageView::special_view() const -> base::Result<base::Bytes> {
    const std::size_t special = header().value().special;
    return bytes_.subspan(special, kPageSize - special);
}

MutablePageView::MutablePageView(const base::MutableBytes bytes) noexcept
    : bytes_{bytes} {}

auto MutablePageView::from_storage(Page::Storage& storage) noexcept -> MutablePageView {
    return MutablePageView{base::MutableBytes{storage}};
}

auto MutablePageView::read_only() const noexcept -> PageView {
    return PageView{base::Bytes{bytes_}};
}

auto MutablePageView::bytes() const noexcept -> base::Bytes {
    return base::Bytes{bytes_};
}

auto MutablePageView::bytes() noexcept -> base::MutableBytes {
    return bytes_;
}

auto MutablePageView::header() const -> base::Result<PageHeader> {
    return read_only().header();
}

auto MutablePageView::lsn() const noexcept -> base::Lsn {
    return read_only().lsn();
}

auto MutablePageView::flags() const noexcept -> std::uint16_t {
    return read_only().flags();
}

void MutablePageView::set_lsn(const base::Lsn lsn) noexcept {
    store_u64_le(bytes_, kLsnOffset, lsn.value());
}

void MutablePageView::set_flags(const std::uint16_t flags_value) noexcept {
    store_u16_le(bytes_, kFlagsOffset, flags_value);
}

auto MutablePageView::view(const std::size_t offset, const std::size_t length) const
    -> base::Result<base::Bytes> {
    return read_only().view(offset, length);
}

auto MutablePageView::mutable_view(const std::size_t offset, const std::size_t length)
    -> base::Result<base::MutableBytes> {
    return bytes_.subspan(offset, length);
}

auto MutablePageView::free_space_view() const -> base::Result<base::Bytes> {
    return read_only().free_space_view();
}

auto MutablePageView::special_view() const -> base::Result<base::Bytes> {
    return read_only().special_view();
}

auto MutablePageView::mutable_special_view() -> base::Result<base::MutableBytes> {
    const std::size_t special = header().value().special;
    return bytes_.subspan(special, kPageSize - special);
}

}  // namespace db::storage
