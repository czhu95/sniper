#pragma once

#include "memory_manager_base.h"
#include "cache_base.h"
#include "virt_cache_cntlr.h"
#include "../pr_l1_pr_l2_dram_directory_msi/dram_directory_cntlr.h"
#include "../pr_l1_pr_l2_dram_directory_msi/dram_cntlr.h"
#include "address_home_lookup.h"
#include "shmem_msg.h"
#include "mem_component.h"
#include "semaphore.h"
#include "fixed_types.h"
#include "shmem_perf_model.h"
#include "shared_cache_block_info.h"
#include "subsecond_time.h"
#include "lock.h"

#include <map>
#include <vector>

class DramCache;

namespace SingleLevelMemory
{
   class GMMCoreFast;
   class Segment;

   typedef std::pair<core_id_t, MemComponent::component_t> CoreComponentType;
   typedef std::map<CoreComponentType, VirtCacheCntlr*> CacheCntlrMap;

   class GlobalMemoryManager : public MemoryManagerBase
   {
      protected:
         VirtCacheCntlr* m_cache_cntlrs[MemComponent::LAST_LEVEL_CACHE + 1];
         DramCache* m_dram_cache;
         GMMCoreFast* m_gmm_core;
         PrL1PrL2DramDirectoryMSI::DramCntlr* m_dram_cntlr;
         PrL1PrL2DramDirectoryMSI::DramCntlr* m_secondary_dram_cntlr;
         AddressHomeLookup* m_dram_controller_home_lookup;

         core_id_t m_core_id_master;

         bool m_gmm_present;
         bool m_dram_cntlr_present;

         Semaphore* m_user_thread_sem;
         Semaphore* m_network_thread_sem;

         UInt32 m_cache_block_size;
         MemComponent::component_t m_last_level_cache;
         bool m_enabled;

         ShmemPerf m_dummy_shmem_perf;

         UInt32 m_shared_cores;

         // Performance Models
         CachePerfModel* m_cache_perf_models[MemComponent::LAST_LEVEL_CACHE + 1];

         // Global map of all caches on all cores (within this process!)
         static CacheCntlrMap m_all_cache_cntlrs;

         std::vector<core_id_t> m_core_list_with_dram_controllers;
         std::vector<core_id_t> m_core_list_with_gmm;

      public:
         GlobalMemoryManager(Core* core, Network* network, ShmemPerfModel* shmem_perf_model);

         ~GlobalMemoryManager();

         UInt64 getCacheBlockSize() const { return m_cache_block_size; }

         Cache* getCache(MemComponent::component_t mem_component) {
              return m_cache_cntlrs[mem_component == MemComponent::LAST_LEVEL_CACHE ? MemComponent::component_t(m_last_level_cache) : mem_component]->getCache();
         }
         Cache* getL1ICache() { return getCache(MemComponent::L1_ICACHE); }
         Cache* getL1DCache() { return getCache(MemComponent::L1_DCACHE); }
         Cache* getLastLevelCache() { return getCache(MemComponent::LAST_LEVEL_CACHE); }
         PrL1PrL2DramDirectoryMSI::DramCntlr* getDramCntlr() { return m_dram_cntlr; }
         AddressHomeLookup* getDramControllerHomeLookup() { return m_dram_controller_home_lookup; }

         VirtCacheCntlr* getCacheCntlrAt(core_id_t core_id, MemComponent::component_t mem_component) { return m_all_cache_cntlrs[CoreComponentType(core_id, mem_component)]; }
         void setCacheCntlrAt(core_id_t core_id, MemComponent::component_t mem_component, VirtCacheCntlr* cache_cntlr) { m_all_cache_cntlrs[CoreComponentType(core_id, mem_component)] = cache_cntlr; }

         HitWhere::where_t coreInitiateMemoryAccess(
               MemComponent::component_t mem_component,
               Core::lock_signal_t lock_signal,
               Core::mem_op_t mem_op_type,
               IntPtr address, UInt32 offset,
               Byte* data_buf, UInt32 data_length,
               Core::MemModeled modeled) { return coreInitiateMemoryAccess(mem_component, lock_signal, mem_op_type, address, offset, 0, data_buf, data_length, modeled); }

