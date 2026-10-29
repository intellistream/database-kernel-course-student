#pragma once

#include "db/wal/compression.hpp"
#include "db/wal/control.hpp"
#include "db/wal/control_store.hpp"
#include "db/wal/format.hpp"
#include "db/wal/geometry.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/reader.hpp"
#include "db/wal/record.hpp"
#include "db/wal/resource_manager.hpp"
#include "db/wal/segment_storage.hpp"
#include "db/wal/writer.hpp"

namespace db::course::wal {

using CompressionMethod = db::wal::CompressionMethod;
using DecodedWalRecord = db::wal::DecodedWalRecord;
using WalBlockReference = db::wal::WalBlockReference;
using WalControlImage = db::wal::WalControlImage;
using WalControlStore = db::wal::WalControlStore;
using WalManager = db::wal::WalManager;
using WalReader = db::wal::WalReader;
using WalRecord = db::wal::WalRecord;
using WalRecordHeader = db::wal::WalRecordHeader;
using WalResourceManager = db::wal::WalResourceManager;
using WalSegmentStorage = db::wal::WalSegmentStorage;
using WalWriter = db::wal::WalWriter;

namespace opcode = db::wal::opcode;

using db::wal::decode_record;
using db::wal::decode_record_header;
using db::wal::encode_record;
using db::wal::kWalRecordHeaderSize;

}  // namespace db::course::wal
