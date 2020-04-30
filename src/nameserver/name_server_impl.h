//
// name_server_impl.h
// Copyright (C) 2017 4paradigm.com
// Author denglong
// Date 2017-09-05

#ifndef SRC_NAMESERVER_NAME_SERVER_IMPL_H_
#define SRC_NAMESERVER_NAME_SERVER_IMPL_H_

#include <brpc/server.h>

#include <atomic>
#include <condition_variable>  // NOLINT
#include <list>
#include <map>
#include <memory>
#include <mutex>  // NOLINT
#include <string>
#include <unordered_map>
#include <vector>

#include "base/hash.h"
#include "base/random.h"
#include "base/schema_codec.h"
#include "client/bs_client.h"
#include "client/ns_client.h"
#include "client/tablet_client.h"
#include "proto/name_server.pb.h"
#include "proto/tablet.pb.h"
#include "zk/dist_lock.h"
#include "zk/zk_client.h"

DECLARE_uint32(name_server_task_concurrency);
DECLARE_uint32(name_server_task_concurrency_for_replica_cluster);
DECLARE_int32(zk_keep_alive_check_interval);

namespace rtidb {
namespace nameserver {

using ::google::protobuf::Closure;
using ::google::protobuf::RpcController;
using ::rtidb::api::TabletState;
using ::rtidb::client::BsClient;
using ::rtidb::client::NsClient;
using ::rtidb::client::TabletClient;
using ::rtidb::zk::DistLock;
using ::rtidb::zk::ZkClient;

const uint64_t INVALID_PARENT_ID = UINT64_MAX;
const uint32_t INVALID_PID = UINT32_MAX;

// tablet info
struct TabletInfo {
    // tablet state
    TabletState state_;
    // tablet rpc handle
    std::shared_ptr<TabletClient> client_;
    // the date create
    uint64_t ctime_;
};
// oss info
struct BlobServerInfo {
    TabletState state_;
    std::shared_ptr<BsClient> client_;
    uint64_t ctime_;
};

class ClusterInfo {
 public:
    explicit ClusterInfo(const ::rtidb::nameserver::ClusterAddress& cdp);

    void CheckZkClient();

    void UpdateNSClient(const std::vector<std::string>& children);

    int Init(std::string& msg);  // NOLINT

    bool CreateTableRemote(const ::rtidb::api::TaskInfo& task_info,
                           const ::rtidb::nameserver::TableInfo& table_info,
                           const ::rtidb::nameserver::ZoneInfo& zone_info);

    bool DropTableRemote(const ::rtidb::api::TaskInfo& task_info,
                         const std::string& name,
                         const ::rtidb::nameserver::ZoneInfo& zone_info);

    bool AddReplicaClusterByNs(const std::string& alias,
                               const std::string& zone_name,
                               const uint64_t term,
                               std::string& msg);  // NOLINT

    bool RemoveReplicaClusterByNs(const std::string& alias,
                                  const std::string& zone_name,
                                  const uint64_t term, int& code,  // NOLINT
                                  std::string& msg);               // NOLINT

    std::shared_ptr<::rtidb::client::NsClient> client_;
    std::map<std::string, std::vector<TablePartition>> last_status;
    ::rtidb::nameserver::ClusterAddress cluster_add_;
    uint64_t ctime_;
    std::atomic<ClusterStatus> state_;

 private:
    std::shared_ptr<ZkClient> zk_client_;
    uint64_t session_term_;
    // todo :: add statsus variable show replicas status
};

// the container of tablet
typedef std::map<std::string, std::shared_ptr<TabletInfo>> Tablets;
typedef std::map<std::string, std::shared_ptr<BlobServerInfo>> BlobServers;

typedef boost::function<void()> TaskFun;

struct Task {
    Task(const std::string& endpoint,
         std::shared_ptr<::rtidb::api::TaskInfo> task_info)
        : endpoint_(endpoint), task_info_(task_info) {}
    ~Task() {}
    std::string endpoint_;
    std::shared_ptr<::rtidb::api::TaskInfo> task_info_;
    std::vector<std::shared_ptr<Task>> sub_task_;
    TaskFun fun_;
};

struct OPData {
    ::rtidb::api::OPInfo op_info_;
    std::list<std::shared_ptr<Task>> task_list_;
};

class NameServerImplTest;
class NameServerImplRemoteTest;

class NameServerImpl : public NameServer {
    // used for ut
    friend class NameServerImplTest;
    friend class NameServerImplRemoteTest;

