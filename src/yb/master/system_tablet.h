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
//

#ifndef YB_MASTER_SYSTEM_TABLET_H
#define YB_MASTER_SYSTEM_TABLET_H

#include "yb/common/hybrid_time.h"

#include "yb/master/master_fwd.h"

#include "yb/tablet/abstract_tablet.h"

namespace yb {
namespace master {

// This is a virtual tablet that is used for our virtual tables in the system namespace.
class SystemTablet : public tablet::AbstractTablet {
 public:
  SystemTablet(const Schema& schema, std::unique_ptr<YQLVirtualTable> yql_virtual_table,
               const TabletId& tablet_id);

  docdb::DocReadContextPtr GetDocReadContext(const std::string& table_id = "") const override;

  const docdb::YQLStorageIf& QLStorage() const override;

  TableType table_type() const override;

  const TabletId& tablet_id() const override;

  bool system() const override {
    return true;
  }

  tablet::TabletRetentionPolicy* RetentionPolicy() override {
    return nullptr;
  }

  Status HandleRedisReadRequest(CoarseTimePoint deadline,
                                        const ReadHybridTime& read_time,
                                        const RedisReadRequestPB& redis_read_request,
                                        RedisResponsePB* response) override {
    return STATUS(NotSupported, "RedisReadRequest is not supported for system tablets!");
  }

  Status HandleQLReadRequest(CoarseTimePoint deadline,
                                     const ReadHybridTime& read_time,
                                     const QLReadRequestPB& ql_read_request,
                                     const TransactionMetadataPB& transaction_metadata,
                                     tablet::QLReadRequestResult* result) override;

  Status CreatePagingStateForRead(const QLReadRequestPB& ql_read_request,
                                          const size_t row_count,
                                          QLResponsePB* response) const override;

  Status HandlePgsqlReadRequest(CoarseTimePoint deadline,
                                        const ReadHybridTime& read_time,
                                        bool is_explicit_request_read_time,
                                        const PgsqlReadRequestPB& pgsql_read_request,
                                        const TransactionMetadataPB& transaction_metadata,
                                        const SubTransactionMetadataPB& subtransaction_metadata,
                                        tablet::PgsqlReadRequestResult* result,
                                        size_t* num_rows_read) override {
    return STATUS(NotSupported, "Postgres system table is not yet supported");
  }

  Status CreatePagingStateForRead(const PgsqlReadRequestPB& pgsql_read_request,
                                          const size_t row_count,
                                          PgsqlResponsePB* response) const override {
    return STATUS(NotSupported, "Postgres system table is not yet supported");
  }

  const TableName& GetTableName() const;

  Result<IsolationLevel> GetIsolationLevel(const TransactionMetadataPB& transaction) override {
    return IsolationLevel::NON_TRANSACTIONAL;
  }

  // Decides whether the given request should go through the distributed transaction framework
  // based on internal properties of the tablet and whether it is a YSQL request (is_ysql_request).
  bool IsTransactionalRequest(bool is_ysql_request) const override { return false; }

 private:
  Result<HybridTime> DoGetSafeTime(
      tablet::RequireLease require_lease, HybridTime min_allowed,
      CoarseTimePoint deadline) const override;

  docdb::DocReadContextPtr doc_read_context_;
  std::unique_ptr<YQLVirtualTable> yql_virtual_table_;
  TabletId tablet_id_;
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_SYSTEM_TABLET_H
