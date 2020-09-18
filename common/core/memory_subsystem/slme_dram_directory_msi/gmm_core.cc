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
#include "sift_format.h"
#include "instruction.h"
#include "thread.h"
#include "queue_model_history_list.h"

#if 0
   extern Lock iolock;
#  include "core_manager.h"
#  include "simulator.h"
#  define MYLOG(...) { ScopedLock l(iolock); fflush(stdout); printf("[%s] %d%cdd %-25s@%3u: ", itostr(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD)).c_str(), getMemoryManager()->getCore()->getId(), Sim()->getCoreManager()->amiUserThread() ? '^' : '_', __FUNCTION__, __LINE__); printf(__VA_ARGS__); printf("\n"); fflush(stdout); }
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

   m_dram_queue_list = new ReqQueueList();

   m_queue_model = QueueModel::create("GMM Core", m_core_id, "history_list", *global_domain);
}

GMMCore::~GMMCore()
{
   delete m_dram_controller_home_lookup;
   delete m_dram_queue_list;
   delete m_queue_model;
}

void
GMMCore::handleMsgFromNetwork(core_id_t sender, ShmemMsg* shmem_msg)
{
   IntPtr address = shmem_msg->getAddress();

   MYLOG("begin for address %lx, %d in queue", address, m_dram_queue_list->size(address));
   updateShmemPerf(shmem_msg, ShmemPerf::TD_ACCESS);

   // Look up line in segment table
   // bool slb_hit = false;

   // Look up policy
   policy_id_t policy_id;
   uint64_t seg_id;
   Sim()->getSegmentTable()->lookup(shmem_msg->getAddress(), policy_id, seg_id);

   if (policy_id != DIRECTORY_COHERENCE)
   {
      MemComponent::component_t sender_mem_component = shmem_msg->getSenderMemComponent();

      switch(sender_mem_component)
      {
         case MemComponent::LAST_LEVEL_CACHE:
         {
            // Look up TLB
            if (sender != m_core_id &&
                (shmem_msg->getMsgType() == ShmemMsg::SH_REQ || shmem_msg->getMsgType() == ShmemMsg::EX_REQ)
                && m_tlb.count({address, address + 1}) == 0)
            {
               Sift::GMMCoreMessage *core_msg = new Sift::GMMCoreMessage();
               buildGMMCoreMessage(seg_id, policy_id, sender, shmem_msg, *core_msg);
               LOG_ASSERT_ERROR(core_msg->policy != INVALID_POLICY, "Built invalid policy.");
               enqueueMessage(core_msg);
            }
            handleMsgFromL2Cache(sender, shmem_msg);
            break;
         }
         case MemComponent::CORE:
         {
            Sift::GMMCoreMessage *core_msg = new Sift::GMMCoreMessage();
            buildGMMCoreMessage(seg_id, policy_id, sender, shmem_msg, *core_msg);
            LOG_ASSERT_ERROR(core_msg->policy != INVALID_POLICY, "Built invalid policy.");
            enqueueMessage(core_msg);
            break;
         }
         case MemComponent::DRAM:
            handleMsgFromDRAM(sender, shmem_msg);
            break;
         case MemComponent::GMM_CORE:
            handleMsgFromGMM(sender, shmem_msg);
            break;
         default:
            LOG_PRINT_ERROR("Unrecognized sender component(%u)",
                  sender_mem_component);
            break;
      }

   }
   else
   {
      MemComponent::component_t sender_mem_component = shmem_msg->getSenderMemComponent();

      switch(sender_mem_component)
      {
         case MemComponent::LAST_LEVEL_CACHE:
            m_directory_policy->handleMsgFromL2Cache(sender, shmem_msg);
            break;
         case MemComponent::DRAM:
            m_directory_policy->handleMsgFromDRAM(sender, shmem_msg);
            break;
         case MemComponent::GMM_CORE:
            m_directory_policy->handleMsgFromGMM(sender, shmem_msg);
            break;

         default:
            LOG_PRINT_ERROR("Unrecognized sender component(%u)",
                  sender_mem_component);
            break;
      }
   }
}

