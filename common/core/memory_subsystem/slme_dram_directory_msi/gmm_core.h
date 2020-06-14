#pragma once

#include "core.h"
#include "fixed_types.h"
#include "shmem_req.h"
#include "shmem_perf_model.h"
#include "shmem_perf.h"
#include "segment_table.h"
#include "lock.h"
#include "cond.h"

#include <queue>

class AddressHomeLookup;

namespace SingleLevelMemory
{
   class DirectoryMSIPolicy;
   class PolicyBase;
   class ShmemMsg;
   class GlobalMemoryManager;

   class GMMCore : public Core
   {
      public:
         GMMCore(SInt32 id);
         ~GMMCore() override;

         void handleMsgFromNetwork(core_id_t sender, ShmemMsg* shmem_msg);
        // int getId() const override;
         void segmentAssignPolicy(IntPtr start, uint64_t policy_id);
         void createSegment(IntPtr start, uint64_t length);
         void Command(uint64_t cmd_type, IntPtr start, uint64_t arg1);
         PolicyBase* policyLookup(IntPtr address);

         void enqueueMessage(const ShmemMsg *shmem_msg);
         void dequeueMessage(ShmemMsg *shmem_msg);

      protected:
         DirectoryMSIPolicy* m_directory_policy;
         std::vector<Segment> m_segment_table;
         Lock m_segment_table_lock;
         AddressHomeLookup *m_dram_controller_home_lookup;

         GlobalMemoryManager *m_global_memory_manager;

         std::queue<ShmemMsg> m_msg_queue;
         Lock m_msg_queue_lock;
         ConditionVariable m_msg_cond;

         GlobalMemoryManager* getGlobalMemoryManager() { return m_global_memory_manager; }

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
