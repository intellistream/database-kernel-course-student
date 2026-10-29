#include <type_traits>
#include <vector>

#include "db/base/bytes.hpp"

static_assert(std::is_constructible_v<db::base::LittleEndianReader, db::base::Bytes>);
static_assert(std::is_constructible_v<db::base::BigEndianWriter, db::base::MutableBytes>);
static_assert(!std::is_copy_constructible_v<db::base::LittleEndianAppender>);
static_assert(!std::is_move_constructible_v<db::base::BigEndianAppender>);

[[maybe_unused]] void byte_appenders_borrow_dynamic_storage() {
    std::vector<std::byte> bytes;
    db::base::LittleEndianAppender little_endian{bytes};
    little_endian.append_u32(7U);
}
