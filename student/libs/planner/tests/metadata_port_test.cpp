#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/planner/metadata_port.hpp"
#include "db/types/data_type.hpp"

namespace db::planner {
namespace {

class FakeMetadataPort final : public MetadataPort {
public:
    FakeMetadataPort(PlanningTable table, PlanningIndex index)
        : table_{std::move(table)},
          index_{std::move(index)} {}

    auto generation() const noexcept -> std::uint64_t override {
        return 12U;
    }

    auto lookup_table(const base::Oid oid) const
        -> base::Result<std::optional<PlanningTable>> override {
        if (oid == table_.oid) {
            return std::optional<PlanningTable>{table_};
        }
        return std::optional<PlanningTable>{};
    }

    auto indexes_for_table(const base::Oid table_oid) const
        -> base::Result<std::vector<PlanningIndex>> override {
        return table_oid == table_.oid ? std::vector<PlanningIndex>{index_}
                                       : std::vector<PlanningIndex>{};
    }

private:
    PlanningTable table_;
    PlanningIndex index_;
};

TEST(MetadataPortTest, ExposesOnlyStableTableAndPerTableIndexQueries) {
    const PlanningTable table{
        .oid = base::Oid{3U},
        .columns = {{.name = "id", .data_type = types::kInt64Type}},
    };
    const PlanningIndex index{
        .oid = base::Oid{5U},
        .name = "items_id_idx",
        .column_names = {"id"},
        .unique = true,
    };
    FakeMetadataPort fake{table, index};
    const MetadataPort& port = fake;

    EXPECT_EQ(port.generation(), 12U);
    EXPECT_EQ(port.lookup_table(base::Oid{3U}).value(), table);
    EXPECT_EQ(port.indexes_for_table(base::Oid{3U}).value(), std::vector{index});
    EXPECT_TRUE(port.indexes_for_table(base::Oid{999U}).value().empty());
}

}  // namespace
}  // namespace db::planner
