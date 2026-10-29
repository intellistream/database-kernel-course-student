#include "db/catalog/analyze.hpp"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/catalog/statistics.hpp"
#include "db/types/codec.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::catalog {
namespace {

constexpr std::uint64_t kRandomSeedMixer = 0x9e37'79b9'7f4a'7c15ULL;
constexpr std::uint64_t kRandomMultiplier = 6'364'136'223'846'793'005ULL;
constexpr std::uint64_t kRandomIncrement = 1'442'695'040'888'963'407ULL;

[[maybe_unused]] auto value_width(const types::Value& value) noexcept -> std::size_t {
    switch (value.kind()) {
        case types::ValueKind::kNull:
            return 0U;
        case types::ValueKind::kBoolean:
            return 1U;
        case types::ValueKind::kInteger:
            return sizeof(std::int64_t);
        case types::ValueKind::kFloat:
            return sizeof(float);
        case types::ValueKind::kString:
            return value.as_string() == nullptr ? 0U : value.as_string()->size();
        case types::ValueKind::kTimestamp:
            return sizeof(std::int64_t);
    }
    return 0U;
}

[[maybe_unused]] auto calculate_column_statistics(
    [[maybe_unused]] const std::span<const std::vector<types::Value>> sample,
    [[maybe_unused]] const std::size_t column,
    [[maybe_unused]] const types::DataType data_type
) -> base::Result<ColumnStatistics> {
    // LAB-S04.Q07:R01-BEGIN
    base::lab::missing_algorithm("S04.Q07:R01");
    // LAB-S04.Q07:R01-END
}
}  // namespace

[[maybe_unused]] auto AnalyzeAccumulator::create(
    [[maybe_unused]] const base::Oid table_oid,
    [[maybe_unused]] Schema schema,
    [[maybe_unused]] const std::size_t sample_target
) -> base::Result<AnalyzeAccumulator> {
    // LAB-S04.Q07:R02-BEGIN
    base::lab::missing_algorithm("S04.Q07:R02");
    // LAB-S04.Q07:R02-END
}

[[maybe_unused]] AnalyzeAccumulator::AnalyzeAccumulator(
    [[maybe_unused]] const base::Oid table_oid,
    [[maybe_unused]] Schema schema,
    [[maybe_unused]] const std::size_t sample_target
)
    : table_oid_{table_oid},
      schema_{std::move(schema)},
      sample_target_{sample_target},
      random_state_{static_cast<std::uint64_t>(table_oid.value()) ^ kRandomSeedMixer} {
    // LAB-S04.Q07:R03-BEGIN
    base::lab::missing_algorithm("S04.Q07:R03");
    // LAB-S04.Q07:R03-END
}

[[maybe_unused]] auto AnalyzeAccumulator::observe(
    [[maybe_unused]] const std::span<const types::Value> row,
    [[maybe_unused]] const std::size_t encoded_row_width
) -> base::Status {
    // LAB-S04.Q07:R04-BEGIN
    base::lab::missing_algorithm("S04.Q07:R04");
    // LAB-S04.Q07:R04-END
}

[[maybe_unused]] auto
AnalyzeAccumulator::finish([[maybe_unused]] const AnalyzeCompletion completion) const
    -> base::Result<TableStatistics> {
    // LAB-S04.Q07:R05-BEGIN
    base::lab::missing_algorithm("S04.Q07:R05");
    // LAB-S04.Q07:R05-END
}

auto AnalyzeAccumulator::live_rows() const noexcept -> std::uint64_t {
    return live_rows_;
}

auto AnalyzeAccumulator::sample_rows() const noexcept -> std::size_t {
    return sample_.size();
}

[[maybe_unused]] auto AnalyzeAccumulator::next_random() noexcept -> std::uint64_t {
    // LAB-S04.Q07:R06-BEGIN
    base::lab::missing_algorithm("S04.Q07:R06");
    // LAB-S04.Q07:R06-END
}
}  // namespace db::catalog
