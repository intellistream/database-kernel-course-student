#pragma once

#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/storage/frame.hpp"
#include "db/storage/free_space_map.hpp"
#include "db/storage/page.hpp"
#include "db/storage/replacement.hpp"

namespace db::course::storage {

using BufferPool = db::storage::BufferPool;
using CheckpointDirtySnapshot = db::storage::CheckpointDirtySnapshot;
using DiskManager = db::storage::DiskManager;
using FileDiskManager = db::storage::FileDiskManager;
using Frame = db::storage::Frame;
using FrameSnapshot = db::storage::FrameSnapshot;
using FreeSpaceMap = db::storage::FreeSpaceMap;
using LruReplacer = db::storage::LruReplacer;
using ClockReplacer = db::storage::ClockReplacer;
using Page = db::storage::Page;
using PageAddress = db::storage::PageAddress;
using PageHeader = db::storage::PageHeader;
using PageKind = db::storage::PageKind;
using PageReadGuard = db::storage::PageReadGuard;
using PageWriteGuard = db::storage::PageWriteGuard;
using PinGuard = db::storage::PinGuard;
using ReplacementPolicy = db::storage::ReplacementPolicy;
using Replacer = db::storage::ReplacementPolicy;

// PageAddress is the C++ name; PageTag is retained as a teaching synonym for
// the reference's physical-page vocabulary without introducing a new type.
using PageTag = db::storage::PageAddress;

}  // namespace db::course::storage