void
GMMCore::handleMsgFromL2Cache(core_id_t sender, ShmemMsg* shmem_msg)
{
   ShmemMsg::msg_t shmem_msg_type = shmem_msg->getMsgType();
   SubsecondTime msg_time = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD);
   IntPtr va = shmem_msg->getAddress();
   IntPtr pa = shmem_msg->getPhysAddress();
   core_id_t requester = shmem_msg->getRequester();

   MYLOG("begin for address %lx, pa %lx, %d in queue, requester is %u", va, pa, m_dram_queue_list->size(va), requester);

   assert(pa != INVALID_ADDRESS);
   // if (pa == INVALID_ADDRESS)
   // {
   //    pa = Sim()->getThreadManager()->getThreadFromID(requester)->va2pa(va);
   //    shmem_msg->setPhysAddress(pa);
   // }

   switch (shmem_msg_type)
   {
      case ShmemMsg::EX_REQ:
      {
         MYLOG("E REQ<%u @ %lx", sender, va);

         policy_id_t policy_id;
         uint64_t seg_id;
         Sim()->getSegmentTable()->lookup(va, policy_id, seg_id);
         LOG_ASSERT_ERROR(policy_id == ATOMIC_UPDATE, "EX_REQ unacceptable for policy %d", policy_id);
         // LOG_ASSERT_WARNING((policy_id - 9) / 4 == m_core_id - 16, "Policy %d written by %d", policy_id - 9, m_core_id - 16);

         // Add request onto a queue
         ShmemReq* shmem_req = new ShmemReq(shmem_msg, msg_time);

         m_dram_queue_list->enqueue(va, shmem_req);
         MYLOG("ENqueued E REQ for address %lx", va );
         if (m_tlb.count({va, va + 1}) == true && m_dram_queue_list->size(va) == 1)
         {
            processExReqFromL2Cache(shmem_req);
         }
         else
         {
            MYLOG("E REQ (%lx) not handled yet because of outstanding request in the queue", va);
         }
         break;
      }
      case ShmemMsg::SH_REQ:
      {
         MYLOG("S REQ<%u @ %lx", sender, va);

         // Add request onto a queue
         ShmemReq* shmem_req = new ShmemReq(shmem_msg, msg_time);

         m_dram_queue_list->enqueue(va, shmem_req);
         MYLOG("ENqueued S REQ for address %lx", va );
         if (m_tlb.count({va, va + 1}) == true && m_dram_queue_list->size(va) == 1)
         {
            processShReqFromL2Cache(shmem_req);
         }
         else
         {
            MYLOG("S REQ (%lx) not handled because of outstanding request in the queue", va);
         }
         break;
      }

      case ShmemMsg::INV_REP:
         MYLOG("INV REP<%u @ %lx", sender, va);
         processInvRepFromL2Cache(sender, shmem_msg);
         break;

      case ShmemMsg::FLUSH_REP:
         MYLOG("FLUSH REP<%u @ %lx", sender, va);
         // processFlushRepFromL2Cache(sender, shmem_msg);
         break;

      // case ShmemMsg::WB_REP:
      //    MYLOG("WB REP<%u @ %lx", sender, va);
      //    processWbRepFromL2Cache(sender, shmem_msg);
      //    break;

      default:
         LOG_PRINT_ERROR("Unrecognized Shmem Msg Type: %u", shmem_msg_type);
         break;

      // case ShmemMsg::UPGRADE_REQ:
      //    MYLOG("UPGR REQ<%u @ %lx", sender, va);

      //    // Add request onto a queue
      //    ShmemReq* shmem_req = new ShmemReq(shmem_msg, msg_time);
      //    m_dram_queue_list->enqueue(va, shmem_req);
      //    MYLOG("ENqueued  UPGRADE REQ for address %lx",  va );

      //    if (m_dram_queue_list->size(va) == 1)
      //    {
      //       processUpgradeReqFromL2Cache(shmem_req);
      //    }
      //    else
      //    {
      //       MYLOG("UPGRADE REQ (%lx) not handled because of outstanding request in the queue", va);
      //    }

      //    break;

   }
MYLOG("done for %lx", va);
}