 public:
    NameServerImpl();

    ~NameServerImpl();

    bool Init();

    NameServerImpl(const NameServerImpl&) = delete;

    NameServerImpl& operator=(const NameServerImpl&) = delete;

    void DeleteOPTask(RpcController* controller,
                      const ::rtidb::api::DeleteTaskRequest* request,
                      ::rtidb::api::GeneralResponse* response, Closure* done);

    void GetTaskStatus(RpcController* controller,
                       const ::rtidb::api::TaskStatusRequest* request,
                       ::rtidb::api::TaskStatusResponse* response,
                       Closure* done);

    void LoadTable(RpcController* controller, const LoadTableRequest* request,
                   GeneralResponse* response, Closure* done);

    void CreateTableInternel(
        GeneralResponse& response,  // NOLINT
        std::shared_ptr<::rtidb::nameserver::TableInfo> table_info,
        const std::vector<::rtidb::base::ColumnDesc>& columns,
        uint64_t cur_term, uint32_t tid,
        std::shared_ptr<::rtidb::api::TaskInfo> task_ptr);

    void CreateTableInfoSimply(RpcController* controller,
                               const CreateTableInfoRequest* request,
                               CreateTableInfoResponse* response,
                               Closure* done);

    void CreateTableInfo(RpcController* controller,
                         const CreateTableInfoRequest* request,
                         CreateTableInfoResponse* response, Closure* done);

    void CreateTable(RpcController* controller,
                     const CreateTableRequest* request,
                     GeneralResponse* response, Closure* done);

    void DropTableInternel(
        const DropTableRequest& request, GeneralResponse& response,  // NOLINT
        std::shared_ptr<::rtidb::nameserver::TableInfo> table_info,
        std::shared_ptr<::rtidb::api::TaskInfo> task_ptr);

    void DropTable(RpcController* controller, const DropTableRequest* request,
                   GeneralResponse* response, Closure* done);

    void AddTableField(RpcController* controller,
                       const AddTableFieldRequest* request,
                       GeneralResponse* response, Closure* done);

    void ShowTablet(RpcController* controller, const ShowTabletRequest* request,
                    ShowTabletResponse* response, Closure* done);

    void ShowTable(RpcController* controller, const ShowTableRequest* request,
                   ShowTableResponse* response, Closure* done);

    void MakeSnapshotNS(RpcController* controller,
                        const MakeSnapshotNSRequest* request,
                        GeneralResponse* response, Closure* done);

    int AddReplicaSimplyRemoteOP(const std::string& alias,
                                 const std::string& name,
                                 const std::string& endpoint, uint32_t tid,
                                 uint32_t pid);

    int AddReplicaRemoteOP(
        const std::string& alias, const std::string& name,
        const ::rtidb::nameserver::TablePartition& table_partition,
        uint32_t remote_tid, uint32_t pid);

    void AddReplicaNS(RpcController* controller,
                      const AddReplicaNSRequest* request,
                      GeneralResponse* response, Closure* done);

    void AddReplicaNSFromRemote(RpcController* controller,
                                const AddReplicaNSRequest* request,
                                GeneralResponse* response, Closure* done);

    int DelReplicaRemoteOP(const std::string& endpoint, const std::string name,
                           uint32_t pid);

    void DelReplicaNS(RpcController* controller,
                      const DelReplicaNSRequest* request,
                      GeneralResponse* response, Closure* done);

    void ShowOPStatus(RpcController* controller,
                      const ShowOPStatusRequest* request,
                      ShowOPStatusResponse* response, Closure* done);

    void ConfSet(RpcController* controller, const ConfSetRequest* request,
                 GeneralResponse* response, Closure* done);

    void ConfGet(RpcController* controller, const ConfGetRequest* request,
                 ConfGetResponse* response, Closure* done);

    void ChangeLeader(RpcController* controller,
                      const ChangeLeaderRequest* request,
                      GeneralResponse* response, Closure* done);

    void OfflineEndpoint(RpcController* controller,
                         const OfflineEndpointRequest* request,
                         GeneralResponse* response, Closure* done);

    void UpdateTTL(RpcController* controller,
                   const ::rtidb::nameserver::UpdateTTLRequest* request,
                   ::rtidb::nameserver::UpdateTTLResponse* response,
                   Closure* done);

    void Migrate(RpcController* controller, const MigrateRequest* request,
                 GeneralResponse* response, Closure* done);

