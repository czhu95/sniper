#include "global_memory_manager.h"
#include "gmm_core.h"
#include "policy.h"
#include "core_manager.h"
#include "cache_base.h"
#include "dram_cache.h"
#include "simulator.h"
#include "log.h"
#include "dvfs_manager.h"
#include "itostr.h"
#include "instruction.h"
#include "config.hpp"
#include "distribution.h"
#include "topology_info.h"
#include "directory_msi_policy.h"
#include "replication_policy.h"

#if 0
   extern Lock iolock;
#  include "core_manager.h"
#  include "simulator.h"
#  define MYLOG(...) { ScopedLock l(iolock); fflush(stderr); fprintf(stderr, "[%s] %d%cmm %-25s@%03u: ", itostr(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD)).c_str(), getCore()->getId(), Sim()->getCoreManager()->amiUserThread() ? '^' : '_', __FUNCTION__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); fflush(stderr); }
#else
#  define MYLOG(...) {}
#endif


namespace SingleLevelMemory
{

using ParametricDramDirectoryMSI::CacheParameters;

std::map<CoreComponentType, VirtCacheCntlr*> GlobalMemoryManager::m_all_cache_cntlrs;

GlobalMemoryManager::GlobalMemoryManager(Core* core,
      Network* network, ShmemPerfModel* shmem_perf_model):
   MemoryManagerBase(core, network, shmem_perf_model),
   m_dram_cache(NULL),
   m_gmm_core(NULL),
   m_dram_cntlr(NULL),
   m_gmm_present(false),
   m_dram_cntlr_present(false),
   m_enabled(false),
   m_default_policy(NULL)
{
   // Read Parameters from the Config file
   std::map<MemComponent::component_t, CacheParameters> cache_parameters;
   std::map<MemComponent::component_t, String> cache_names;

   bool nuca_enable = false;
   CacheParameters nuca_parameters;

   const ComponentPeriod *global_domain = Sim()->getDvfsManager()->getGlobalDomain();

   UInt32 smt_cores;
   bool dram_direct_access = false;
   UInt32 dram_directory_total_entries = 0;
   UInt32 dram_directory_associativity = 0;
   UInt32 dram_directory_max_num_sharers = 0;
   UInt32 dram_directory_max_hw_sharers = 0;
   String dram_directory_type_str;
   UInt32 dram_directory_home_lookup_param = 0;
   ComponentLatency dram_directory_cache_access_time(global_domain, 0);

   try
   {
      m_cache_block_size = Sim()->getCfg()->getInt("perf_model/l1_icache/cache_block_size");

      m_last_level_cache = (MemComponent::component_t)(Sim()->getCfg()->getInt("perf_model/cache/levels") - 2 + MemComponent::L2_CACHE);

      smt_cores = Sim()->getCfg()->getInt("perf_model/core/logical_cpus");

      for(UInt32 i = MemComponent::FIRST_LEVEL_CACHE; i <= (UInt32)m_last_level_cache; ++i)
      {
         String configName, objectName;
         switch((MemComponent::component_t)i) {
            case MemComponent::L1_ICACHE:
               configName = "l1_icache";
               objectName = "L1-I";
               break;
            case MemComponent::L1_DCACHE:
               configName = "l1_dcache";
               objectName = "L1-D";
               break;
            default:
               String level = itostr(i - MemComponent::L2_CACHE + 2);
               configName = "l" + level + "_cache";
               objectName = "L" + level;
               break;
         }

         const ComponentPeriod *clock_domain = NULL;
         String domain_name = Sim()->getCfg()->getStringArray("perf_model/" + configName + "/dvfs_domain", core->getId());
         if (domain_name == "core")
            clock_domain = core->getDvfsDomain();
         else if (domain_name == "global")
            clock_domain = global_domain;
         else
            LOG_PRINT_ERROR("dvfs_domain %s is invalid", domain_name.c_str());

         LOG_ASSERT_ERROR(Sim()->getCfg()->getInt("perf_model/" + configName + "/cache_block_size") == m_cache_block_size,
                          "The cache block size of the %s is not the same as the l1_icache (%d)", configName.c_str(), m_cache_block_size);

         cache_parameters[(MemComponent::component_t)i] = CacheParameters(
            configName,
            Sim()->getCfg()->getIntArray(   "perf_model/" + configName + "/cache_size", core->getId()),
            Sim()->getCfg()->getIntArray(   "perf_model/" + configName + "/associativity", core->getId()),
            getCacheBlockSize(),
            Sim()->getCfg()->getStringArray("perf_model/" + configName + "/address_hash", core->getId()),
            Sim()->getCfg()->getStringArray("perf_model/" + configName + "/replacement_policy", core->getId()),
            Sim()->getCfg()->getBoolArray(  "perf_model/" + configName + "/perfect", core->getId()),
            i == MemComponent::L1_ICACHE
               ? Sim()->getCfg()->getBoolArray(  "perf_model/" + configName + "/coherent", core->getId())
               : true,
            ComponentLatency(clock_domain, Sim()->getCfg()->getIntArray("perf_model/" + configName + "/data_access_time", core->getId())),
            ComponentLatency(clock_domain, Sim()->getCfg()->getIntArray("perf_model/" + configName + "/tags_access_time", core->getId())),
            ComponentLatency(clock_domain, Sim()->getCfg()->getIntArray("perf_model/" + configName + "/writeback_time", core->getId())),
            ComponentBandwidthPerCycle(clock_domain,
               i < (UInt32)m_last_level_cache
                  ? Sim()->getCfg()->getIntArray("perf_model/" + configName + "/next_level_read_bandwidth", core->getId())
                  : 0),
            Sim()->getCfg()->getStringArray("perf_model/" + configName + "/perf_model_type", core->getId()),
            Sim()->getCfg()->getBoolArray(  "perf_model/" + configName + "/writethrough", core->getId()),
            Sim()->getCfg()->getIntArray(   "perf_model/" + configName + "/shared_cores", core->getId()) * smt_cores,
            Sim()->getCfg()->getStringArray("perf_model/" + configName + "/prefetcher", core->getId()),
            i == MemComponent::L1_DCACHE
               ? Sim()->getCfg()->getIntArray(   "perf_model/" + configName + "/outstanding_misses", core->getId())
               : 0
         );
         cache_names[(MemComponent::component_t)i] = objectName;

         /* Non-application threads will be distributed at 1 per process, probably not as shared_cores per process.
            Still, they need caches (for inter-process data communication, not for modeling target timing).
            Make them non-shared so we don't create process-spanning shared caches. */
         if (getCore()->getId() >= (core_id_t) Sim()->getConfig()->getApplicationCores())
            cache_parameters[(MemComponent::component_t)i].shared_cores = 1;
      }

      nuca_enable = Sim()->getCfg()->getBoolArray(  "perf_model/nuca/enabled", core->getId());
      if (nuca_enable)
      {
         nuca_parameters = CacheParameters(
            "nuca",
            Sim()->getCfg()->getIntArray(   "perf_model/nuca/cache_size", core->getId()),
            Sim()->getCfg()->getIntArray(   "perf_model/nuca/associativity", core->getId()),
            getCacheBlockSize(),
            Sim()->getCfg()->getStringArray("perf_model/nuca/address_hash", core->getId()),
            Sim()->getCfg()->getStringArray("perf_model/nuca/replacement_policy", core->getId()),
            false, true,
            ComponentLatency(global_domain, Sim()->getCfg()->getIntArray("perf_model/nuca/data_access_time", core->getId())),
            ComponentLatency(global_domain, Sim()->getCfg()->getIntArray("perf_model/nuca/tags_access_time", core->getId())),
            ComponentLatency(global_domain, 0), ComponentBandwidthPerCycle(global_domain, 0), "", false, 0, "", 0 // unused
         );
      }

      // Dram Directory Cache
      dram_directory_total_entries = Sim()->getCfg()->getInt("perf_model/dram_directory/total_entries");
      dram_directory_associativity = Sim()->getCfg()->getInt("perf_model/dram_directory/associativity");
      dram_directory_max_num_sharers = Sim()->getConfig()->getTotalCores();
      dram_directory_max_hw_sharers = Sim()->getCfg()->getInt("perf_model/dram_directory/max_hw_sharers");
      dram_directory_type_str = Sim()->getCfg()->getString("perf_model/dram_directory/directory_type");
      dram_directory_home_lookup_param = Sim()->getCfg()->getInt("perf_model/dram_directory/home_lookup_param");
      dram_directory_cache_access_time = ComponentLatency(global_domain, Sim()->getCfg()->getInt("perf_model/dram_directory/directory_cache_access_time"));

      // Dram Cntlr
      dram_direct_access = Sim()->getCfg()->getBool("perf_model/dram/direct_access");
   }
   catch(...)
   {
      LOG_PRINT_ERROR("Error reading memory system parameters from the config file");
   }

   m_user_thread_sem = new Semaphore(0);
   m_network_thread_sem = new Semaphore(0);

   m_core_list_with_dram_controllers = getCoreListWithMemoryControllers();
   String gmm_locations = Sim()->getCfg()->getString("perf_model/dram_directory/locations");

   m_core_list_with_gmm = m_core_list_with_dram_controllers;
   m_gmm_home_lookup = new AddressHomeLookup(dram_directory_home_lookup_param, m_core_list_with_gmm, getCacheBlockSize());
   m_dram_controller_home_lookup = new AddressHomeLookup(dram_directory_home_lookup_param, m_core_list_with_dram_controllers, getCacheBlockSize());

   // if (m_core->getId() == 0)
   //   printCoreListWithMemoryControllers(m_core_list_with_dram_controllers);

   if (find(m_core_list_with_dram_controllers.begin(), m_core_list_with_dram_controllers.end(), getCore()->getId()) != m_core_list_with_dram_controllers.end())
   {
      m_dram_cntlr_present = true;

      m_dram_cntlr = new PrL1PrL2DramDirectoryMSI::DramCntlr(this,
            getShmemPerfModel(),
            getCacheBlockSize());
      Sim()->getStatsManager()->logTopology("dram-cntlr", core->getId(), core->getId());

      if (Sim()->getCfg()->getBoolArray("perf_model/dram/cache/enabled", core->getId()))
      {
         m_dram_cache = new DramCache(this, getShmemPerfModel(), m_dram_controller_home_lookup, getCacheBlockSize(), m_dram_cntlr);
         Sim()->getStatsManager()->logTopology("dram-cache", core->getId(), core->getId());
      }
   }

   if (find(m_core_list_with_gmm.begin(), m_core_list_with_gmm.end(), getCore()->getId()) != m_core_list_with_gmm.end())
   {
      m_gmm_present = true;

      if (!dram_direct_access)
      {
         m_default_policy = new DirectoryMSIPolicy(getCore()->getId(),
               this,
               m_dram_controller_home_lookup,
               dram_directory_total_entries,
               dram_directory_associativity,
               getCacheBlockSize(),
               dram_directory_max_num_sharers,
               dram_directory_max_hw_sharers,
               dram_directory_type_str,
               dram_directory_cache_access_time,
               getShmemPerfModel());
         m_gmm_core = new GMMCore(getCore()->getId(), this, getShmemPerfModel());
         Sim()->getStatsManager()->logTopology("gmm-core", core->getId(), core->getId());
      }
   }

   for(UInt32 i = MemComponent::FIRST_LEVEL_CACHE; i <= (UInt32)m_last_level_cache; ++i) {
      VirtCacheCntlr* cache_cntlr = new VirtCacheCntlr(
         (MemComponent::component_t)i,
         cache_names[(MemComponent::component_t)i],
         getCore()->getId(),
         this,
         m_gmm_home_lookup,
         m_user_thread_sem,
         m_network_thread_sem,
         getCacheBlockSize(),
         cache_parameters[(MemComponent::component_t)i],
         getShmemPerfModel(),
         i == (UInt32)m_last_level_cache
      );
      m_cache_cntlrs[(MemComponent::component_t)i] = cache_cntlr;
      setCacheCntlrAt(getCore()->getId(), (MemComponent::component_t)i, cache_cntlr);
   }

   m_cache_cntlrs[MemComponent::L1_ICACHE]->setNextCacheCntlr(m_cache_cntlrs[MemComponent::L2_CACHE]);
   m_cache_cntlrs[MemComponent::L1_DCACHE]->setNextCacheCntlr(m_cache_cntlrs[MemComponent::L2_CACHE]);
   for(UInt32 i = MemComponent::L2_CACHE; i <= (UInt32)m_last_level_cache - 1; ++i)
      m_cache_cntlrs[(MemComponent::component_t)i]->setNextCacheCntlr(m_cache_cntlrs[(MemComponent::component_t)(i + 1)]);

   CacheCntlrList prev_cache_cntlrs;
   prev_cache_cntlrs.push_back(m_cache_cntlrs[MemComponent::L1_ICACHE]);
   prev_cache_cntlrs.push_back(m_cache_cntlrs[MemComponent::L1_DCACHE]);
   m_cache_cntlrs[MemComponent::L2_CACHE]->setPrevCacheCntlrs(prev_cache_cntlrs);

   for(UInt32 i = MemComponent::L2_CACHE; i <= (UInt32)m_last_level_cache - 1; ++i) {
      CacheCntlrList prev_cache_cntlrs;
      prev_cache_cntlrs.push_back(m_cache_cntlrs[(MemComponent::component_t)i]);
      m_cache_cntlrs[(MemComponent::component_t)(i + 1)]->setPrevCacheCntlrs(prev_cache_cntlrs);
   }

   // Create Performance Models
   for(UInt32 i = MemComponent::FIRST_LEVEL_CACHE; i <= (UInt32)m_last_level_cache; ++i)
      m_cache_perf_models[(MemComponent::component_t)i] = CachePerfModel::create(
       cache_parameters[(MemComponent::component_t)i].perf_model_type,
       cache_parameters[(MemComponent::component_t)i].data_access_time,
       cache_parameters[(MemComponent::component_t)i].tags_access_time
      );


   if (m_dram_cntlr_present)
      LOG_ASSERT_ERROR(m_cache_cntlrs[m_last_level_cache]->isMasterCache() == true,
                       "DRAM controllers may only be at 'master' node of shared caches\n"
                       "\n"
                       "Make sure perf_model/dram/controllers_interleaving is a multiple of perf_model/l%d_cache/shared_cores\n",
                       Sim()->getCfg()->getInt("perf_model/cache/levels")
                      );
   if (m_gmm_present)
      LOG_ASSERT_ERROR(m_cache_cntlrs[m_last_level_cache]->isMasterCache() == true,
                       "GMM may only be at 'master' node of shared caches\n"
                       "\n"
                       "Make sure perf_model/dram_directory/interleaving is a multiple of perf_model/l%d_cache/shared_cores\n",
                       Sim()->getCfg()->getInt("perf_model/cache/levels")
                      );


   // The core id to use when sending messages to the directory (master node of the last-level cache)
   m_core_id_master = getCore()->getId() - getCore()->getId() % cache_parameters[m_last_level_cache].shared_cores;

   if (m_core_id_master == getCore()->getId())
   {
      UInt32 num_sets = cache_parameters[MemComponent::L1_DCACHE].num_sets;
      // With heterogeneous caches, or fancy hash functions, we can no longer be certain that operations
      // only have effect within a set as we see it. Turn of optimization...
      if (num_sets != (1UL << floorLog2(num_sets)))
         num_sets = 1;
      for(core_id_t core_id = 0; core_id < (core_id_t)Sim()->getConfig()->getApplicationCores(); ++core_id)
      {
         if (Sim()->getCfg()->getIntArray("perf_model/l1_dcache/cache_size", core_id) != cache_parameters[MemComponent::L1_DCACHE].size)
            num_sets = 1;
         if (Sim()->getCfg()->getStringArray("perf_model/l1_dcache/address_hash", core_id) != "mask")
            num_sets = 1;
         // FIXME: We really should check all cache levels
      }

      m_cache_cntlrs[(UInt32)m_last_level_cache]->createSetLocks(
         getCacheBlockSize(),
         num_sets,
         m_core_id_master,
         cache_parameters[m_last_level_cache].shared_cores
      );
      if (dram_direct_access && getCore()->getId() < (core_id_t)Sim()->getConfig()->getApplicationCores())
      {
         LOG_ASSERT_ERROR(Sim()->getConfig()->getApplicationCores() <= cache_parameters[m_last_level_cache].shared_cores, "DRAM direct access is only possible when there is just a single last-level cache (LLC level %d shared by %d, num cores %d)", m_last_level_cache, cache_parameters[m_last_level_cache].shared_cores, Sim()->getConfig()->getApplicationCores());
         LOG_ASSERT_ERROR(m_dram_cntlr != NULL, "I'm supposed to have direct access to a DRAM controller, but there isn't one at this node");
         m_cache_cntlrs[(UInt32)m_last_level_cache]->setDRAMDirectAccess(
            m_dram_cache ? (DramCntlrInterface*)m_dram_cache : (DramCntlrInterface*)m_dram_cntlr,
            Sim()->getCfg()->getInt("perf_model/llc/evict_buffers"));
      }
   }

   // Register Call-backs
   getNetwork()->registerCallback(SHARED_MEM_1, MemoryManagerNetworkCallback, this);
   getNetwork()->registerCallback(SLME_MAGIC, MemoryManagerNetworkCallback, this);

   // Set up core topology information
   getCore()->getTopologyInfo()->setup(smt_cores, cache_parameters[m_last_level_cache].shared_cores);
}

GlobalMemoryManager::~GlobalMemoryManager()
{
   UInt32 i;

   getNetwork()->unregisterCallback(SHARED_MEM_1);
   getNetwork()->unregisterCallback(SLME_MAGIC);

   // Delete the Models

   for(i = MemComponent::FIRST_LEVEL_CACHE; i <= (UInt32)m_last_level_cache; ++i)
   {
      delete m_cache_perf_models[(MemComponent::component_t)i];
      m_cache_perf_models[(MemComponent::component_t)i] = NULL;
   }

   delete m_user_thread_sem;
   delete m_network_thread_sem;
   delete m_gmm_home_lookup;
   delete m_dram_controller_home_lookup;

   for(i = MemComponent::FIRST_LEVEL_CACHE; i <= (UInt32)m_last_level_cache; ++i)
   {
      delete m_cache_cntlrs[(MemComponent::component_t)i];
      m_cache_cntlrs[(MemComponent::component_t)i] = NULL;
   }

   if (m_dram_cache)
      delete m_dram_cache;
   if (m_dram_cntlr)
      delete m_dram_cntlr;
   if (m_gmm_core)
      delete m_gmm_core;

   if (m_default_policy)
      delete m_default_policy;

   for (auto& segment: m_segment_table)
   {
      if (segment.m_policy)
         delete segment.m_policy;
   }
}

HitWhere::where_t
GlobalMemoryManager::coreInitiateMemoryAccess(
      MemComponent::component_t mem_component,
      Core::lock_signal_t lock_signal,
      Core::mem_op_t mem_op_type,
      IntPtr address, UInt32 offset,
      Byte* data_buf, UInt32 data_length,
      Core::MemModeled modeled)
{
   LOG_ASSERT_ERROR(mem_component <= m_last_level_cache,
      "Error: invalid mem_component (%d) for coreInitiateMemoryAccess", mem_component);

   return m_cache_cntlrs[mem_component]->processMemOpFromCore(
         lock_signal,
         mem_op_type,
         address, offset,
         data_buf, data_length,
         modeled == Core::MEM_MODELED_NONE || modeled == Core::MEM_MODELED_COUNT ? false : true,
         modeled == Core::MEM_MODELED_NONE ? false : true);
}

void
GlobalMemoryManager::sendMsg(PrL1PrL2DramDirectoryMSI::ShmemMsg::msg_t msg_type, MemComponent::component_t sender_mem_component, MemComponent::component_t receiver_mem_component, core_id_t requester, core_id_t receiver, IntPtr vaddr, Byte* data_buf, UInt32 data_length, HitWhere::where_t where, ShmemPerf *perf, ShmemPerfModel::Thread_t thread_num)
{
   sendMsg(msg_type, sender_mem_component, receiver_mem_component, requester, receiver, vaddr, INVALID_ADDRESS, data_buf, data_length, where, perf, thread_num);
}

void
GlobalMemoryManager::sendMsg(PrL1PrL2DramDirectoryMSI::ShmemMsg::msg_t msg_type, MemComponent::component_t sender_mem_component, MemComponent::component_t receiver_mem_component, core_id_t requester, core_id_t receiver, IntPtr vaddr, IntPtr paddr, Byte* data_buf, UInt32 data_length, HitWhere::where_t where, ShmemPerf *perf, ShmemPerfModel::Thread_t thread_num)
{
MYLOG("send msg %u %ul%u > %ul%u", msg_type, requester, sender_mem_component, receiver, receiver_mem_component);
   assert((data_buf == NULL) == (data_length == 0));
   bool send_magic = msg_type != PrL1PrL2DramDirectoryMSI::ShmemMsg::UPGRADE_REQ;

   // bool send_magic = false;

   ShmemMsg shmem_msg(msg_type, sender_mem_component, receiver_mem_component, requester, vaddr, paddr, data_buf, data_length, perf);
   shmem_msg.setWhere(where);

   Byte* msg_buf = shmem_msg.makeMsgBuf();
   SubsecondTime msg_time = getShmemPerfModel()->getElapsedTime(thread_num);
   perf->updateTime(msg_time);

   if (m_enabled)
   {
      LOG_PRINT("Sending Msg: type(%u), address(0x%x), sender_mem_component(%u), receiver_mem_component(%u), requester(%i), sender(%i), receiver(%i)", msg_type, vaddr, sender_mem_component, receiver_mem_component, requester, getCore()->getId(), receiver);
   }

   NetPacket packet(msg_time, send_magic ? SLME_MAGIC : SHARED_MEM_1,
         m_core_id_master, receiver,
         shmem_msg.getMsgLen(), (const void*) msg_buf);
   getNetwork()->netSend(packet);

   // Delete the Msg Buf
   delete [] msg_buf;
}

void
GlobalMemoryManager::broadcastMsg(PrL1PrL2DramDirectoryMSI::ShmemMsg::msg_t msg_type, MemComponent::component_t sender_mem_component, MemComponent::component_t receiver_mem_component, core_id_t requester, IntPtr vaddr, Byte* data_buf, UInt32 data_length, ShmemPerf *perf, ShmemPerfModel::Thread_t thread_num)
{
   broadcastMsg(msg_type, sender_mem_component, receiver_mem_component, requester, vaddr, INVALID_ADDRESS, data_buf, data_length, perf, thread_num);
}

void
GlobalMemoryManager::broadcastMsg(PrL1PrL2DramDirectoryMSI::ShmemMsg::msg_t msg_type, MemComponent::component_t sender_mem_component, MemComponent::component_t receiver_mem_component, core_id_t requester, IntPtr vaddr, IntPtr paddr, Byte* data_buf, UInt32 data_length, ShmemPerf *perf, ShmemPerfModel::Thread_t thread_num)
{
MYLOG("bcast msg");
   assert((data_buf == NULL) == (data_length == 0));
   ShmemMsg shmem_msg(msg_type, sender_mem_component, receiver_mem_component, requester, vaddr, paddr, data_buf, data_length, perf);

   Byte* msg_buf = shmem_msg.makeMsgBuf();
   SubsecondTime msg_time = getShmemPerfModel()->getElapsedTime(thread_num);
   perf->updateTime(msg_time);

   if (m_enabled)
   {
      LOG_PRINT("Sending Msg: type(%u), address(0x%x), sender_mem_component(%u), receiver_mem_component(%u), requester(%i), sender(%i), receiver(%i)", msg_type, vaddr, sender_mem_component, receiver_mem_component, requester, getCore()->getId(), NetPacket::BROADCAST);
   }

   NetPacket packet(msg_time, SHARED_MEM_1,
         m_core_id_master, NetPacket::BROADCAST,
         shmem_msg.getMsgLen(), (const void*) msg_buf);
   getNetwork()->netSend(packet);

   // Delete the Msg Buf
   delete [] msg_buf;
}

SubsecondTime
GlobalMemoryManager::getCost(MemComponent::component_t mem_component, CachePerfModel::CacheAccess_t access_type)
{
   if (mem_component == MemComponent::INVALID_MEM_COMPONENT)
      return SubsecondTime::Zero();

   return m_cache_perf_models[mem_component]->getLatency(access_type);
}

void
GlobalMemoryManager::incrElapsedTime(SubsecondTime latency, ShmemPerfModel::Thread_t thread_num)
{
   MYLOG("cycles += %s", itostr(latency).c_str());
   getShmemPerfModel()->incrElapsedTime(latency, thread_num);
}

void
GlobalMemoryManager::incrElapsedTime(MemComponent::component_t mem_component, CachePerfModel::CacheAccess_t access_type, ShmemPerfModel::Thread_t thread_num)
{
   incrElapsedTime(getCost(mem_component, access_type), thread_num);
}

void
GlobalMemoryManager::enableModels()
{
   m_enabled = true;

   for(UInt32 i = MemComponent::FIRST_LEVEL_CACHE; i <= (UInt32)m_last_level_cache; ++i)
   {
      m_cache_cntlrs[(MemComponent::component_t)i]->enable();
      m_cache_perf_models[(MemComponent::component_t)i]->enable();
   }

   if (m_dram_cntlr_present)
      m_dram_cntlr->getDramPerfModel()->enable();
}

void
GlobalMemoryManager::disableModels()
{
   m_enabled = false;

   for(UInt32 i = MemComponent::FIRST_LEVEL_CACHE; i <= (UInt32)m_last_level_cache; ++i)
   {
      m_cache_cntlrs[(MemComponent::component_t)i]->disable();
      m_cache_perf_models[(MemComponent::component_t)i]->disable();
   }

   if (m_dram_cntlr_present)
      m_dram_cntlr->getDramPerfModel()->disable();
}


void
GlobalMemoryManager::handleMsgFromNetwork(NetPacket& packet)
{
MYLOG("begin");
   core_id_t sender = packet.sender;
   ShmemMsg* shmem_msg = ShmemMsg::getShmemMsg((Byte*) packet.data, &m_dummy_shmem_perf);
   SubsecondTime msg_time = packet.time;

   getShmemPerfModel()->setElapsedTime(ShmemPerfModel::_SIM_THREAD, msg_time);
   shmem_msg->getPerf()->updatePacket(packet);

   MemComponent::component_t receiver_mem_component = shmem_msg->getReceiverMemComponent();
   MemComponent::component_t sender_mem_component = shmem_msg->getSenderMemComponent();

   if (m_enabled)
   {
      LOG_PRINT("Got Shmem Msg: type(%i), address(0x%x), sender_mem_component(%u), receiver_mem_component(%u), sender(%i), receiver(%i)",
            shmem_msg->getMsgType(), shmem_msg->getAddress(), sender_mem_component, receiver_mem_component, sender, packet.receiver);
   }

   switch (receiver_mem_component)
   {
      case MemComponent::L2_CACHE: /* PrL1PrL2DramDirectoryMSI::DramCntlr sends to L2 and doesn't know about our other levels */
      case MemComponent::LAST_LEVEL_CACHE:
         switch(sender_mem_component)
         {
            case MemComponent::GMM:
               m_cache_cntlrs[m_last_level_cache]->handleMsgFromGMM(sender, shmem_msg);
               break;

            default:
               LOG_PRINT_ERROR("Unrecognized sender component(%u)",
                     sender_mem_component);
               break;
         }
         break;

      case MemComponent::GMM:
         LOG_ASSERT_ERROR(m_gmm_present, "GMM NOT present");

         switch(sender_mem_component)
         {
            case MemComponent::LAST_LEVEL_CACHE:
            case MemComponent::DRAM:
            case MemComponent::GMM:
               m_gmm_core->handleMsgFromNetwork(sender, shmem_msg);
               break;

            default:
               LOG_PRINT_ERROR("Unrecognized sender component(%u)",
                     sender_mem_component);
               break;
         }
         break;

      case MemComponent::DRAM:
         LOG_ASSERT_ERROR(m_dram_cntlr_present, "Dram Cntlr NOT present");

         switch(sender_mem_component)
         {
            case MemComponent::GMM:
            {
               DramCntlrInterface* dram_interface = m_dram_cache ? (DramCntlrInterface*)m_dram_cache : (DramCntlrInterface*)m_dram_cntlr;
               dram_interface->handleMsgFromGMM(sender, shmem_msg);
               break;
            }

            default:
               LOG_PRINT_ERROR("Unrecognized sender component(%u)",
                     sender_mem_component);
               break;
         }
         break;

      default:
         LOG_PRINT_ERROR("Unrecognized receiver component(%u)",
               receiver_mem_component);
         break;
   }

   // Delete the allocated Shared Memory Message
   // First delete 'data_buf' if it is present
   // LOG_PRINT("Finished handling Shmem Msg");

   if (shmem_msg->getDataLength() > 0)
   {
      assert(shmem_msg->getDataBuf());
      delete [] shmem_msg->getDataBuf();
   }
   delete shmem_msg;
MYLOG("end");
}

PolicyBase*
GlobalMemoryManager::policyLookup(IntPtr address)
{
   PolicyBase *policy = m_default_policy;
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
GlobalMemoryManager::Command(uint64_t cmd_type, IntPtr start, uint64_t arg1)
{
   if (cmd_type == 0)
      createSegment(start, arg1);
   else if (cmd_type == 1)
      segmentAssignPolicy(start, arg1);
}

void
GlobalMemoryManager::createSegment(IntPtr start, uint64_t length)
{
   if (find(m_core_list_with_gmm.begin(), m_core_list_with_gmm.end(), getCore()->getId()) == m_core_list_with_gmm.end())
      return;

   Segment new_seg{0, start, start + length, NULL};
   m_segment_table_lock.acquire();

   for (const auto& seg : m_segment_table)
      assert(seg != new_seg);

   m_segment_table.push_back(new_seg);
   m_segment_table_lock.release();


   MYLOG("Created segment: %p - %p", (void *)new_seg.m_start, (void *)new_seg.m_end);

}

void
GlobalMemoryManager::segmentAssignPolicy(IntPtr start, uint64_t policy_id)
{
   if (find(m_core_list_with_gmm.begin(), m_core_list_with_gmm.end(), getCore()->getId()) == m_core_list_with_gmm.end())
      return;

   m_segment_table_lock.acquire();
   for (auto& seg : m_segment_table)
   {
      if (seg.m_start == start)
      {
         seg.m_policy = new ReplicationPolicy(getCore()->getId(),
               this,
               m_dram_controller_home_lookup,
               1024 * 1024,
               getShmemPerfModel());

         MYLOG("Segment assign policy: %p - %d", (void *)seg.m_start, policy_id);
      }
   }
   m_segment_table_lock.release();
}

}
