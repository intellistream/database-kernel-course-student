#include "db/io/durable.hpp"

namespace {

[[maybe_unused]] constexpr auto kAtomicReplace = &db::io::atomic_replace;
[[maybe_unused]] constexpr auto kPublishNewFile = &db::io::publish_new_file;
[[maybe_unused]] constexpr auto kPublishNewFileOpen = &db::io::publish_new_file_open;

}  // namespace
