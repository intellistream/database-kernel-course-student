#pragma once

#include "db/api/result.hpp"
#include "db/base/identifier.hpp"
#include "db/catalog/metadata.hpp"
#include "db/catalog/schema.hpp"
#include "db/catalog/snapshot.hpp"
#include "db/catalog/store.hpp"
#include "db/execution/tuple.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::course::foundations {

using Catalog = catalog::CatalogStore;
using CatalogSnapshot = catalog::CatalogSnapshot;
using CatalogStore = catalog::CatalogStore;
using Column = catalog::Column;
using IndexInfo = catalog::IndexMetadata;
using Schema = catalog::Schema;
using TableInfo = catalog::TableMetadata;

using DataType = types::DataType;
using TypeModifier = types::TypeModifier;
using TimestampValue = types::TimestampValue;
using Value = types::Value;
using ValueKind = types::ValueKind;
using SqlTruth = types::SqlTruth;
using TypeId = base::TypeId;
using Oid = base::Oid;
using RelationId = base::RelationId;
using IndexId = base::IndexId;
using PageId = base::PageId;
using SlotId = base::SlotId;
using Lsn = base::Lsn;
using Rid = base::Rid;
using RID = base::Rid;
using TxnId = base::Xid;
using Xid = base::Xid;

using ResultColumn = api::ResultColumn;
using ResultRow = api::ResultRow;
using ResultSchema = api::ResultSchema;
using ResultSet = api::ResultSet;
using Row = execution::Tuple;
using Tuple = execution::Tuple;
using TupleDescriptor = execution::TupleDescriptor;

}  // namespace db::course::foundations