    void RecoverEndpoint(RpcController* controller,
                         const RecoverEndpointRequest* request,
                         GeneralResponse* response, Closure* done);

    void RecoverTable(RpcController* controller,
                      const RecoverTableRequest* request,
                      GeneralResponse* response, Closure* done);

    void ConnectZK(RpcController* controller, const ConnectZKRequest* request,
                   GeneralResponse* response, Closure* done);

    void DisConnectZK(RpcController* controller,
                      const DisConnectZKRequest* request,
                      GeneralResponse* response, Closure* done);

    void SetTablePartition(RpcController* controller,
                           const SetTablePartitionRequest* request,
                           GeneralResponse* response, Closure* done);

    void GetTablePartition(RpcController* controller,
                           const GetTablePartitionRequest* request,
                           GetTablePartitionResponse* response, Closure* done);

    void UpdateTableAliveStatus(RpcController* controller,
                                const UpdateTableAliveRequest* request,
                                GeneralResponse* response, Closure* done);

    void CancelOP(RpcController* controller, const CancelOPRequest* request,
                  GeneralResponse* response, Closure* done);

    void AddReplicaCluster(RpcController* controller,
                           const ClusterAddress* request,
                           GeneralResponse* response, Closure* done);

    void AddReplicaClusterByNs(RpcController* controller,
                               const ReplicaClusterByNsRequest* request,
                               AddReplicaClusterByNsResponse* response,
                               Closure* done);

    void ShowReplicaCluster(RpcController* controller,
                            const GeneralRequest* request,
                            ShowReplicaClusterResponse* response,
                            Closure* done);

    void RemoveReplicaCluster(RpcController* controller,
                              const RemoveReplicaOfRequest* request,
                              GeneralResponse* response, Closure* done);

    void RemoveReplicaClusterByNs(RpcController* controller,
                                  const ReplicaClusterByNsRequest* request,
                                  GeneralResponse* response, Closure* done);

    void SwitchMode(RpcController* controller, const SwitchModeRequest* request,
                    GeneralResponse* response, Closure* done);

    void SyncTable(RpcController* controller, const SyncTableRequest* request,
                   GeneralResponse* response, Closure* done);

    void DeleteIndex(RpcController* controller,
                     const DeleteIndexRequest* request,
                     GeneralResponse* response, Closure* done);

    void AddIndex(RpcController* controller, const AddIndexRequest* request,
                  GeneralResponse* response, Closure* done);

    int SyncExistTable(
        const std::string& alias, const std::string& name,
        const std::vector<::rtidb::nameserver::TableInfo> tables_remote,
        const ::rtidb::nameserver::TableInfo& table_info_local, uint32_t pid,
        int& code, std::string& msg);  // NOLINT

    int CreateTableOnTablet(
        std::shared_ptr<::rtidb::nameserver::TableInfo> table_info,
        bool is_leader, const std::vector<::rtidb::base::ColumnDesc>& columns,
        std::map<uint32_t, std::vector<std::string>>& endpoint_map,  // NOLINT
        uint64_t term);

    void CheckZkClient();

    int UpdateTaskStatusRemote(bool is_recover_op);

    int UpdateTask(const std::list<std::shared_ptr<OPData>>& op_list,
                   const std::string& endpoint, const std::string& msg,
                   bool is_recover_op,
                   ::rtidb::api::TaskStatusResponse& response);  // NOLINT

    int UpdateTaskStatus(bool is_recover_op);

    int DeleteTaskRemote(const std::vector<uint64_t>& done_task_vec,
                         bool& has_failed);  // NOLINT

    void UpdateTaskMapStatus(uint64_t remote_op_id, uint64_t op_id,
                             const ::rtidb::api::TaskStatus& status);

    int DeleteTask();

    void DeleteTask(const std::vector<uint64_t>& done_task_vec);

    void ProcessTask();

    int UpdateZKTaskStatus();

    void CheckClusterInfo();

    bool CreateTableRemote(
        const ::rtidb::api::TaskInfo& task_info,
        const ::rtidb::nameserver::TableInfo& table_info,
        const std::shared_ptr<::rtidb::nameserver::ClusterInfo> cluster_info);

    bool DropTableRemote(
        const ::rtidb::api::TaskInfo& task_info, const std::string& name,
        const std::shared_ptr<::rtidb::nameserver::ClusterInfo> cluster_info);

