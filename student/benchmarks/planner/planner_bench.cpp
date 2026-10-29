#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/planner/join_order.hpp"
#include "db/planner/lowering.hpp"
#include "db/planner/metadata_port.hpp"
#include "db/planner/path.hpp"
#include "db/planner/preprocess.hpp"
#include "db/types/data_type.hpp"
#include "db/types/operator.hpp"
#include "db/types/value.hpp"

namespace db::benchmarks {
namespace {

class EmptyMetadataPort final : public planner::MetadataPort {
public:
    auto generation() const noexcept -> std::uint64_t override {
        return 1U;
    }

    auto lookup_table(const base::Oid /*oid*/) const
        -> base::Result<std::optional<planner::PlanningTable>> override {
        return std::optional<planner::PlanningTable>{};
    }

    auto indexes_for_table(const base::Oid /*table_oid*/) const
        -> base::Result<std::vector<planner::PlanningIndex>> override {
        return std::vector<planner::PlanningIndex>{};
    }
};

class WideMetadataPort final : public planner::MetadataPort {
public:
    explicit WideMetadataPort(const std::size_t index_count) {
        table_.oid = base::Oid{71U};
        table_.columns.reserve(64U);
        for (std::size_t index = 0U; index < 64U; ++index) {
            table_.columns.push_back(
                planner::PlanningColumn{
                    .name = "c" + std::to_string(index),
                    .data_type = types::kInt64Type,
                }
            );
        }
        indexes_.reserve(index_count);
        for (std::size_t index = 0U; index < index_count; ++index) {
            indexes_.push_back(
                planner::PlanningIndex{
                    .oid = base::Oid{static_cast<std::uint32_t>(100U + index)},
                    .name = "wide_idx_" + std::to_string(index),
                    .column_names = {"c" + std::to_string(63U - (index % 16U))},
                    .unique = index == 0U,
                }
            );
        }
    }

    auto generation() const noexcept -> std::uint64_t override {
        return 7U;
    }

    auto lookup_table(const base::Oid oid) const
        -> base::Result<std::optional<planner::PlanningTable>> override {
        if (oid == table_.oid) {
            return std::optional<planner::PlanningTable>{table_};
        }
        return std::optional<planner::PlanningTable>{};
    }