void
GMMCore::handleMsgFromGMM(core_id_t sender, ShmemMsg* shmem_msg)
{
   switch (shmem_msg->getMsgType())
   {
      case ShmemMsg::TLB_INSERT:
      {
         TLBEntry e = {shmem_msg->getAddress(), shmem_msg->getPhysAddress()};
         m_tlb.insert(e);
         for (auto it = m_dram_queue_list->begin(); it != m_dram_queue_list->end(); it ++)
         {
            if (e.contains(*it))
            {
               // processNextReqFromL2Cache(*it);
               ShmemReq* shmem_req = m_dram_queue_list->front(*it);

               if (shmem_req->getShmemMsg()->getMsgType() == ShmemMsg::EX_REQ)
               {
                  MYLOG("A new EX_REQ for address(%lx) found", shmem_msg->getAddress());
                  processExReqFromL2Cache(shmem_req);
               }
               else if (shmem_req->getShmemMsg()->getMsgType() == ShmemMsg::SH_REQ)
               {
                  MYLOG("A new SH_REQ for address(%lx) found", shmem_msg->getAddress());
                  processShReqFromL2Cache(shmem_req);
               }
            }
         }
         break;
      }
      case ShmemMsg::ATOMIC_UPDATE_REP:
      case ShmemMsg::ATOMIC_UPDATE_MSG:
      {
         policy_id_t policy_id;
         uint64_t seg_id;
         Sim()->getSegmentTable()->lookup(shmem_msg->getAddress(), policy_id, seg_id);
         Sift::GMMCoreMessage *core_msg = new Sift::GMMCoreMessage();
         buildGMMCoreMessage(seg_id, policy_id, sender, shmem_msg, *core_msg);
         LOG_ASSERT_ERROR(core_msg->policy != INVALID_POLICY, "Built invalid policy.");
         enqueueMessage(core_msg);
         break;
      }
      case ShmemMsg::USER_CACHE_READ_REQ:
      {
         // Add request onto a queue
         SubsecondTime msg_time = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD);
         IntPtr va = shmem_msg->getAddress();
         ShmemReq* shmem_req = new ShmemReq(shmem_msg, msg_time);

         m_dram_queue_list->enqueue(va, shmem_req);
         processShReqFromL2Cache(m_dram_queue_list->front(va));
         break;
      }
      default:
         LOG_PRINT_ERROR("Unrecognized Shmem Msg Type: %u", shmem_msg->getMsgType());
         break;

   }
   // IntPtr pa = shmem_msg->getPhysAddress();

   // if (sender != m_core_id)
   // {
   //    core_id_t home_node = m_dram_controller_home_lookup->getHome(pa);
   //    assert(home_node == m_core_id);
   // }

   // handleMsgFromL2Cache(shmem_msg->getRequester(), shmem_msg);
}

void
GMMCore::processNextReqFromL2Cache(IntPtr address)
{
   MYLOG("Start processNextReqFromL2Cache(%lx): %d in Queue", address, m_dram_queue_list->size(address) );

   MYLOG("about to dequeue request for address %lx", address );
   assert(m_dram_queue_list->size(address) >= 1);
   ShmemReq* completed_shmem_req = m_dram_queue_list->dequeue(address);
   delete completed_shmem_req;

   if (! m_dram_queue_list->empty(address))
   {
      MYLOG("A new shmem req for address(%lx) found", address);

      ShmemReq* shmem_req = m_dram_queue_list->front(address);

      // Update the Shared Mem Cycle Counts appropriately
      getShmemPerfModel()->updateElapsedTime(shmem_req->getTime(), ShmemPerfModel::_SIM_THREAD);

      if (shmem_req->getShmemMsg()->getMsgType() == ShmemMsg::EX_REQ)
      {
         MYLOG("A new EX_REQ for address(%lx) found", address);
         processExReqFromL2Cache(shmem_req);
      }
      else if (shmem_req->getShmemMsg()->getMsgType() == ShmemMsg::SH_REQ)
      {
         MYLOG("A new SH_REQ for address(%lx) found", address);
         processShReqFromL2Cache(shmem_req);
      } else
         LOG_PRINT_ERROR("Unrecognized Request(%u)", shmem_req->getShmemMsg()->getMsgType());
   }
   MYLOG("End processNextReqFromL2Cache(%lx)", address);
}

void
GMMCore::processShReqFromL2Cache(ShmemReq* shmem_req, Byte* cached_data_buf)
{
   IntPtr address = shmem_req->getShmemMsg()->getAddress();
   core_id_t requester = shmem_req->getShmemMsg()->getRequester();

   MYLOG("Start @ %lx", address);
   updateShmemPerf(shmem_req);

   if (shmem_req->getShmemMsg()->getMsgType() == ShmemMsg::USER_CACHE_READ_REQ)
      retrieveDataAndSendToGMM(ShmemMsg::USER_CACHE_READ_REQ, address, shmem_req->getShmemMsg());
   else
      retrieveDataAndSendToL2Cache(ShmemMsg::SH_REP, requester, address, NULL, shmem_req->getShmemMsg());
   MYLOG("End @ %lx", address);
}