 private:
    // Recover all memory status, the steps
    // 1.recover table meta from zookeeper
    // 2.recover table status from all tablets
    bool Recover();

    bool RecoverTableInfo();

    void RecoverClusterInfo();

    bool RecoverOPTask();

    int SetPartitionInfo(TableInfo& table_info);  // NOLINT

    int CheckTableMeta(const TableInfo& table_info);

    int FillColumnKey(TableInfo& table_info);  // NOLINT

    int CreateMakeSnapshotOPTask(std::shared_ptr<OPData> op_data);

    int CreateAddReplicaSimplyRemoteOPTask(std::shared_ptr<OPData> op_data);

    int CreateAddReplicaOPTask(std::shared_ptr<OPData> op_data);

    int CreateAddReplicaRemoteOPTask(std::shared_ptr<OPData> op_data);

    int CreateChangeLeaderOPTask(std::shared_ptr<OPData> op_data);

    int CreateMigrateTask(std::shared_ptr<OPData> op_data);

    int CreateRecoverTableOPTask(std::shared_ptr<OPData> op_data);

    int CreateDelReplicaRemoteOPTask(std::shared_ptr<OPData> op_data);

    int CreateDelReplicaOPTask(std::shared_ptr<OPData> op_data);

    int CreateOfflineReplicaTask(std::shared_ptr<OPData> op_data);

    int CreateReAddReplicaTask(std::shared_ptr<OPData> op_data);

    int CreateReAddReplicaNoSendTask(std::shared_ptr<OPData> op_data);

    int CreateReAddReplicaWithDropTask(std::shared_ptr<OPData> op_data);

    int CreateReAddReplicaSimplifyTask(std::shared_ptr<OPData> op_data);

    int CreateReLoadTableTask(std::shared_ptr<OPData> op_data);

    int CreateUpdatePartitionStatusOPTask(std::shared_ptr<OPData> op_data);

    bool SkipDoneTask(std::shared_ptr<OPData> op_data);

    int CreateTableRemoteTask(std::shared_ptr<OPData> op_data);

    int DropTableRemoteTask(std::shared_ptr<OPData> op_data);

    // Get the lock
    void OnLocked();
    // Lost the lock
    void OnLostLock();

    // Update tablets from zookeeper
    void UpdateTablets(const std::vector<std::string>& endpoints);

    void UpdateBlobServers(const std::vector<std::string>& endpoints);

    void UpdateBlobServersLocked(const std::vector<std::string>& endpoints);

    void OnTabletOffline(const std::string& endpoint, bool startup_flag);

    void RecoverOfflineTablet();

    void OnTabletOnline(const std::string& endpoint);

    void OnBlobOffline(const std::string& endpoint, bool startup_flag);

    void OnBlobOnline(const std::string& endpoint);

    void OfflineEndpointInternal(const std::string& endpoint,
                                 uint32_t concurrency);

    void RecoverEndpointInternal(const std::string& endpoint, bool need_restore,
                                 uint32_t concurrency);

    void UpdateTabletsLocked(const std::vector<std::string>& endpoints);

    void DelTableInfo(const std::string& name, const std::string& endpoint,
                      uint32_t pid,
                      std::shared_ptr<::rtidb::api::TaskInfo> task_info);

    void DelTableInfo(const std::string& name, const std::string& endpoint,
                      uint32_t pid,
                      std::shared_ptr<::rtidb::api::TaskInfo> task_info,
                      uint32_t flag);

    std::shared_ptr<BlobServerInfo> SetBlobTableInfo(TableInfo* table_info);

    void UpdatePartitionStatus(
        const std::string& name, const std::string& endpoint, uint32_t pid,
        bool is_leader, bool is_alive,
        std::shared_ptr<::rtidb::api::TaskInfo> task_info);

    int UpdateEndpointTableAlive(const std::string& endpoint, bool is_alive);

    std::shared_ptr<Task> CreateMakeSnapshotTask(const std::string& endpoint,
                                                 uint64_t op_index,
                                                 ::rtidb::api::OPType op_type,
                                                 uint32_t tid, uint32_t pid,
                                                 uint64_t end_offset);

    std::shared_ptr<Task> CreatePauseSnapshotTask(const std::string& endpoint,
                                                  uint64_t op_index,
                                                  ::rtidb::api::OPType op_type,
                                                  uint32_t tid, uint32_t pid);

    std::shared_ptr<Task> CreateRecoverSnapshotTask(
        const std::string& endpoint, uint64_t op_index,
        ::rtidb::api::OPType op_type, uint32_t tid, uint32_t pid);