         HitWhere::where_t coreInitiateMemoryAccess(
               MemComponent::component_t mem_component,
               Core::lock_signal_t lock_signal,
               Core::mem_op_t mem_op_type,
               IntPtr address, UInt32 offset,
               IntPtr user_thread,
               Byte* data_buf, UInt32 data_length,
               Core::MemModeled modeled);

         void handleMsgFromNetwork(NetPacket& packet) override;

         void sendMsg(ShmemMsg::msg_t msg_type, MemComponent::component_t sender_mem_component, MemComponent::component_t receiver_mem_component, core_id_t requester, core_id_t receiver, IntPtr vaddr, Byte* data_buf = NULL, UInt32 data_length = 0, HitWhere::where_t where = HitWhere::UNKNOWN, ShmemPerf *perf = NULL, ShmemPerfModel::Thread_t thread_num = ShmemPerfModel::NUM_CORE_THREADS) override;
         void sendMsg(ShmemMsg::msg_t msg_type, MemComponent::component_t sender_mem_component, MemComponent::component_t receiver_mem_component, core_id_t requester, core_id_t receiver, IntPtr vaddr, IntPtr paddr, Byte* data_buf = NULL, UInt32 data_length = 0, HitWhere::where_t where = HitWhere::UNKNOWN, ShmemPerf *perf = NULL, ShmemPerfModel::Thread_t thread_num = ShmemPerfModel::NUM_CORE_THREADS);
         void sendMsg(ShmemMsg::msg_t msg_type, MemComponent::component_t sender_mem_component, MemComponent::component_t receiver_mem_component, core_id_t requester, core_id_t receiver, IntPtr vaddr, IntPtr paddr, IntPtr user_thread, Byte* data_buf = NULL, UInt32 data_length = 0, HitWhere::where_t where = HitWhere::UNKNOWN, ShmemPerf *perf = NULL, ShmemPerfModel::Thread_t thread_num = ShmemPerfModel::NUM_CORE_THREADS);

         void broadcastMsg(ShmemMsg::msg_t msg_type, MemComponent::component_t sender_mem_component, MemComponent::component_t receiver_mem_component, core_id_t requester, IntPtr vaddr, Byte* data_buf = NULL, UInt32 data_length = 0, ShmemPerf *perf = NULL, ShmemPerfModel::Thread_t thread_num = ShmemPerfModel::NUM_CORE_THREADS) override;
         void broadcastMsg(ShmemMsg::msg_t msg_type, MemComponent::component_t sender_mem_component, MemComponent::component_t receiver_mem_component, core_id_t requester, IntPtr vaddr, IntPtr paddr, Byte* data_buf = NULL, UInt32 data_length = 0, ShmemPerf *perf = NULL, ShmemPerfModel::Thread_t thread_num = ShmemPerfModel::NUM_CORE_THREADS);

         SubsecondTime getL1HitLatency(void) { return m_cache_perf_models[MemComponent::L1_ICACHE]->getLatency(CachePerfModel::ACCESS_CACHE_DATA_AND_TAGS); }
         void addL1Hits(bool icache, Core::mem_op_t mem_op_type, UInt64 hits) {
            (icache ? m_cache_cntlrs[MemComponent::L1_ICACHE] : m_cache_cntlrs[MemComponent::L1_DCACHE])->updateHits(mem_op_type, hits);
         }

         void enableModels();
         void disableModels();

         core_id_t getShmemRequester(const void* pkt_data)
         { return ((ShmemMsg*) pkt_data)->getRequester(); }

         UInt32 getModeledLength(const void* pkt_data)
         { return ((ShmemMsg*) pkt_data)->getModeledLength(); }

         SubsecondTime getCost(MemComponent::component_t mem_component, CachePerfModel::CacheAccess_t access_type);
         void incrElapsedTime(SubsecondTime latency, ShmemPerfModel::Thread_t thread_num = ShmemPerfModel::NUM_CORE_THREADS);
         void incrElapsedTime(MemComponent::component_t mem_component, CachePerfModel::CacheAccess_t access_type, ShmemPerfModel::Thread_t thread_num = ShmemPerfModel::NUM_CORE_THREADS);

         core_id_t getGMMFromId(core_id_t core_id);
         core_id_t getUserFromId(core_id_t core_id);
   };
}