void
GMMCore::processExReqFromL2Cache(ShmemReq* shmem_req, Byte* cached_data_buf)
{
   IntPtr address = shmem_req->getShmemMsg()->getAddress();
   core_id_t requester = shmem_req->getShmemMsg()->getRequester();

   MYLOG("Start @ %lx", address);
   updateShmemPerf(shmem_req);
   retrieveDataAndSendToL2Cache(ShmemMsg::EX_REP, requester, address, NULL, shmem_req->getShmemMsg());

   MYLOG("End @ %lx", address);
}

void
GMMCore::buildGMMCoreMessage(uint64_t segid, policy_id_t policy, core_id_t sender, ShmemMsg *shmem_msg, Sift::GMMCoreMessage &msg)
{
   LOG_ASSERT_ERROR(policy != DIRECTORY_COHERENCE, "Directory coherence maintained by hardware.");

   msg.segid = segid;
   msg.policy = policy;
   msg.sender = sender;
   msg.requester = shmem_msg->getRequester();

   MemComponent::component_t sender_mem_component = shmem_msg->getSenderMemComponent();

   switch(sender_mem_component)
   {
      case MemComponent::CORE:
         switch (policy)
         {
            case ATOMIC_SWAP:
            case ATOMIC_UPDATE:
            case SUBSCRIPTION:
               msg.type = ShmemMsg::ATOMIC_UPDATE_REQ;
               msg.payload[0] = shmem_msg->getAddress();
               msg.payload[1] = shmem_msg->getPhysAddress();
               break;
            default:
               LOG_PRINT_ERROR("Not implemented");
         }
         break;
      case MemComponent::GMM_CORE:
         switch (policy)
         {
            case ATOMIC_SWAP:
            case SUBSCRIPTION:
               msg.type = shmem_msg->getMsgType();
               msg.payload[0] = shmem_msg->getAddress();
               msg.payload[1] = shmem_msg->getPhysAddress();
               break;
            default:
               LOG_PRINT_ERROR("Not implemented");
         }
         break;
      case MemComponent::LAST_LEVEL_CACHE:
         switch (policy)
         {
            case SUBSCRIPTION:
               LOG_ASSERT_ERROR(shmem_msg->getMsgType() == ShmemMsg::INV_REP || shmem_msg->getMsgType() == ShmemMsg::SH_REQ, "Unrecognized message type.");
               msg.type = shmem_msg->getMsgType();
               msg.payload[0] = shmem_msg->getAddress();
               break;
            case REPLICATION:
            case ATOMIC_SWAP:
            case ATOMIC_UPDATE:
               msg.type = shmem_msg->getMsgType();
               msg.payload[0] = shmem_msg->getAddress();
               if (shmem_msg->getMsgType() == ShmemMsg::INV_REP)
                  msg.policy = INVALID_POLICY;
               break;
            default:
               LOG_PRINT_ERROR("Not implemented");
         }
         break;
      case MemComponent::DRAM:
         switch (policy)
         {
            case ATOMIC_SWAP:
               LOG_ASSERT_ERROR(shmem_msg->getMsgType() == ShmemMsg::DRAM_READ_REP, "Unrecognized message type.");
               msg.type = ShmemMsg::USER_CACHE_READ_REP;
               msg.payload[0] = shmem_msg->getAddress();
               msg.payload[1] = 0xBABE;
               break;
            default:
               LOG_PRINT_ERROR("Not implemented");
         }
         break;
      default:
         LOG_PRINT_ERROR("Not implemented");
   }
}


// int
// GMMCore::getId() const
// {
//    return m_core_id;
// }

void
GMMCore::enqueueMessage(Sift::GMMCoreMessage *msg)
{
   m_msg_queue_lock.acquire();

   SubsecondTime msg_time = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD);
   m_msg_queue.push_back(std::make_pair(msg, msg_time));
   if (m_msg_queue.size() == 1)
      m_msg_cond.signal();

   m_msg_queue_lock.release();
}

