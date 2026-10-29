#include "db/storage/disk_manager.hpp"

namespace {

template <typename Type>
concept ExposesDiskManagerImplementation = requires { typename Type::Impl; };

[[maybe_unused]] constexpr auto kDiskManagerPointerSize = sizeof(db::storage::DiskManager*);
static_assert(!ExposesDiskManagerImplementation<db::storage::FileDiskManager>);

}  // namespace
