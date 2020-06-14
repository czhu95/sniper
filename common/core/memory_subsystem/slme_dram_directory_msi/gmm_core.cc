#include "gmm_core.h"
#include "network.h"
#include "branch_predictor.h"
#include "memory_manager_base.h"
#include "performance_model.h"
#include "clock_skew_minimization_object.h"
#include "core_manager.h"
#include "dvfs_manager.h"
#include "hooks_manager.h"
#include "trace_manager.h"
#include "simulator.h"
#include "log.h"
#include "config.hpp"
#include "stats.h"
#include "topology_info.h"
#include "directory_msi_policy.h"
#include "segment_table.h"
#include "policy.h"
#include "shmem_msg.h"
#include "address_home_lookup.h"
#include "global_memory_manager.h"
#include "shmem_perf.h"

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

GMMCore::GMMCore(SInt32 id)
   : Core(id, 0)
{
   LOG_PRINT("GMMCore ctor for: %d", id);

   registerStatsMetric("gmm-core", id, "instructions", &m_instructions);
   registerStatsMetric("gmm-core", id, "spin_loops", &m_spin_loops);
   registerStatsMetric("gmm-core", id, "spin_instructions", &m_spin_instructions);
   registerStatsMetric("gmm-core", id, "spin_elapsed_time", &m_spin_elapsed_time);

   Sim()->getStatsManager()->logTopology("gmm-core", id, id);

   m_network = new Network(this);

   m_clock_skew_minimization_client = ClockSkewMinimizationClient::create(this);

   m_shmem_perf_model = new ShmemPerfModel();

   LOG_PRINT("instantiated memory manager model");
   m_memory_manager = MemoryManagerBase::createMMU(
         Sim()->getCfg()->getString("caching_protocol/type"),
         this, m_network, m_shmem_perf_model);

   m_global_memory_manager = dynamic_cast<GlobalMemoryManager *>(m_memory_manager);
   LOG_ASSERT_ERROR(m_global_memory_manager, "GMM core should attach to a GlobalMemoryManager");

   m_performance_model = PerformanceModel::create(this);

   const ComponentPeriod *global_domain = Sim()->getDvfsManager()->getGlobalDomain();

   // Dram Directory Cache
   UInt32 dram_directory_total_entries = Sim()->getCfg()->getInt("perf_model/dram_directory/total_entries");
   UInt32 dram_directory_associativity = Sim()->getCfg()->getInt("perf_model/dram_directory/associativity");
   UInt32 dram_directory_max_num_sharers = Sim()->getConfig()->getApplicationCores();
   UInt32 dram_directory_max_hw_sharers = Sim()->getCfg()->getInt("perf_model/dram_directory/max_hw_sharers");
   String dram_directory_type_str = Sim()->getCfg()->getString("perf_model/dram_directory/directory_type");
   UInt32 dram_directory_home_lookup_param = Sim()->getCfg()->getInt("perf_model/dram_directory/home_lookup_param");
   ComponentLatency dram_directory_cache_access_time = ComponentLatency(global_domain, Sim()->getCfg()->getInt("perf_model/dram_directory/directory_cache_access_time"));

   std::vector<core_id_t> core_list_with_dram_controllers;
   for (core_id_t core_id = Sim()->getConfig()->getApplicationCores();
        core_id < (core_id_t)Sim()->getConfig()->getTotalCores(); core_id ++)
   {
      core_list_with_dram_controllers.push_back(core_id);
   }
   m_dram_controller_home_lookup = new AddressHomeLookup(dram_directory_home_lookup_param, core_list_with_dram_controllers, m_memory_manager->getCacheBlockSize());

   m_directory_policy = new DirectoryMSIPolicy(getId(),
               m_global_memory_manager,
               m_dram_controller_home_lookup,
               dram_directory_total_entries,
               dram_directory_associativity,
               m_memory_manager->getCacheBlockSize(),
               dram_directory_max_num_sharers,
               dram_directory_max_hw_sharers,
               dram_directory_type_str,
               dram_directory_cache_access_time,
               m_shmem_perf_model);
}

