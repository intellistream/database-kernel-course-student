#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"
#include "db/wal/segment_storage.hpp"

namespace db::wal::reader_detail {

class ReadAhead final {
public:
    explicit ReadAhead(std::size_t capacity);

    auto read_exact(
        const WalSegmentStorage& storage,
        std::uint64_t file_end,
        std::uint64_t offset,
        base::MutableBytes output
    ) -> base::Status;
    auto fills() const noexcept -> std::uint64_t;

private:
    std::vector<std::byte> bytes_;
    std::uint64_t start_{0};
    std::size_t valid_length_{0};
    std::uint64_t fills_{0};
};

}  // namespace db::wal::reader_detail
