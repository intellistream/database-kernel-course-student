#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/sql/catalog_port.hpp"
#include "db/types/data_type.hpp"

namespace db::sql {
namespace {

class FakeCatalogReadPort final : public CatalogReadPort {
public:
    FakeCatalogReadPort(const std::uint64_t generation, CatalogTable table)
        : generation_{generation},
          table_{std::move(table)} {}

    auto generation() const noexcept -> std::uint64_t override {
        return generation_;
    }

    auto lookup_table(const std::string_view name) const
        -> base::Result<std::optional<CatalogTable>> override {
        if (name == table_.name) {
            return std::optional<CatalogTable>{table_};
        }
        return std::optional<CatalogTable>{};
    }

private:
    std::uint64_t generation_;
    CatalogTable table_;
};

TEST(CatalogReadPortTest, ExposesOnlyOwnedBinderLookupsAtOneGeneration) {
    const CatalogTable table{
        .oid = base::Oid{5U},
        .name = "items",
        .columns = {{.name = "id", .data_type = types::kInt64Type}},
    };
    FakeCatalogReadPort fake{9U, table};
    const CatalogReadPort& port = fake;

    EXPECT_EQ(port.generation(), 9U);
    EXPECT_EQ(port.lookup_table("items").value(), table);
    EXPECT_FALSE(port.lookup_table("missing").value().has_value());
}

}  // namespace
}  // namespace db::sql