    std::shared_ptr<Task> CreateSendSnapshotTask(
        const std::string& endpoint, uint64_t op_index,
        ::rtidb::api::OPType op_type, uint32_t tid, uint32_t remote_tid,
        uint32_t pid, const std::string& des_endpoint);

    std::shared_ptr<Task> CreateLoadTableTask(
        const std::string& endpoint, uint64_t op_index,
        ::rtidb::api::OPType op_type, const std::string& name, uint32_t tid,
        uint32_t pid, uint64_t ttl, uint32_t seg_cnt, bool is_leader,
        ::rtidb::common::StorageMode storage_mode);

    std::shared_ptr<Task> CreateLoadTableRemoteTask(
        const std::string& alias, const std::string& name,
        const std::string& endpoint, uint32_t pid, uint64_t op_index,
        ::rtidb::api::OPType op_type);

    std::shared_ptr<Task> CreateAddReplicaRemoteTask(
        const std::string& endpoint, uint64_t op_index,
        ::rtidb::api::OPType op_type, uint32_t tid, uint32_t remote_tid,
        uint32_t pid, const std::string& des_endpoint,
        uint64_t task_id = INVALID_PARENT_ID);

    std::shared_ptr<Task> CreateAddReplicaNSRemoteTask(
        const std::string& alias, const std::string& name,
        const std::vector<std::string>& endpoint_vec, uint32_t pid,
        uint64_t op_index, ::rtidb::api::OPType op_type);

    std::shared_ptr<Task> CreateAddReplicaTask(const std::string& endpoint,
                                               uint64_t op_index,
                                               ::rtidb::api::OPType op_type,
                                               uint32_t tid, uint32_t pid,
                                               const std::string& des_endpoint);

    std::shared_ptr<Task> CreateAddTableInfoTask(
        const std::string& alias, const std::string& endpoint,
        const std::string& name, uint32_t remote_tid, uint32_t pid,
        uint64_t op_index, ::rtidb::api::OPType op_type);

    std::shared_ptr<Task> CreateAddTableInfoTask(const std::string& name,
                                                 uint32_t pid,
                                                 const std::string& endpoint,
                                                 uint64_t op_index,
                                                 ::rtidb::api::OPType op_type);

    void AddTableInfo(const std::string& alias, const std::string& endpoint,
                      const std::string& name, uint32_t pid,
                      uint32_t remote_tid,
                      std::shared_ptr<::rtidb::api::TaskInfo> task_info);

    void AddTableInfo(const std::string& name, const std::string& endpoint,
                      uint32_t pid,
                      std::shared_ptr<::rtidb::api::TaskInfo> task_info);

    std::shared_ptr<Task> CreateDelReplicaTask(
        const std::string& endpoint, uint64_t op_index,
        ::rtidb::api::OPType op_type, uint32_t tid, uint32_t pid,
        const std::string& follower_endpoint);

    std::shared_ptr<Task> CreateDelTableInfoTask(const std::string& name,
                                                 uint32_t pid,
                                                 const std::string& endpoint,
                                                 uint64_t op_index,
                                                 ::rtidb::api::OPType op_type);

    std::shared_ptr<Task> CreateDelTableInfoTask(
        const std::string& name, uint32_t pid, const std::string& endpoint,
        uint64_t op_index, ::rtidb::api::OPType op_type, uint32_t flag);

    std::shared_ptr<Task> CreateUpdateTableInfoTask(
        const std::string& src_endpoint, const std::string& name, uint32_t pid,
        const std::string& des_endpoint, uint64_t op_index,
        ::rtidb::api::OPType op_type);

    void UpdateTableInfo(const std::string& src_endpoint,
                         const std::string& name, uint32_t pid,
                         const std::string& des_endpoint,
                         std::shared_ptr<::rtidb::api::TaskInfo> task_info);

    std::shared_ptr<Task> CreateUpdatePartitionStatusTask(
        const std::string& name, uint32_t pid, const std::string& endpoint,
        bool is_leader, bool is_alive, uint64_t op_index,
        ::rtidb::api::OPType op_type);

    std::shared_ptr<Task> CreateSelectLeaderTask(
        uint64_t op_index, ::rtidb::api::OPType op_type,
        const std::string& name, uint32_t tid, uint32_t pid,
        std::vector<std::string>& follower_endpoint);  // NOLINT

