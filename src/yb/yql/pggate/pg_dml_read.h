//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGGATE_PG_DML_READ_H_
#define YB_YQL_PGGATE_PG_DML_READ_H_

#include <vector>

#include "yb/common/pgsql_protocol.fwd.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/status_fwd.h"

#include "yb/yql/pggate/pg_dml.h"
#include "yb/yql/pggate/pg_doc_op.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_statement.h"

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------
// DML_READ
//--------------------------------------------------------------------------------------------------
// Scan Scenarios:
//
// 1. SequentialScan or PrimaryIndexScan (class PgSelect)
//    - YugaByte does not have a separate table for PrimaryIndex.
//    - The target table descriptor, where data is read and returned, is the main table.
//    - The binding table descriptor, whose column is bound to values, is also the main table.
//
// 2. IndexOnlyScan (Class PgSelectIndex)
//    - This special case is optimized where data is read from index table.
//    - The target table descriptor, where data is read and returned, is the index table.
//    - The binding table descriptor, whose column is bound to values, is also the index table.
//
// 3. IndexScan SysTable / UserTable (Class PgSelect and Nested PgSelectIndex)
//    - YugaByte will use the binds to query base-ybctid in the index table, which is then used
//      to query data from the main table.
//    - The target table descriptor, where data is read and returned, is the main table.
//    - The binding table descriptor, whose column is bound to values, is the index table.

class PgDmlRead : public PgDml {
 public:
  PgDmlRead(PgSession::ScopedRefPtr pg_session, const PgObjectId& table_id,
           const PgObjectId& index_id, const PgPrepareParameters *prepare_params,
           bool is_region_local);
  virtual ~PgDmlRead();

  StmtOp stmt_op() const override { return StmtOp::STMT_SELECT; }

  virtual Status Prepare() = 0;

  // Allocate binds.
  virtual void PrepareBinds();

  // Set forward (or backward) scan.
  void SetForwardScan(const bool is_forward_scan);

  // Bind a range column with a BETWEEN condition.
  Status BindColumnCondBetween(int attr_num, PgExpr *attr_value, PgExpr *attr_value_end);

  // Bind a column with an IN condition.
  Status BindColumnCondIn(int attnum, int n_attr_values, PgExpr **attr_values);

  Status BindHashCode(bool start_valid, bool start_inclusive,
                                uint64_t start_hash_val, bool end_valid,
                                bool end_inclusive, uint64_t end_hash_val);

  // Add a lower bound to the scan. If a lower bound has already been added
  // this call will set the lower bound to the stricter of the two bounds.
  Status AddRowLowerBound(YBCPgStatement handle, int n_col_values,
                                    PgExpr **col_values, bool is_inclusive);

  // Add an upper bound to the scan. If an upper bound has already been added
  // this call will set the upper bound to the stricter of the two bounds.
  Status AddRowUpperBound(YBCPgStatement handle, int n_col_values,
                                    PgExpr **col_values, bool is_inclusive);

  // Execute.
  virtual Status Exec(const PgExecParameters *exec_params);

  void SetCatalogCacheVersion(const uint64_t catalog_cache_version) override {
    DCHECK_NOTNULL(read_req_)->set_ysql_catalog_version(catalog_cache_version);
  }

  void UpgradeDocOp(PgDocOp::SharedPtr doc_op);

  const LWPgsqlReadRequestPB* read_req() const { return read_req_.get(); }

  bool IsReadFromYsqlCatalog() const;

  bool IsIndexOrderedScan() const;

 protected:
  // Allocate column protobuf.
  LWPgsqlExpressionPB *AllocColumnBindPB(PgColumn *col) override;
  LWPgsqlExpressionPB *AllocColumnBindConditionExprPB(PgColumn *col);
  LWPgsqlExpressionPB *AllocIndexColumnBindPB(PgColumn *col);

  // Allocate protobuf for target.
  LWPgsqlExpressionPB *AllocTargetPB() override;

  // Allocate protobuf for a qual in the read request's where_clauses list.
  LWPgsqlExpressionPB *AllocQualPB() override;

  // Allocate protobuf for a column reference in the read request's col_refs list.
  LWPgsqlColRefPB *AllocColRefPB() override;

  // Clear the read request's col_refs list.
  void ClearColRefPBs() override;

  // Allocate column expression.
  LWPgsqlExpressionPB *AllocColumnAssignPB(PgColumn *col) override;

  // Add column refs to protobuf read request.
  void SetColumnRefs();

  // References mutable request from template operation of doc_op_.
  std::shared_ptr<LWPgsqlReadRequestPB> read_req_;

 private:
  // Indicates that current operation reads concrete row by specifying row's DocKey.
  bool IsConcreteRowRead() const;
  Status ProcessEmptyPrimaryBinds();
  bool IsAllPrimaryKeysBound(size_t num_range_components_in_expected);
  bool CanBuildYbctidsFromPrimaryBinds();
  Result<std::vector<std::string>> BuildYbctidsFromPrimaryBinds();
  Status SubstitutePrimaryBindsWithYbctids(const PgExecParameters* exec_params);
  Result<docdb::DocKey> EncodeRowKeyForBound(
      YBCPgStatement handle, size_t n_col_values, PgExpr **col_values, bool for_lower_bound);
  Status MoveBoundKeyInOperator(PgColumn* col, const LWPgsqlConditionPB& in_operator);
  Result<LWQLValuePB*> GetBoundValue(
      const PgColumn& col, const LWPgsqlExpressionPB& src) const;
  Result<docdb::KeyEntryValue> BuildKeyColumnValue(
      const PgColumn& col, const LWPgsqlExpressionPB& src, LWQLValuePB** dest);
  Result<docdb::KeyEntryValue> BuildKeyColumnValue(
      const PgColumn& col, const LWPgsqlExpressionPB& src);

  // Holds original doc_op_ object after call of the UpgradeDocOp method.
  // Required to prevent structures related to request from being freed.
  PgDocOp::SharedPtr original_doc_op_;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_DML_READ_H_