Sift::GMMCoreMessage *
GMMCore::dequeueMessage()
{
   Sift::GMMCoreMessage *msg;
   m_msg_queue_lock.acquire();
   while (true)
   {
      if (m_msg_queue.empty())
      {
         m_msg_queue_lock.release();
         {
            ScopedLock sl(Sim()->getThreadManager()->getLock(m_core_id));
            Sim()->getThreadManager()->stallThread_async(m_core_id, ThreadManager::STALL_GMM_PULL,
                                                         getPerformanceModel()->getElapsedTime());
         }
         LOG_ASSERT_ERROR(m_thread == NULL, "GMM thread not stalled on GMM Core.");
         getPerformanceModel()->queuePseudoInstruction(new UnknownInstruction(getDvfsDomain()->getPeriod()));
         getPerformanceModel()->iterate();

         m_msg_queue_lock.acquire();
         if (m_msg_queue.empty())
            m_msg_cond.wait(m_msg_queue_lock);

         SubsecondTime time_wake = getPerformanceModel()->getElapsedTime(); //Sim()->getClockSkewMinimizationServer()->getGlobalTime(true /*upper_bound*/);
         {
            ScopedLock sl(Sim()->getThreadManager()->getLock(m_core_id));
            Sim()->getThreadManager()->resumeThread_async(m_core_id, INVALID_THREAD_ID, time_wake, NULL);
         }
         LOG_ASSERT_ERROR(m_thread && m_thread->getCore(), "GMM thread not resumed on GMM Core.");
      }

      // Waken by signalStop
      if (m_msg_queue.empty())
      {
         m_msg_queue_lock.release();
         return NULL;
      }

      msg = m_msg_queue.front().first;
      m_dequeued_msg_time.push_back(m_msg_queue.front().second);
      m_msg_queue.pop_front();
      // if (m_dequeue_time == SubsecondTime::Zero())
      // {
      //    m_dequeue_time = m_next_msg_time;
      //    getPerformanceModel()->queuePseudoInstruction(new SyncInstruction(m_dequeue_time, SyncInstruction::SYSCALL));
      // }
      // m_dequeue_time = m_msg_time > m_dequeue_time ? m_msg_time : m_dequeue_time;

      // getShmemPerfModel()->updateElapsedTime(msg_time, ShmemPerfModel::_USER_THREAD);

      // LOG_PRINT_WARNING("[%d]dequeue time: %s, addr=%lx, type=%d", m_core_id, itostr(m_dequeue_time).c_str(), msg->payload[0], msg->type);
      break;
      // if (executeSoftwarePolicy(msg))
      //    break;
   }
   m_msg_queue_lock.release();
   return msg;
}

// bool
// GMMCore::executeSoftwarePolicy(Sift::GMMCoreMessage *msg)
// {
//    bool exec = false;
//    switch (msg->policy)
//    {
//       case REPLICATION:
//          IntPtr addr = msg->payload[0];
//          exec = m_tlb.count({addr, addr + 1}) == 0;
//          break;
//       default:
//          LOG_PRINT_ERROR("Unrecognized policy");
//    }
//
//    return exec;
// }

void
GMMCore::sendDataToDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now)
{
   MYLOG("Start @ %lx", address);

   // Write data to Dram
   core_id_t dram_node = m_core_id; // m_dram_controller_home_lookup->getHome(address);

   getGlobalMemoryManager()->sendMsg(ShmemMsg::DRAM_WRITE_REQ,
         MemComponent::GMM_CORE, MemComponent::DRAM,
         requester /* requester */,
         dram_node /* receiver */,
         INVALID_ADDRESS, /* vaddr is unused */
         address,
         data_buf, getMemoryManager()->getCacheBlockSize(),
         HitWhere::UNKNOWN,
         &m_dummy_shmem_perf,
         ShmemPerfModel::_SIM_THREAD);

   // DRAM latency is ignored on write
   MYLOG("End @ %lx", address);
}

void
GMMCore::handleMsgFromDRAM(core_id_t sender, ShmemMsg* shmem_msg)
{
   MYLOG("Start");
   ShmemMsg::msg_t shmem_msg_type = shmem_msg->getMsgType();

   switch (shmem_msg_type)
   {
      case ShmemMsg::DRAM_READ_REP:
         processDRAMReply(sender, shmem_msg);
         break;

      default:
         LOG_PRINT_ERROR("Unrecognized Shmem Msg Type: %u", shmem_msg_type);
         break;
   }
   MYLOG("End");
}