    std::shared_ptr<Task> CreateChangeLeaderTask(uint64_t op_index,
                                                 ::rtidb::api::OPType op_type,
                                                 const std::string& name,
                                                 uint32_t pid);

    std::shared_ptr<Task> CreateUpdateLeaderInfoTask(
        uint64_t op_index, ::rtidb::api::OPType op_type,
        const std::string& name, uint32_t pid);

    std::shared_ptr<Task> CreateCheckBinlogSyncProgressTask(
        uint64_t op_index, ::rtidb::api::OPType op_type,
        const std::string& name, uint32_t pid, const std::string& follower,
        uint64_t offset_delta);

    std::shared_ptr<Task> CreateDropTableTask(const std::string& endpoint,
                                              uint64_t op_index,
                                              ::rtidb::api::OPType op_type,
                                              uint32_t tid, uint32_t pid);

    std::shared_ptr<Task> CreateRecoverTableTask(
        uint64_t op_index, ::rtidb::api::OPType op_type,
        const std::string& name, uint32_t pid, const std::string& endpoint,
        uint64_t offset_delta, uint32_t concurrency);

    std::shared_ptr<Task> CreateTableRemoteTask(
        const ::rtidb::nameserver::TableInfo& table_info,
        const std::string& alias, uint64_t op_index,
        ::rtidb::api::OPType op_type);

    std::shared_ptr<Task> DropTableRemoteTask(const std::string& name,
                                              const std::string& alias,
                                              uint64_t op_index,
                                              ::rtidb::api::OPType op_type);

    std::shared_ptr<Task> CreateDumpIndexDataTask(
        uint64_t op_index, ::rtidb::api::OPType op_type, uint32_t tid,
        uint32_t pid, const std::string& endpoint, uint32_t partition_num,
        const ::rtidb::common::ColumnKey& column_key, uint32_t idx);

    std::shared_ptr<Task> CreateSendIndexDataTask(
        uint64_t op_index, ::rtidb::api::OPType op_type, uint32_t tid,
        uint32_t pid, const std::string& endpoint,
        const std::map<uint32_t, std::string>& pid_endpoint_map);

    std::shared_ptr<Task> CreateLoadIndexDataTask(uint64_t op_index,
                                                  ::rtidb::api::OPType op_type,
                                                  uint32_t tid, uint32_t pid,
                                                  const std::string& endpoint,
                                                  uint32_t partition_num);

    std::shared_ptr<Task> CreateExtractIndexDataTask(
        uint64_t op_index, ::rtidb::api::OPType op_type, uint32_t tid,
        uint32_t pid, const std::vector<std::string>& endpoints,
        uint32_t partition_num, const ::rtidb::common::ColumnKey& column_key,
        uint32_t idx);

    std::shared_ptr<Task> CreateAddIndexToTabletTask(
        uint64_t op_index, ::rtidb::api::OPType op_type, uint32_t tid,
        uint32_t pid, const std::vector<std::string>& endpoints,
        const ::rtidb::common::ColumnKey& column_key);

    std::shared_ptr<Task> CreateTableSyncTask(
        uint64_t op_index, ::rtidb::api::OPType op_type,
        const std::string& name, const boost::function<bool()>& fun);

    std::shared_ptr<TableInfo> GetTableInfo(const std::string& name);

    int AddOPTask(const ::rtidb::api::TaskInfo& task_info,
                  ::rtidb::api::TaskType task_type,
                  std::shared_ptr<::rtidb::api::TaskInfo>& task_ptr,  // NOLINT
                  std::vector<uint64_t> rep_cluster_op_id_vec);

    std::shared_ptr<::rtidb::api::TaskInfo> FindTask(
        uint64_t op_id, ::rtidb::api::TaskType task_type);

    int CreateBlobTable(std::shared_ptr<TableInfo> table_info);

    bool SaveTableInfo(std::shared_ptr<TableInfo> table_info);

    int CreateOPData(::rtidb::api::OPType op_type, const std::string& value,
                     std::shared_ptr<OPData>& op_data,  // NOLINT
                     const std::string& name, uint32_t pid,
                     uint64_t parent_id = INVALID_PARENT_ID,
                     uint64_t remote_op_id = INVALID_PARENT_ID);
    int AddOPData(const std::shared_ptr<OPData>& op_data,
                  uint32_t concurrency = FLAGS_name_server_task_concurrency);
    int CreateDelReplicaOP(const std::string& name, uint32_t pid,
                           const std::string& endpoint);
    int CreateChangeLeaderOP(
        const std::string& name, uint32_t pid,
        const std::string& candidate_leader, bool need_restore,
        uint32_t concurrency = FLAGS_name_server_task_concurrency);

