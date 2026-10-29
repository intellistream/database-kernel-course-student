#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/base/result.hpp"
#include "db/catalog/store.hpp"

#include "format_internal.hpp"

namespace db::catalog {

auto unavailable(std::string_view message) -> base::Error;
auto load_image(const std::filesystem::path& path, const CatalogStoreOptions& options)
    -> base::Result<CatalogImage>;
auto require_image_size(std::size_t size, const CatalogStoreOptions& options) -> base::Status;
auto require_catalog_path_absent(const std::filesystem::path& path) -> base::Status;
auto publish_catalog_image(
    CatalogPublicationPort& publication,
    const std::filesystem::path& path,
    base::Bytes image,
    std::uint32_t permissions
) -> base::Status;
auto make_catalog_publication() -> std::unique_ptr<CatalogPublicationPort>;

template <typename Metadata>
[[maybe_unused]] void sort_by_oid([[maybe_unused]] std::vector<Metadata>& values) {
    // LAB-S02.Q10:R64-BEGIN
    base::lab::missing_algorithm("S02.Q10:R64");
    // LAB-S02.Q10:R64-END
}

auto table_position(const CatalogImage& image, std::string_view name) -> std::optional<std::size_t>;
auto table_position(const CatalogImage& image, base::Oid oid) -> std::optional<std::size_t>;
auto index_position(const CatalogImage& image, std::string_view name) -> std::optional<std::size_t>;
auto statistics_position(const CatalogImage& image, base::Oid table_oid)
    -> std::optional<std::size_t>;
void sort_statistics(std::vector<TableStatistics>& statistics);
auto advance_generation(CatalogImage& image) -> base::Status;

struct CatalogStore::Impl final {
    Impl(
        std::filesystem::path selected_path,
        CatalogStoreOptions selected_options,
        std::unique_ptr<CatalogPublicationPort> selected_publication,
        CatalogImage selected_image
    );

    std::filesystem::path path;
    CatalogStoreOptions options;
    std::unique_ptr<CatalogPublicationPort> publication;
    mutable std::mutex mutex;
    CatalogImage image;
    bool available{true};

    auto check_available() const -> base::Status;
    auto publish_candidate(CatalogImage candidate) -> base::Status;
};

}  // namespace db::catalog
