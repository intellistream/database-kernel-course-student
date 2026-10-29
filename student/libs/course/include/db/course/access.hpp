#pragma once

#include "db/access/btree_maintenance.hpp"
#include "db/access/btree_relation.hpp"
#include "db/access/btree_scan.hpp"
#include "db/access/dead_tid_store.hpp"
#include "db/access/heap_delete.hpp"
#include "db/access/heap_fetch.hpp"
#include "db/access/heap_maintenance.hpp"
#include "db/access/heap_scan.hpp"
#include "db/access/heap_table.hpp"
#include "db/access/heap_update.hpp"

namespace db::course::access {

using BtreeBulkBuildEntry = db::access::BtreeBulkBuildEntry;
using BtreeBulkBuildStats = db::access::BtreeBulkBuildStats;
using BtreeCleanupBatch = db::access::BtreeCleanupBatch;
using BtreeCleanupStats = db::access::BtreeCleanupStats;
using BtreeRelation = db::access::BtreeRelation;
using BtreeScan = db::access::BtreeScan;
using BtreeScanCandidate = db::access::BtreeScanCandidate;
using BtreeScanContext = db::access::BtreeScanContext;
using BtreeScanRange = db::access::BtreeScanRange;
using BtreeVacuumStats = db::access::BtreeVacuumStats;
using DeadTidStore = db::access::DeadTidStore;
using HeapDeleteOutcome = db::access::HeapDeleteOutcome;
using HeapDeleteRequest = db::access::HeapDeleteRequest;
using HeapFetchContext = db::access::HeapFetchContext;
using HeapFetchTuple = db::access::HeapFetchTuple;
using HeapIndexCleanupStats = db::access::HeapIndexCleanupStats;
using HeapMutationContext = db::access::HeapMutationContext;
using HeapScan = db::access::HeapScan;
using HeapScanContext = db::access::HeapScanContext;
using HeapScanTuple = db::access::HeapScanTuple;
using HeapTable = db::access::HeapTable;
using HeapUpdateKind = db::access::HeapUpdateKind;
using HeapUpdateOutcome = db::access::HeapUpdateOutcome;
using HeapUpdateRequest = db::access::HeapUpdateRequest;
using HeapVacuumStats = db::access::HeapVacuumStats;

// Topic-oriented names keep the course vocabulary short while the underlying
// access library retains its precise C++ names.
using BPlusTree = db::access::BtreeRelation;
using BTreeScan = db::access::BtreeScan;
using ScanRange = db::access::BtreeScanRange;
using BTreeVacuumStats = db::access::BtreeVacuumStats;
using DeleteResult = db::access::HeapDeleteOutcome;
using UpdateKind = db::access::HeapUpdateKind;
using UpdateResult = db::access::HeapUpdateOutcome;
using VacuumStats = db::access::HeapVacuumStats;

}  // namespace db::course::access