void
GMMCore::processDRAMReply(core_id_t sender, ShmemMsg* shmem_msg)
{
   IntPtr address = shmem_msg->getAddress();

   MYLOG("Start @ %lx", address);
   // Data received from DRAM

   //   Which node to reply to?

   LOG_ASSERT_ERROR(m_dram_queue_list->size(address) >= 1, "0x%lx", address);
   ShmemReq* shmem_req = m_dram_queue_list->front(address);
   updateShmemPerf(shmem_req);

   //   Which reply type to use?

   ShmemMsg::msg_t reply_msg_type;

   updateShmemPerf(shmem_req, ShmemPerf::TD_ACCESS);

   policy_id_t policy_id;
   uint64_t seg_id;
   Sim()->getSegmentTable()->lookup(address, policy_id, seg_id);
   switch(shmem_req->getShmemMsg()->getMsgType())
   {
      case ShmemMsg::EX_REQ:
         reply_msg_type = ShmemMsg::EX_REP;
         break;
      case ShmemMsg::SH_REQ:
         reply_msg_type = policy_id == ATOMIC_UPDATE ? ShmemMsg::EX_REP : ShmemMsg::SH_REP;
         break;
      // case ShmemMsg::EX_REQ:
      //    reply_msg_type = ShmemMsg::EX_REP;
      //    assert(curr_dstate == DirectoryState::MODIFIED);
      //    break;
      // case ShmemMsg::UPGRADE_REQ:
      // {
      //    // if we had to get the data from DRAM, nobody has it anymore: send EX_REP
      //    reply_msg_type = ShmemMsg::EX_REP;
      //    break;
      // }
      default:
         LOG_PRINT_ERROR("Unsupported request type: %u", shmem_req->getShmemMsg()->getMsgType());
   }

   //   Which HitWhere to report?
   HitWhere::where_t hit_where = shmem_msg->getWhere();
   if (hit_where == HitWhere::DRAM)
      hit_where = (getGlobalMemoryManager()->getUserFromId(sender) == getGlobalMemoryManager()->getUserFromId(shmem_msg->getRequester())) ? HitWhere::DRAM_LOCAL : HitWhere::DRAM_REMOTE;

   if (hit_where == HitWhere::DRAM_REMOTE)
   {
      LOG_PRINT_WARNING("GMM Remote DRAM access: va = %p, %d->%d", address, sender, shmem_msg->getRequester());
   }
   // else
   // {
   //    LOG_PRINT_WARNING("Directory Policy: Local DRAM access: va = %p", address);
   // }

   //   Send reply
   MYLOG("MSG DRAM>%d for %lx", shmem_req->getShmemMsg()->getRequester(), address )
   getGlobalMemoryManager()->sendMsg(reply_msg_type,
         MemComponent::GMM_CORE, MemComponent::L2_CACHE,
         shmem_req->getShmemMsg()->getRequester() /* requester */,
         shmem_req->getShmemMsg()->getRequester() /* receiver */,
         address,
         shmem_msg->getDataBuf(), getMemoryManager()->getCacheBlockSize(),
         hit_where,
         shmem_req->getShmemMsg()->getPerf(),
         ShmemPerfModel::_SIM_THREAD);

   // Process Next Request
   processNextReqFromL2Cache(address);
   MYLOG("End @ %lx", address);

}

void
GMMCore::processInvRepFromL2Cache(core_id_t sender, ShmemMsg* shmem_msg)
{
   IntPtr address = shmem_msg->getAddress();

   MYLOG("Start @ %lx", address);

   policy_id_t policy_id;
   uint64_t seg_id;
   bool found = Sim()->getSegmentTable()->lookup(shmem_msg->getAddress(), policy_id, seg_id);
   LOG_ASSERT_ERROR(found, "Segment not found.");

   Sift::GMMCoreMessage *core_msg = new Sift::GMMCoreMessage();
   buildGMMCoreMessage(seg_id, policy_id, sender, shmem_msg, *core_msg);

   if (core_msg->policy != INVALID_POLICY)
      enqueueMessage(core_msg);

   MYLOG("End @ %lx", address);
}

