#pragma once

#include "dram_directory_cache.h"
#include "req_queue_list.h"
#include "dram_cntlr.h"
#include "address_home_lookup.h"
#include "shmem_req.h"
#include "shmem_msg.h"
#include "shmem_perf.h"
#include "mem_component.h"
#include "memory_manager_base.h"
#include "coherency_protocol.h"
#include "segment_table.h"

class NucaCache;

namespace SingleLevelMemory
{
   class GlobalMemoryManager;
   class PolicyBase;

   class GMMCoreFast
   {
      private:
         // Functional Models
         GlobalMemoryManager* m_memory_manager;
         ReqQueueList* m_dram_directory_req_queue_list;

         core_id_t m_core_id;
         // UInt32 m_cache_block_size;

         ShmemPerfModel* m_shmem_perf_model;
         SegmentTable* m_segment_table;
         PolicyBase* m_default_policy;
         AddressHomeLookup *m_dram_controller_home_lookup;
         // UInt32 getCacheBlockSize() { return m_cache_block_size; }
         GlobalMemoryManager* getMemoryManager() { return m_memory_manager; }
         ShmemPerfModel* getShmemPerfModel() { return m_shmem_perf_model; }

         void updateShmemPerf(ShmemReq *shmem_req, ShmemPerf::shmem_times_type_t reason = ShmemPerf::UNKNOWN)
         {
            updateShmemPerf(shmem_req->getShmemMsg(), reason);
         }
         void updateShmemPerf(ShmemMsg *shmem_msg, ShmemPerf::shmem_times_type_t reason = ShmemPerf::UNKNOWN)
         {
            shmem_msg->getPerf()->updateTime(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD), reason);
         }

      public:
         GMMCoreFast(core_id_t core_id,
               GlobalMemoryManager* memory_manager,
               std::vector<core_id_t>& core_list_with_dram_controllers,
               ShmemPerfModel* shmem_perf_model);
         ~GMMCoreFast();

         void handleMsgFromNetwork(core_id_t sender, ShmemMsg* shmem_msg);
   };

}
