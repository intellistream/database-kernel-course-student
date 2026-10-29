#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/test_support/test_directory.hpp"

#include "array_access.hpp"
#include "load.hpp"
#include "load_contract.hpp"

namespace db::tpcc {
namespace {

class TpccLoadManifestTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto directory = test_support::TestDirectory::create("tpcc-load-manifest");
        ASSERT_TRUE(directory) << directory.error().to_string();
        directory_.emplace(std::move(directory).value());
        write_csv_files();
        write_manifest(scale_.warehouses);
    }

    void write_csv_files() {
        for (std::size_t index = 0U; index < kLoadTables.size(); ++index) {
            std::ofstream output{csv_path(index), std::ios::binary | std::ios::trunc};
            ASSERT_TRUE(output.is_open());
            output << "opaque_header\n";
            for (std::uint64_t row = 0U; row < array_element(row_counts_, index); ++row) {
                output << "x\n";
            }
            ASSERT_TRUE(output.good());
        }
    }

    void write_manifest(const std::uint32_t warehouses) {
        std::ostringstream text;
        text << "version=tpcc-5.11-f32-nonull-2025-01-01-v1\n"
             << "key=test-key\n"
             << "dataset_seed=1\n"
             << "identifier_fingerprint=test\n"
             << "warehouses=" << warehouses << '\n'
             << "districts=" << scale_.districts_per_warehouse << '\n'
             << "customers=" << scale_.customers_per_district << '\n'
             << "orders=" << scale_.orders_per_district << '\n'
             << "first_unprocessed=" << scale_.first_unprocessed_order_id << '\n'
             << "items=" << scale_.items << '\n'
             << "stock=" << scale_.stock_per_warehouse << '\n'
             << "order_line_count=" << row_counts_[6U] << '\n'
             << "original_item_count=1\n"
             << "original_stock_count=1\n";
        for (std::size_t index = 0U; index < kLoadTables.size(); ++index) {
            std::error_code error;
            const auto size = std::filesystem::file_size(csv_path(index), error);
            ASSERT_FALSE(error);
            text << "file." << array_element(kLoadTables, index).name << '=' << csv_name(index)
                 << ':' << size << '\n';
        }
        std::ofstream output{directory_->join("cache.manifest"), std::ios::trunc};
        ASSERT_TRUE(output.is_open());
        output << text.str();
        ASSERT_TRUE(output.good());
    }

    auto csv_name(const std::size_t index) const -> std::string {
        return std::string{array_element(kLoadTables, index).name} + ".csv";
    }

    auto csv_path(const std::size_t index) const -> std::filesystem::path {
        return directory_->join(csv_name(index));
    }

    DatasetScale scale_{
        .warehouses = 1U,
        .districts_per_warehouse = 1U,
        .customers_per_district = 2U,
        .orders_per_district = 2U,
        .first_unprocessed_order_id = 2U,
        .items = 3U,
        .stock_per_warehouse = 3U,
    };
    std::array<std::uint64_t, 9> row_counts_{1U, 1U, 2U, 2U, 1U, 2U, 10U, 3U, 3U};
    std::optional<test_support::TestDirectory> directory_;
};

TEST_F(TpccLoadManifestTest, AcceptsACompleteMappedDatasetBeforeSchemaMutation) {
    auto source = inspect_load_dataset(directory_->path(), scale_);
    ASSERT_TRUE(source) << source.error().to_string();
    EXPECT_EQ(source->tables.size(), kLoadTables.size());
    EXPECT_EQ(source->total_rows(), 25U);
    for (std::size_t index = 0U; index < source->tables.size(); ++index) {
        const auto& table = array_element(source->tables, index);
        EXPECT_EQ(table.table, array_element(kLoadTables, index).name);
        EXPECT_EQ(table.row_count, array_element(row_counts_, index));
        EXPECT_TRUE(table.csv_path.is_absolute());
    }
}

TEST_F(TpccLoadManifestTest, RejectsAScaleMismatchAtTheManifestBoundary) {
    write_manifest(2U);
    auto source = inspect_load_dataset(directory_->path(), scale_);
    ASSERT_FALSE(source);
    EXPECT_EQ(source.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST_F(TpccLoadManifestTest, RejectsAFileWhoseSizeChangedAfterPublication) {
    std::ofstream changed{csv_path(8U), std::ios::binary | std::ios::app};
    ASSERT_TRUE(changed.is_open());
    changed << "extra\n";
    changed.close();

    auto source = inspect_load_dataset(directory_->path(), scale_);
    ASSERT_FALSE(source);
    EXPECT_EQ(source.error().code(), base::ErrorCode::kInvalidArgument);
}

}  // namespace
}  // namespace db::tpcc