    auto indexes_for_table(const base::Oid table_oid) const
        -> base::Result<std::vector<planner::PlanningIndex>> override {
        if (table_oid == table_.oid) {
            return indexes_;
        }
        return std::vector<planner::PlanningIndex>{};
    }

private:
    planner::PlanningTable table_;
    std::vector<planner::PlanningIndex> indexes_;
};

auto integer_literal(const std::int64_t value) -> planner::PreprocessedExpressionPtr {
    return std::make_unique<planner::PreprocessedExpression>(
        sql::SourceSpan{},
        types::kInt64Type,
        planner::QueryLiteralExpression{.value = types::Value::integer(value)}
    );
}

auto constant_query(const std::size_t column_count) -> planner::PreprocessedQuery {
    planner::PreprocessedSelectQuery select;
    select.projection.reserve(column_count);
    for (std::size_t index = 0U; index < column_count; ++index) {
        select.projection.push_back(
            planner::PreprocessedProjectionItem{
                .expression = integer_literal(static_cast<std::int64_t>(index)),
                .alias = "column_" + std::to_string(index),
            }
        );
    }
    return planner::PreprocessedQuery{sql::SourceSpan{}, std::move(select)};
}

auto wide_base_table() -> planner::PreprocessedBaseTable {
    planner::PreprocessedBaseTable table{
        .oid = base::Oid{71U},
        .name = "wide_items",
        .alias = std::nullopt,
        .columns = {},
        .range_table_index = planner::QueryRangeTableIndex{0U},
    };
    table.columns.reserve(64U);
    for (std::size_t index = 0U; index < 64U; ++index) {
        table.columns.push_back(
            planner::QueryColumn{
                .name = "c" + std::to_string(index),
                .data_type = types::kInt64Type,
            }
        );
    }
    return table;
}

auto wide_filter() -> planner::PreprocessedExpression {
    auto column = std::make_unique<planner::PreprocessedExpression>(
        sql::SourceSpan{},
        types::kInt64Type,
        planner::QueryColumnReference{
            .range_table_index = planner::QueryRangeTableIndex{0U},
            .column_index = planner::QueryColumnIndex{63U},
            .data_type = types::kInt64Type,
            .outer_query_levels = 0U,
        }
    );
    return planner::PreprocessedExpression{
        sql::SourceSpan{},
        types::kBoolType,
        planner::QueryBinaryExpression{
            .op = types::BinaryOperator::kEqual,
            .left = std::move(column),
            .right = integer_literal(42),
        },
    };
}

auto empty_parameters() -> base::Result<planner::ParameterNamespace> {
    return planner::ParameterNamespace::from_external_slots({});
}

auto join_relations(const std::size_t relation_count)
    -> std::vector<planner::BaseRelationPathList> {
    std::vector<planner::BaseRelationPathList> relations;
    relations.reserve(relation_count);
    for (std::size_t index = 0U; index < relation_count; ++index) {
        planner::PlanSchema schema{
            .columns = {planner::PlanOutputColumn{
                .name = "id",
                .data_type = types::kInt64Type,
                .origin = std::nullopt,
            }},
        };
        relations.push_back(
            planner::BaseRelationPathList{
                .catalog_generation = 11U,
                .range_table_index =
                    planner::QueryRangeTableIndex{static_cast<std::uint32_t>(index)},
                .relation =
                    planner::PlanRelation{
                        .oid = base::Oid{static_cast<std::uint32_t>(200U + index)},
                        .name = "relation_" + std::to_string(index),
                        .alias = std::nullopt,
                        .schema = std::move(schema),
                    },
                .paths = {planner::SequentialScanPath{}},
                .selected_path_index = 0U,
            }
        );
    }
    return relations;
}

auto chain_qualifications(const std::size_t relation_count)
    -> std::vector<planner::JoinQualification> {
    std::vector<planner::JoinQualification> qualifications;
    qualifications.reserve(relation_count - 1U);
    for (std::size_t index = 0U; index + 1U < relation_count; ++index) {
        qualifications.push_back(
            planner::JoinQualification{
                .source =
                    planner::JoinQualificationSource{
                        .source_clause_index = index,
                        .conjunct_index = 0U,
                    },
                .required_relations = {
                    planner::QueryRangeTableIndex{static_cast<std::uint32_t>(index)},
                    planner::QueryRangeTableIndex{static_cast<std::uint32_t>(index + 1U)}
                },
            }
        );
    }
    return qualifications;
}

void lower_constant_select(benchmark::State& state) {
    EmptyMetadataPort metadata;
    const auto column_count = static_cast<std::size_t>(state.range(0));
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto parameters = empty_parameters();
        if (!parameters) {
            state.SkipWithError(parameters.error().to_string());
            return;
        }
        auto lowered = planner::lower_preprocessed_select(
            constant_query(column_count),
            metadata,
            std::move(parameters).value()
        );
        if (!lowered) {
            state.SkipWithError(lowered.error().to_string());
            return;
        }
        benchmark::DoNotOptimize(&lowered.value());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(column_count));
}

void lower_and_assemble_constant_select(benchmark::State& state) {
    EmptyMetadataPort metadata;
    const auto column_count = static_cast<std::size_t>(state.range(0));
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto parameters = empty_parameters();
        if (!parameters) {
            state.SkipWithError(parameters.error().to_string());
            return;
        }
        auto lowered = planner::lower_preprocessed_select(
            constant_query(column_count),
            metadata,
            std::move(parameters).value()
        );
        if (!lowered) {
            state.SkipWithError(lowered.error().to_string());
            return;
        }
        auto planned = planner::assemble_planned_select(std::move(lowered).value());
        if (!planned) {
            state.SkipWithError(planned.error().to_string());
            return;
        }
        benchmark::DoNotOptimize(&planned.value());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(column_count));
}

void build_wide_relation_paths(benchmark::State& state) {
    const auto index_count = static_cast<std::size_t>(state.range(0));
    WideMetadataPort metadata{index_count};
    const planner::PreprocessedBaseTable table = wide_base_table();
    const planner::PreprocessedExpression filter = wide_filter();

    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto paths = planner::build_base_relation_paths(metadata, table, &filter);
        if (!paths) {
            state.SkipWithError(paths.error().to_string());
            return;
        }
        benchmark::DoNotOptimize(paths->paths.data());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(index_count + 1U));
}

void choose_join_order(benchmark::State& state) {
    const auto relation_count = static_cast<std::size_t>(state.range(0));
    const auto relations = join_relations(relation_count);
    const auto qualifications = chain_qualifications(relation_count);
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto order = planner::choose_small_inner_join_order(relations, qualifications);
        if (!order) {
            state.SkipWithError(order.error().to_string());
            return;
        }
        benchmark::DoNotOptimize(order->steps.data());
    }
    state.SetItemsProcessed(state.iterations());
}

// Google Benchmark owns the process-lifetime registration object.
// NOLINTBEGIN(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)
BENCHMARK(lower_constant_select)
    ->Name("planner/lower_constant_select")
    ->ArgName("columns")
    ->Arg(8)
    ->Arg(64);
BENCHMARK(lower_and_assemble_constant_select)
    ->Name("planner/lower_and_assemble_constant_select")
    ->ArgName("columns")
    ->Arg(8)
    ->Arg(64);
BENCHMARK(build_wide_relation_paths)
    ->Name("planner/build_wide_relation_paths")
    ->ArgName("indexes")
    ->Arg(1)
    ->Arg(8)
    ->Arg(32);
BENCHMARK(choose_join_order)
    ->Name("planner/choose_join_order")
    ->ArgName("relations")
    ->Arg(3)
    ->Arg(6);
// NOLINTEND(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)

}  // namespace
}  // namespace db::benchmarks
