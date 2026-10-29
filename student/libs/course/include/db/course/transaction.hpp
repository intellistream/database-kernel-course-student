#pragma once

#include "db/transaction/backend.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/lock_manager.hpp"
#include "db/transaction/manager.hpp"
#include "db/transaction/proc_array.hpp"
#include "db/transaction/proc_slot.hpp"
#include "db/transaction/ssi.hpp"
#include "db/transaction/state.hpp"
#include "db/transaction/visibility.hpp"
#include "db/transaction/xid.hpp"
#include "db/transaction/xid_waiter.hpp"

namespace db::course::transaction {

using BackendLocalState = db::transaction::BackendLocalState;
using CommandOutcome = db::transaction::CommandOutcome;
using CommitLog = db::transaction::CommitLog;
using IsolationLevel = db::transaction::IsolationLevel;
using LockManager = db::transaction::LockManager;
using ProcArray = db::transaction::ProcArray;
using ProcSlot = db::transaction::ProcSlot;
using ProcSlotLease = db::transaction::ProcSlotLease;
using SerializableConflictManager = db::transaction::SerializableConflictManager;
using Snapshot = db::transaction::Snapshot;
using SnapshotCoordinator = db::transaction::SnapshotCoordinator;
using TransactionBackend = db::transaction::TransactionBackend;
using TransactionBackendOptions = db::transaction::TransactionBackendOptions;
using TransactionBlockState = db::transaction::TransactionBlockState;
using TransactionManager = db::transaction::TransactionManager;
using TransactionStateMachine = db::transaction::TransactionStateMachine;
using TransactionStatus = db::transaction::TransactionStatus;
using XidHorizon = db::transaction::XidHorizon;
using XidWaiter = db::transaction::XidWaiter;

using TransactionState = db::transaction::TransactionBlockState;
using SIReadRecord = db::transaction::SsiReadRecord;
using SIReadTarget = db::transaction::SsiReadTarget;

using db::transaction::isolation_level_name;
using db::transaction::transaction_block_state_name;

}  // namespace db::course::transaction