    std::shared_ptr<rtidb::nameserver::ClusterInfo> GetHealthCluster(
        const std::string& alias);

    int CreateRecoverTableOP(const std::string& name, uint32_t pid,
                             const std::string& endpoint, bool is_leader,
                             uint64_t offset_delta, uint32_t concurrency);
    void SelectLeader(const std::string& name, uint32_t tid, uint32_t pid,
                      std::vector<std::string>& follower_endpoint,  // NOLINT
                      std::shared_ptr<::rtidb::api::TaskInfo> task_info);
    void ChangeLeader(std::shared_ptr<::rtidb::api::TaskInfo> task_info);
    void UpdateLeaderInfo(std::shared_ptr<::rtidb::api::TaskInfo> task_info);
    int CreateMigrateOP(const std::string& src_endpoint,
                        const std::string& name, uint32_t pid,
                        const std::string& des_endpoint);
    void RecoverEndpointTable(
        const std::string& name, uint32_t pid, std::string& endpoint,  // NOLINT
        uint64_t offset_delta, uint32_t concurrency,
        std::shared_ptr<::rtidb::api::TaskInfo> task_info);
    int GetLeader(std::shared_ptr<::rtidb::nameserver::TableInfo> table_info,
                  uint32_t pid, std::string& leader_endpoint);  // NOLINT
    int MatchTermOffset(const std::string& name, uint32_t pid, bool has_table,
                        uint64_t term, uint64_t offset);
    int CreateReAddReplicaOP(const std::string& name, uint32_t pid,
                             const std::string& endpoint, uint64_t offset_delta,
                             uint64_t parent_id, uint32_t concurrency);
    int CreateReAddReplicaSimplifyOP(const std::string& name, uint32_t pid,
                                     const std::string& endpoint,
                                     uint64_t offset_delta, uint64_t parent_id,
                                     uint32_t concurrency);
    int CreateReAddReplicaWithDropOP(const std::string& name, uint32_t pid,
                                     const std::string& endpoint,
                                     uint64_t offset_delta, uint64_t parent_id,
                                     uint32_t concurrency);
    int CreateReAddReplicaNoSendOP(const std::string& name, uint32_t pid,
                                   const std::string& endpoint,
                                   uint64_t offset_delta, uint64_t parent_id,
                                   uint32_t concurrency);
    int CreateReLoadTableOP(const std::string& name, uint32_t pid,
                            const std::string& endpoint, uint64_t parent_id,
                            uint32_t concurrency);
    int CreateReLoadTableOP(const std::string& name, uint32_t pid,
                            const std::string& endpoint, uint64_t parent_id,
                            uint32_t concurrency, uint64_t remote_op_id,
                            uint64_t& rep_cluter_op_id);  // NOLINT
    int CreateUpdatePartitionStatusOP(const std::string& name, uint32_t pid,

                                      const std::string& endpoint,
                                      bool is_leader, bool is_alive,
                                      uint64_t parent_id, uint32_t concurrency);
    int CreateOfflineReplicaOP(
        const std::string& name, uint32_t pid, const std::string& endpoint,
        uint32_t concurrency = FLAGS_name_server_task_concurrency);
    int CreateTableRemoteOP(
        const ::rtidb::nameserver::TableInfo& table_info,
        const ::rtidb::nameserver::TableInfo& remote_table_info,
        const std::string& alias, uint64_t parent_id = INVALID_PARENT_ID,
        uint32_t concurrency =
            FLAGS_name_server_task_concurrency_for_replica_cluster);

    int CreateAddIndexOP(const std::string& name, uint32_t pid,
                         const ::rtidb::common::ColumnKey& column_key,
                         uint32_t idx);

    int CreateAddIndexOPTask(std::shared_ptr<OPData> op_data);

    int DropTableRemoteOP(
        const std::string& name, const std::string& alias,
        uint64_t parent_id = INVALID_PARENT_ID,
        uint32_t concurrency =
            FLAGS_name_server_task_concurrency_for_replica_cluster);
    void NotifyTableChanged();
    void DeleteDoneOP();
    void UpdateTableStatus();
    int DropTableOnTablet(
        std::shared_ptr<::rtidb::nameserver::TableInfo> table_info);
    int DropTableOnBlob(std::shared_ptr<TableInfo> table_info);
    void CheckBinlogSyncProgress(
        const std::string& name, uint32_t pid, const std::string& follower,
        uint64_t offset_delta,
        std::shared_ptr<::rtidb::api::TaskInfo> task_info);

