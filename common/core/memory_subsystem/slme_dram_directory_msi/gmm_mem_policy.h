#pragma once

#include "policy.h"
#include "memory_manager_base.h"
#include "req_queue_list.h"
#include "dram_cntlr.h"
#include "shmem_req.h"
#include "shmem_msg.h"
#include "shmem_perf.h"
#include "mem_component.h"
#include "coherency_protocol.h"

namespace SingleLevelMemory
{
   class GMMMemPolicy: public PolicyBase
   {
      public:
         GMMMemPolicy(
               core_id_t core_id,
               GlobalMemoryManager* memory_manager,
               UInt32 cache_block_size,
               ShmemPerfModel* shmem_perf_model);

         ~GMMMemPolicy() override;

         void handleMsgFromGMM(core_id_t sender, ShmemMsg* shmem_msg) override;
         void handleMsgFromL2Cache(core_id_t sender, ShmemMsg* shmem_msg) override;
         void handleMsgFromDRAM(core_id_t sender, ShmemMsg* shmem_msg) override;

      protected:
         ReqQueueList* m_req_queue_list;

         core_id_t m_core_id;
         UInt32 m_cache_block_size;

         ShmemPerfModel* m_shmem_perf_model;
         ShmemPerf m_dummy_shmem_perf;

         UInt32 getCacheBlockSize() { return m_cache_block_size; }
         ShmemPerfModel* getShmemPerfModel() { return m_shmem_perf_model; }

         // Private Functions
         void processNullifyReq(ShmemReq* shmem_req);

         void processNextReqFromL2Cache(IntPtr address);
         void processExReqFromL2Cache(ShmemReq* shmem_req, Byte* cached_data_buf = NULL);
         void processShReqFromL2Cache(ShmemReq* shmem_req, Byte* cached_data_buf = NULL);
         void retrieveDataAndSendToL2Cache(ShmemMsg::msg_t reply_msg_type, core_id_t receiver, IntPtr address, Byte* cached_data_buf, ShmemMsg *orig_shmem_msg);
         void processDRAMReply(core_id_t sender, ShmemMsg* shmem_msg);

         void processUpgradeReqFromL2Cache(ShmemReq* shmem_req, Byte* cached_data_buf = NULL);

         void processInvRepFromL2Cache(core_id_t sender, ShmemMsg* shmem_msg);
         void processFlushRepFromL2Cache(core_id_t sender, ShmemMsg* shmem_msg);
         void processWbRepFromL2Cache(core_id_t sender, ShmemMsg* shmem_msg);
         void sendDataToDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now);

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
