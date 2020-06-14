#include "gmm_core.h"
#include "segment_table.h"
#include "log.h"
#include "global_memory_manager.h"
#include "stats.h"
#include "shmem_perf.h"
#include "coherency_protocol.h"
#include "config.hpp"
#include "policy.h"

#if 0
   extern Lock iolock;
#  include "core_manager.h"
#  include "simulator.h"
#  define MYLOG(...) { ScopedLock l(iolock); fflush(stdout); printf("[%s] %d%cdd %-25s@%3u: ", itostr(getShmemPerfModel()->getElapsedTime()).c_str(), getMemoryManager()->getCore()->getId(), Sim()->getCoreManager()->amiUserThread() ? '^' : '_', __FUNCTION__, __LINE__); printf(__VA_ARGS__); printf("\n"); fflush(stdout); }
#else
#  define MYLOG(...) {}
#endif

namespace SingleLevelMemory
{

GMMCore::GMMCore(core_id_t core_id,
      GlobalMemoryManager* memory_manager,
      ShmemPerfModel* shmem_perf_model):
   m_memory_manager(memory_manager),
   m_shmem_perf_model(shmem_perf_model)
{
   m_segment_table = new SegmentTable(memory_manager);
}

GMMCore::~GMMCore()
{
   delete m_segment_table;
}

void
GMMCore::handleMsgFromNetwork(core_id_t sender, ShmemMsg* shmem_msg)
{
   IntPtr address = shmem_msg->getAddress();

   MYLOG("begin for address %lx, %d in queue", address, m_dram_directory_req_queue_list->size(address));

   updateShmemPerf(shmem_msg, ShmemPerf::TD_ACCESS);

   // Look up line in segment table
   // bool slb_hit = false;

   // Look up policy
   PolicyBase *policy = getMemoryManager()->policyLookup(address);
   if (policy->getId() == 2)
      policy = getMemoryManager()->policyLookup(0);

   MemComponent::component_t sender_mem_component = shmem_msg->getSenderMemComponent();

   switch(sender_mem_component)
   {
      case MemComponent::LAST_LEVEL_CACHE:
         policy->handleMsgFromL2Cache(sender, shmem_msg);
         break;
      case MemComponent::DRAM:
         policy->handleMsgFromDRAM(sender, shmem_msg);
         break;
      case MemComponent::GMM:
         policy->handleMsgFromGMM(sender, shmem_msg);
         break;

      default:
         LOG_PRINT_ERROR("Unrecognized sender component(%u)",
               sender_mem_component);
         break;
   }
}

}