GMMCore::~GMMCore()
{
}

void
GMMCore::handleMsgFromNetwork(core_id_t sender, ShmemMsg* shmem_msg)
{
   IntPtr address = shmem_msg->getAddress();

   // MYLOG("begin for address %lx, %d in queue", address, m_dram_directory_req_queue_list->size(address));
   enqueueMessage(shmem_msg);

   updateShmemPerf(shmem_msg, ShmemPerf::TD_ACCESS);

   // Look up line in segment table
   // bool slb_hit = false;

   // Look up policy
   PolicyBase *policy = policyLookup(address);

   MemComponent::component_t sender_mem_component = shmem_msg->getSenderMemComponent();

   switch(sender_mem_component)
   {
      case MemComponent::LAST_LEVEL_CACHE:
         policy->handleMsgFromL2Cache(sender, shmem_msg);
         break;
      case MemComponent::DRAM:
         policy->handleMsgFromDRAM(sender, shmem_msg);
         break;
      case MemComponent::GMM_CORE:
         policy->handleMsgFromGMM(sender, shmem_msg);
         break;

      default:
         LOG_PRINT_ERROR("Unrecognized sender component(%u)",
               sender_mem_component);
         break;
   }
}

PolicyBase*
GMMCore::policyLookup(IntPtr address)
{
   PolicyBase *policy = m_directory_policy;
   m_segment_table_lock.acquire_read();
   for (const auto& seg : m_segment_table)
      if (seg.contains(address) && seg.m_policy)
      {
         policy = seg.m_policy;
         break;
      }

   m_segment_table_lock.release_read();

   return policy;
}

void
GMMCore::Command(uint64_t cmd_type, IntPtr start, uint64_t arg1)
{
   // if (cmd_type == 0)
   //    createSegment(start, arg1);
   // else if (cmd_type == 1)
   //    segmentAssignPolicy(start, arg1);
}

void
GMMCore::createSegment(IntPtr start, uint64_t length)
{
   Segment new_seg{0, start, start + length, NULL};
   m_segment_table_lock.acquire();

   for (const auto& seg : m_segment_table)
      assert(seg != new_seg);

   m_segment_table.push_back(new_seg);
   m_segment_table_lock.release();


   MYLOG("Created segment: %p - %p", (void *)new_seg.m_start, (void *)new_seg.m_end);

}

void
GMMCore::segmentAssignPolicy(IntPtr start, uint64_t policy_id)
{
   // m_segment_table_lock.acquire();
   // for (auto& seg : m_segment_table)
   // {
   //    if (seg.m_start == start)
   //    {
   //       if (policy_id == 1)
   //       {
   //          seg.m_policy = new ReplicationPolicy(getCore(),
   //                this,
   //                m_dram_controller_home_lookup,
   //                1024 * 1024,
   //                getShmemPerfModel());
   //       }
   //       else
   //       {
   //          if (seg.m_policy)
   //             delete seg.m_policy;
   //          seg.m_policy = NULL;
   //       }

   //       MYLOG("Segment assign policy: %p - %d", (void *)seg.m_start, policy_id);
   //    }
   // }
   // m_segment_table_lock.release();
}


// int
// GMMCore::getId() const
// {
//    return m_core_id;
// }

void
GMMCore::enqueueMessage(const ShmemMsg *msg)
{
   m_msg_queue_lock.acquire();
   m_msg_queue.emplace(*msg);
   if (m_msg_queue.size() == 1)
      m_msg_cond.signal();

   m_msg_queue_lock.release();
}

void
GMMCore::dequeueMessage(ShmemMsg *msg)
{
   m_msg_queue_lock.acquire();
   if (m_msg_queue.empty())
      m_msg_cond.wait(m_msg_queue_lock);

   *msg = m_msg_queue.front();
   m_msg_queue.pop();
   m_msg_queue_lock.release();
}


}