void
GMMCore::retrieveDataAndSendToL2Cache(ShmemMsg::msg_t reply_msg_type,
      core_id_t receiver, IntPtr address, Byte* cached_data_buf, ShmemMsg *orig_shmem_msg)
{
   MYLOG("Start @ %lx", address);
   assert(m_dram_queue_list->size(address) > 0);
   ShmemReq* shmem_req = m_dram_queue_list->front(address);

   // Get the data from DRAM
   // This could be directly forwarded to the cache or passed
   // through the Dram Directory Controller

   // Remember that this request is waiting for data, and should not be woken up by voluntary invalidates
   shmem_req->setWaitForData(true);

   core_id_t dram_node = m_core_id; // m_dram_controller_home_lookup->getHome(address);

   IntPtr phys_address = orig_shmem_msg->getPhysAddress();
   assert(phys_address != INVALID_ADDRESS);

   MYLOG("Sending request to DRAM for the data");
   getGlobalMemoryManager()->sendMsg(ShmemMsg::DRAM_READ_REQ,
         MemComponent::GMM_CORE, MemComponent::DRAM,
         receiver /* requester */,
         dram_node /* receiver */,
         address,
         phys_address,
         NULL, 0,
         HitWhere::UNKNOWN,
         orig_shmem_msg->getPerf(),
         ShmemPerfModel::_SIM_THREAD);

   MYLOG("End @ %lx", address);
}

void
GMMCore::retrieveDataAndSendToGMM(ShmemMsg::msg_t reply_msg_type,
      IntPtr address, ShmemMsg *orig_shmem_msg)
{
   MYLOG("Start @ %lx", address);
   assert(m_dram_queue_list->size(address) > 0);
   ShmemReq* shmem_req = m_dram_queue_list->front(address);

   // Get the data from DRAM
   // This could be directly forwarded to the cache or passed
   // through the Dram Directory Controller

   // Remember that this request is waiting for data, and should not be woken up by voluntary invalidates
   shmem_req->setWaitForData(true);

   core_id_t dram_node = m_core_id; // m_dram_controller_home_lookup->getHome(address);

   MYLOG("Sending request to DRAM for the data");
   // LOG_PRINT_WARNING("Retrieving data from user cache @0x%lx", address);
   getGlobalMemoryManager()->sendMsg(ShmemMsg::DRAM_READ_REQ,
         MemComponent::GMM_CORE, MemComponent::DRAM,
         orig_shmem_msg->getRequester() /* requester */,
         dram_node /* receiver */,
         address,
         NULL, 0,
         HitWhere::UNKNOWN,
         orig_shmem_msg->getPerf(),
         ShmemPerfModel::_SIM_THREAD);

   MYLOG("End @ %lx", address);
}

void
GMMCore::handleGMMCorePull(SubsecondTime now)
{
   assert(!m_dequeued_msg_time.empty());
   m_msg_time = m_dequeued_msg_time.front();
   m_dequeued_msg_time.pop_front();
   if (m_msg_time > now)
      m_dequeue_time = now;
   // LOG_PRINT_WARNING("[%d]dequeue time: %s", m_core_id, itostr(m_msg_time).c_str());
}