    bool AddIndexToTableInfo(const std::string& name,
                             const ::rtidb::common::ColumnKey& column_key,
                             uint32_t index_pos);

    void WrapTaskFun(const boost::function<bool()>& fun,
                     std::shared_ptr<::rtidb::api::TaskInfo> task_info);

    void RunSyncTaskFun(const std::string& name,
                        const boost::function<bool()>& fun,
                        std::shared_ptr<::rtidb::api::TaskInfo> task_info);

    void RunSubTask(std::shared_ptr<Task> task);

    // get tablet info
    std::shared_ptr<TabletInfo> GetTabletInfo(const std::string& endpoint);
    std::shared_ptr<OPData> FindRunningOP(uint64_t op_id);

    // update ttl for partition
    bool UpdateTTLOnTablet(const std::string& endpoint, int32_t tid,
                           int32_t pid, const ::rtidb::api::TTLType& type,
                           uint64_t abs_ttl, uint64_t lat_ttl,
                           const std::string& ts_name);

    void CheckSyncExistTable(
        const std::string& alias,
        const std::vector<::rtidb::nameserver::TableInfo>& tables_remote,
        const std::shared_ptr<::rtidb::client::NsClient> ns_client);

    void CheckSyncTable(
        const std::string& alias,
        const std::vector<::rtidb::nameserver::TableInfo> tables,
        const std::shared_ptr<::rtidb::client::NsClient> ns_client);

    bool CompareTableInfo(
        const std::vector<::rtidb::nameserver::TableInfo>& tables,
        bool period_check);

    void CheckTableInfo(
        std::shared_ptr<ClusterInfo>& ci,  // NOLINT
        const std::vector<::rtidb::nameserver::TableInfo>& tables);

    bool CompareSnapshotOffset(
        const std::vector<TableInfo>& tables, std::string& msg,  // NOLINT
        int& code,                                               // NOLINT
        std::map<std::string, std::map<uint32_t, std::map<uint32_t, uint64_t>>>&
            table_part_offset);

    void DistributeTabletMode();

    void SchedMakeSnapshot();

    void MakeTablePartitionSnapshot(
        uint32_t pid, uint64_t end_offset,
        std::shared_ptr<::rtidb::nameserver::TableInfo> table_info);

 private:
    std::mutex mu_;
    Tablets tablets_;
    BlobServers blob_servers_;
    std::map<std::string, std::shared_ptr<::rtidb::nameserver::TableInfo>>
        table_info_;
    std::map<std::string, std::shared_ptr<::rtidb::nameserver::ClusterInfo>>
        nsc_;
    ZoneInfo zone_info_;
    ZkClient* zk_client_;
    DistLock* dist_lock_;
    ::baidu::common::ThreadPool thread_pool_;
    ::baidu::common::ThreadPool task_thread_pool_;
    std::string zk_table_index_node_;
    std::string zk_term_node_;
    std::string zk_table_data_path_;
    std::string zk_auto_failover_node_;
    std::string zk_auto_recover_table_node_;
    std::string zk_table_changed_notify_node_;
    std::string zk_offline_endpoint_lock_node_;
    std::string zk_zone_data_path_;
    uint32_t table_index_;
    uint64_t term_;
    std::string zk_op_index_node_;
    std::string zk_op_data_path_;
    std::string zk_op_sync_path_;
    uint64_t op_index_;
    std::atomic<bool> running_;
    std::list<std::shared_ptr<OPData>> done_op_list_;
    std::vector<std::list<std::shared_ptr<OPData>>> task_vec_;
    std::condition_variable cv_;
    std::atomic<bool> auto_failover_;
    std::atomic<uint32_t> mode_;
    std::map<std::string, uint64_t> offline_endpoint_map_;
    ::rtidb::base::Random rand_;
    uint64_t session_term_;
    std::atomic<uint64_t> task_rpc_version_;
    std::map<uint64_t, std::list<std::shared_ptr<::rtidb::api::TaskInfo>>>
        task_map_;
};

}  // namespace nameserver
}  // namespace rtidb
#endif  // SRC_NAMESERVER_NAME_SERVER_IMPL_H_
