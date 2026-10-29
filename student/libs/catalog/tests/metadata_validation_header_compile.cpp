#include "metadata_validation.hpp"

namespace {

[[maybe_unused]] void compile_metadata_validation_header() {
    auto status = db::catalog::validate_catalog_name("name", "object");
    (void)status;
}

}  // namespace