void
GMMCore::handleGMMCoreMessage(Sift::GMMCoreMessage* msg, SubsecondTime now)
{
   assert(now >= m_dequeue_time);
   SubsecondTime process_time = now - m_dequeue_time;
   SubsecondTime queue_delay = m_queue_model->computeQueueDelay(m_msg_time, process_time);
   // if (queue_delay > SubsecondTime::NS(100))
   //    LOG_PRINT_WARNING("Queue delay too long.");
   getShmemPerfModel()->setElapsedTime(ShmemPerfModel::_USER_THREAD, m_msg_time + process_time + queue_delay);

   m_msg_time = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_USER_THREAD);
   // LOG_PRINT_WARNING("[%d]core msg time: %s, type=%d, addr=%lx, process=%s, queue_delay=%s",
   //                   m_core_id, itostr(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_USER_THREAD)).c_str(),
   //                   msg->type, msg->payload[0], itostr(process_time).c_str(), itostr(queue_delay).c_str());

   switch (msg->type)
   {
      case ShmemMsg::ATOMIC_UPDATE_REP:
      case ShmemMsg::GMM_USER_DONE:
      case ShmemMsg::INV_REQ:
      case ShmemMsg::TLB_INSERT:
         if (msg->component == MemComponent::LAST_LEVEL_CACHE)
            msg->receiver = getGlobalMemoryManager()->getUserFromId(m_core_id);

         getGlobalMemoryManager()->sendMsg(static_cast<ShmemMsg::msg_t>(msg->type),
               MemComponent::GMM_CORE, static_cast<MemComponent::component_t>(msg->component),
               msg->requester /* requester */,
               msg->receiver /* receiver */,
               msg->payload[0],
               msg->payload[1],
               NULL, 0,
               HitWhere::UNKNOWN,
               &m_dummy_shmem_perf,
               ShmemPerfModel::_USER_THREAD);
         // LOG_PRINT_WARNING("Forwarding msg (type=%d) to node %d", msg->type, msg->receiver);
         break;
      case ShmemMsg::ATOMIC_UPDATE_MSG:
         for (core_id_t dest_core_id = (core_id_t)Sim()->getConfig()->getApplicationCores();
              dest_core_id < (core_id_t)Sim()->getConfig()->getTotalCores(); dest_core_id ++)
         {
            if (dest_core_id == m_core_id)
               continue;

            getGlobalMemoryManager()->sendMsg(ShmemMsg::ATOMIC_UPDATE_MSG,
                  MemComponent::GMM_CORE, MemComponent::GMM_CORE,
                  msg->requester /* requester */,
                  dest_core_id /* receiver */,
                  msg->payload[0],
                  msg->payload[1],
                  NULL, 0,
                  HitWhere::UNKNOWN,
                  &m_dummy_shmem_perf,
                  ShmemPerfModel::_USER_THREAD);

            // LOG_PRINT_WARNING("ATOMIC_UPDATE_MSG %d>%d", m_core_id, dest_core_id);
         }
         break;
      case ShmemMsg::USER_CACHE_READ_REQ:
         getGlobalMemoryManager()->sendMsg(ShmemMsg::USER_CACHE_READ_REQ,
               MemComponent::GMM_CORE, MemComponent::GMM_CORE,
               msg->requester /* requester */,
               m_core_id /* receiver */,
               msg->payload[0],
               msg->payload[1],
               NULL, 0,
               HitWhere::UNKNOWN,
               &m_dummy_shmem_perf,
               ShmemPerfModel::_USER_THREAD);
         break;
      case ShmemMsg::GMM_CORE_DONE:
      case ShmemMsg::USER_CACHE_WRITE_REQ:
         break;
      // case TLB_INSERT:
      //    getGlobalMemoryManager()->sendMsg(ShmemMsg::TLB_INSERT,
      //          MemComponent::GMM_CORE, MemComponent::GMM_CORE,
      //          m_core_id /* requester */,
      //          m_core_id /* receiver */,
      //          msg->payload[0],
      //          msg->payload[1],
      //          NULL, 0,
      //          HitWhere::UNKNOWN,
      //          &m_dummy_shmem_perf,
      //          ShmemPerfModel::_USER_THREAD);
      //    break;
      // case INV_REQ:
      //    getGlobalMemoryManager()->sendMsg(ShmemMsg::INV_REQ,
      //          MemComponent::GMM_CORE, MemComponent::LAST_LEVEL_CACHE,
      //          msg->requester /* requester */,
      //          getGlobalMemoryManager()->getUserFromId(m_core_id) /* receiver */,
      //          msg->payload[0],
      //          msg->payload[1],
      //          NULL, 0,
      //          HitWhere::UNKNOWN,
      //          &m_dummy_shmem_perf,
      //          ShmemPerfModel::_USER_THREAD);
      //    break;
      // case ATOMIC_UPDATE_REQ:
      //    {
      //       getGlobalMemoryManager()->sendMsg(ShmemMsg::ATOMIC_UPDATE_MSG,
      //             MemComponent::GMM_CORE, MemComponent::GMM_CORE,
      //             msg->requester /* requester */,
      //             msg->receiver /* receiver */,
      //             msg->payload[0],
      //             msg->payload[1],
      //             NULL, 0,
      //             HitWhere::UNKNOWN,
      //             &m_dummy_shmem_perf,
      //             ShmemPerfModel::_USER_THREAD);
      //    }

      default:
         LOG_PRINT_ERROR("Unrecognized Shmem Msg Type: %u", msg->type);
   }
}

void
GMMCore::policyInit(int seg_id, int policy_id, uint64_t start, uint64_t end)
{
   Sift::GMMCoreMessage *core_msg = new Sift::GMMCoreMessage();
   core_msg->segid = seg_id;
   core_msg->policy = policy_id;
   core_msg->type = ShmemMsg::POLICY_INIT;
   core_msg->requester = m_core_id;
   core_msg->sender = m_core_id;
   core_msg->payload[0] = start;
   core_msg->payload[1] = end;
   enqueueMessage(core_msg);
}

void
GMMCore::signalStop()
{
   m_msg_queue_lock.acquire();
   m_msg_queue.clear();
   m_msg_cond.signal();
   m_msg_queue_lock.release();
}

}
