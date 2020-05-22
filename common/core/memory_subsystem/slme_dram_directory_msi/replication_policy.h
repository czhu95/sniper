#pragma once

#include "policy.h"
#include "memory_manager_base.h"
#include "dram_directory_cache.h"
#include "req_queue_list.h"
#include "dram_cntlr.h"
#include "address_home_lookup.h"
#include "shmem_req.h"
#include "shmem_msg.h"
#include "shmem_perf.h"
#include "mem_component.h"
#include "coherency_protocol.h"

#include <set>
#include <map>
#include <vector>

namespace SingleLevelMemory
{
   class ReplicationPolicy: public PolicyBase
   {
      public:
         ReplicationPolicy(
               Core* core,
               GlobalMemoryManager* memory_manager,
               AddressHomeLookup* dram_controller_home_lookup,
               UInt32 replica_block_size,
               ShmemPerfModel* shmem_perf_model);

         ~ReplicationPolicy() override;

         void handleMsgFromGMM(core_id_t sender, ShmemMsg* shmem_msg) override;
         void handleMsgFromL2Cache(core_id_t sender, ShmemMsg* shmem_msg) override;
         void handleMsgFromDRAM(core_id_t sender, ShmemMsg* shmem_msg) override;

      protected:
         AddressHomeLookup* m_dram_controller_home_lookup;
         ReqQueueList* m_dram_directory_req_queue_list;

         core_id_t m_core_id;
         UInt32 m_replica_block_size;

         ShmemPerfModel* m_shmem_perf_model;
         ShmemPerf m_dummy_shmem_perf;

         CoherencyProtocol::type_t m_protocol;

         UInt64 evict[DirectoryState::NUM_DIRECTORY_STATES];
         UInt64 forward, forward_failed;

         std::set<IntPtr> m_replicas;

         std::map<IntPtr, std::set<IntPtr>> m_outstanding_remote;

         ComponentLatency m_access_time;

         ShmemPerfModel* getShmemPerfModel() { return m_shmem_perf_model; }

         // Private Functions
         DirectoryEntry* processDirectoryEntryAllocationReq(ShmemReq* shmem_req);
         void processNullifyReq(ShmemReq* shmem_req);

         void processNextReqFromL2Cache(IntPtr address);
         void processShReqFromL2Cache(ShmemReq* shmem_req, Byte* cached_data_buf = NULL);
         void retrieveDataAndSendToL2Cache(ShmemMsg::msg_t reply_msg_type, core_id_t receiver, IntPtr address, Byte* cached_data_buf, ShmemMsg *orig_shmem_msg);
         void processDRAMReply(core_id_t sender, ShmemMsg* shmem_msg);

         void processInvRepFromL2Cache(core_id_t sender, ShmemMsg* shmem_msg);

         void updateShmemPerf(ShmemReq *shmem_req, ShmemPerf::shmem_times_type_t reason = ShmemPerf::UNKNOWN)
         {
            updateShmemPerf(shmem_req->getShmemMsg(), reason);
         }
         void updateShmemPerf(ShmemMsg *shmem_msg, ShmemPerf::shmem_times_type_t reason = ShmemPerf::UNKNOWN)
         {
            shmem_msg->getPerf()->updateTime(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD), reason);
         }
   };
}
