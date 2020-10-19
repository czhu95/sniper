#include "gmm_mem_policy.h"
#include "policy.h"
#include "log.h"
#include "global_memory_manager.h"
#include "stats.h"
#include "shmem_perf.h"
#include "coherency_protocol.h"
#include "config.hpp"
#include "directory_state.h"
#include "thread_manager.h"
#include "thread.h"
#include "segment_table.h"

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

GMMMemPolicy::GMMMemPolicy(
      core_id_t core_id,
      GlobalMemoryManager* memory_manager,
      UInt32 cache_block_size,
      ShmemPerfModel* shmem_perf_model):
   PolicyBase(memory_manager, 0),
   m_core_id(core_id),
   m_cache_block_size(cache_block_size),
   m_shmem_perf_model(shmem_perf_model)
{
   m_req_queue_list = new ReqQueueList();
}

GMMMemPolicy::~GMMMemPolicy()
{
   delete m_req_queue_list;
}

void
GMMMemPolicy::handleMsgFromGMM(core_id_t sender, ShmemMsg* shmem_msg)
{
   LOG_PRINT_ERROR("Should not reach here.");
}

void
GMMMemPolicy::handleMsgFromL2Cache(core_id_t sender, ShmemMsg* shmem_msg)
{
   ShmemMsg::msg_t shmem_msg_type = shmem_msg->getMsgType();
   SubsecondTime msg_time = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD);
   IntPtr va = shmem_msg->getAddress();
   IntPtr pa = shmem_msg->getPhysAddress();
   core_id_t requester = shmem_msg->getRequester();

   MYLOG("begin for address %lx, pa %lx, %d in queue, requester is %u", va, pa, m_req_queue_list->size(va), requester);

   LOG_ASSERT_ERROR(sender == m_core_id, "va = 0x%lx", va);
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

         // Add request onto a queue
         ShmemReq* shmem_req = new ShmemReq(shmem_msg, msg_time);

         m_req_queue_list->enqueue(va, shmem_req);
         MYLOG("ENqueued E REQ for address %lx", va );
         if (m_req_queue_list->size(va) == 1)
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

         m_req_queue_list->enqueue(va, shmem_req);
         MYLOG("ENqueued S REQ for address %lx", va );
         if (m_req_queue_list->size(va) == 1)
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
         processFlushRepFromL2Cache(sender, shmem_msg);
         break;

      case ShmemMsg::WB_REP:
         MYLOG("WB REP<%u @ %lx", sender, va);
         processWbRepFromL2Cache(sender, shmem_msg);
         break;

      default:
         LOG_PRINT_ERROR("Unrecognized Shmem Msg Type: %u, va=%lx", shmem_msg_type, va);
         break;

      case ShmemMsg::UPGRADE_REQ:
         MYLOG("UPGR REQ<%u @ %lx", sender, va);

         // Add request onto a queue
         ShmemReq* shmem_req = new ShmemReq(shmem_msg, msg_time);
         m_req_queue_list->enqueue(va, shmem_req);
         MYLOG("ENqueued  UPGRADE REQ for address %lx",  va );

         if (m_req_queue_list->size(va) == 1)
         {
            processUpgradeReqFromL2Cache(shmem_req);
         }
         else
         {
            MYLOG("UPGRADE REQ (%lx) not handled because of outstanding request in the queue", va);
         }

         break;

   }
MYLOG("done for %lx", va);
}

void
GMMMemPolicy::handleMsgFromDRAM(core_id_t sender, ShmemMsg* shmem_msg)
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
GMMMemPolicy::processNextReqFromL2Cache(IntPtr address)
{
   MYLOG("Start processNextReqFromL2Cache(%lx): %d in Queue", address, m_req_queue_list->size(address) );

   MYLOG("about to dequeue request for address %lx", address );
   assert(m_req_queue_list->size(address) >= 1);
   ShmemReq* completed_shmem_req = m_req_queue_list->dequeue(address);
   delete completed_shmem_req;

   if (! m_req_queue_list->empty(address))
   {
      MYLOG("A new shmem req for address(%lx) found", address);

      ShmemReq* shmem_req = m_req_queue_list->front(address);

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
      }
      else if (shmem_req->getShmemMsg()->getMsgType() == ShmemMsg::UPGRADE_REQ)
      {
         MYLOG("A new UPGRADE_REQ for address(%lx) found", address);
         processUpgradeReqFromL2Cache(shmem_req);
      }
      else
         LOG_PRINT_ERROR("Unrecognized Request(%u)", shmem_req->getShmemMsg()->getMsgType());
   }
   MYLOG("End processNextReqFromL2Cache(%lx)", address);
}

void
GMMMemPolicy::processNullifyReq(ShmemReq* shmem_req)
{
   IntPtr address = shmem_req->getShmemMsg()->getAddress();
   core_id_t requester = shmem_req->getShmemMsg()->getRequester();

   // NOTE: no ShmemPerf accounting for nullify requests as they may happen *after* core requests are completed

   MYLOG("Start @ %lx", address);
   processNextReqFromL2Cache(address);
   MYLOG("End @ %lx", address);
}

void
GMMMemPolicy::processExReqFromL2Cache(ShmemReq* shmem_req, Byte* cached_data_buf)
{
   IntPtr address = shmem_req->getShmemMsg()->getAddress();
   core_id_t requester = shmem_req->getShmemMsg()->getRequester();

   MYLOG("Start @ %lx", address);
   updateShmemPerf(shmem_req);
   retrieveDataAndSendToL2Cache(ShmemMsg::EX_REP, requester, address, cached_data_buf, shmem_req->getShmemMsg());

   MYLOG("End @ %lx", address);
}

void
GMMMemPolicy::processShReqFromL2Cache(ShmemReq* shmem_req, Byte* cached_data_buf)
{
   IntPtr address = shmem_req->getShmemMsg()->getAddress();
   core_id_t requester = shmem_req->getShmemMsg()->getRequester();

   MYLOG("Start @ %lx", address);
   updateShmemPerf(shmem_req);

   retrieveDataAndSendToL2Cache(ShmemMsg::SH_REP, requester, address, cached_data_buf, shmem_req->getShmemMsg());
   MYLOG("End @ %lx", address);
}

void
GMMMemPolicy::retrieveDataAndSendToL2Cache(ShmemMsg::msg_t reply_msg_type,
      core_id_t receiver, IntPtr address, Byte* cached_data_buf, ShmemMsg *orig_shmem_msg)
{
   if (cached_data_buf != NULL)
   {
      getMemoryManager()->sendMsg(reply_msg_type,
            MemComponent::GMM_CORE, MemComponent::L2_CACHE,
            receiver /* requester */,
            receiver /* receiver */,
            address,
            cached_data_buf, getCacheBlockSize(),
            HitWhere::CACHE_REMOTE /* cached_data_buf was filled by a WB_REQ or FLUSH_REQ */,
            orig_shmem_msg->getPerf(),
            ShmemPerfModel::_SIM_THREAD);

      // Process Next Request
      processNextReqFromL2Cache(address);
   }
   else
   {
      ShmemReq* shmem_req = m_req_queue_list->front(address);
      // Get the data from DRAM
      if (shmem_req->getShmemMsg()->getMsgType() == ShmemMsg::UPGRADE_REQ)
      {
         MYLOG("Have to get data from DRAM while doing an UPGRADE_REQ: lost data somehow\n");
      }

      // Remember that this request is waiting for data, and should not be woken up by voluntary invalidates
      shmem_req->setWaitForData(true);

      core_id_t dram_node = m_core_id; // m_dram_controller_home_lookup->getHome(address);

      IntPtr phys_address = orig_shmem_msg->getPhysAddress();
      assert(phys_address != INVALID_ADDRESS);

      MYLOG("Sending request to DRAM for the data");
      getMemoryManager()->sendMsg(ShmemMsg::DRAM_READ_REQ,
            MemComponent::GMM_CORE, MemComponent::DRAM,
            receiver /* requester */,
            dram_node /* receiver */,
            address,
            phys_address,
            NULL, 0,
            HitWhere::UNKNOWN,
            orig_shmem_msg->getPerf(),
            ShmemPerfModel::_SIM_THREAD);
   }
   MYLOG("End @ %lx", address);
}

void
GMMMemPolicy::processDRAMReply(core_id_t sender, ShmemMsg* shmem_msg)
{
   IntPtr address = shmem_msg->getAddress();

   MYLOG("Start @ %lx", address);
   // Data received from DRAM

   //   Which node to reply to?

   ShmemReq* shmem_req = m_req_queue_list->front(address);
   updateShmemPerf(shmem_req);

   //   Which reply type to use?

   ShmemMsg::msg_t reply_msg_type;

   switch(shmem_req->getShmemMsg()->getMsgType())
   {
      case ShmemMsg::SH_REQ:
         reply_msg_type = ShmemMsg::EX_REP;
         break;
      case ShmemMsg::EX_REQ:
         reply_msg_type = ShmemMsg::EX_REP;
         break;
      case ShmemMsg::UPGRADE_REQ:
         // if we had to get the data from DRAM, nobody has it anymore: send EX_REP
         reply_msg_type = ShmemMsg::EX_REP;
         break;
      default:
         LOG_PRINT_ERROR("Unsupported request type: %u", shmem_req->getShmemMsg()->getMsgType());
   }

   //   Which HitWhere to report?
   HitWhere::where_t hit_where = shmem_msg->getWhere();
   if (hit_where == HitWhere::DRAM)
      hit_where = (getMemoryManager()->getUserFromId(sender) == shmem_msg->getRequester()) ? HitWhere::DRAM_LOCAL : HitWhere::DRAM_REMOTE;

   // if (hit_where == HitWhere::DRAM_REMOTE)
   // {
   //    LOG_PRINT_WARNING("Directory Policy: Remote DRAM access: va = %p", address);
   // }
   // else
   // {
   //    LOG_PRINT_WARNING("Directory Policy: Local DRAM access: va = %p", address);
   // }

   //   Send reply
   MYLOG("MSG DRAM>%d for %lx", shmem_req->getShmemMsg()->getRequester(), address )
   getMemoryManager()->sendMsg(reply_msg_type,
         MemComponent::GMM_CORE, MemComponent::L2_CACHE,
         shmem_req->getShmemMsg()->getRequester() /* requester */,
         shmem_req->getShmemMsg()->getRequester() /* receiver */,
         address,
         shmem_msg->getDataBuf(), getCacheBlockSize(),
         hit_where,
         shmem_req->getShmemMsg()->getPerf(),
         ShmemPerfModel::_SIM_THREAD);

   // Process Next Request
   processNextReqFromL2Cache(address);
   MYLOG("End @ %lx", address);

}

void
GMMMemPolicy::processInvRepFromL2Cache(core_id_t sender, ShmemMsg* shmem_msg)
{
   IntPtr address = shmem_msg->getAddress();

   MYLOG("Start @ %lx", address);

   // LOG_PRINT_ERROR("Should not reach here.");
   MYLOG("End @ %lx", address);
}

void
GMMMemPolicy::processUpgradeReqFromL2Cache(ShmemReq* shmem_req, Byte* cached_data_buf)
{
   ShmemMsg* shmem_msg = shmem_req->getShmemMsg();

   IntPtr address = shmem_msg->getAddress();
   core_id_t requester = shmem_msg->getRequester();
   updateShmemPerf(shmem_req);

   getMemoryManager()->sendMsg(ShmemMsg::UPGRADE_REP,
         MemComponent::GMM_CORE, MemComponent::L2_CACHE,
         requester /* requester */,
         requester /* receiver */,
         address,
         NULL, 0,
         HitWhere::UNKNOWN, shmem_msg->getPerf(), ShmemPerfModel::_SIM_THREAD);

   processNextReqFromL2Cache(address);
   MYLOG("End @ %lx", address);
}

void
GMMMemPolicy::processFlushRepFromL2Cache(core_id_t sender, ShmemMsg* shmem_msg)
{
   IntPtr address = shmem_msg->getAddress();
   SubsecondTime now = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD);

   MYLOG("Start @ %lx", address);

   if (m_req_queue_list->size(address) != 0)
   {
      ShmemReq* shmem_req = m_req_queue_list->front(address);

      // Update times
      shmem_req->updateTime(now);
      getShmemPerfModel()->updateElapsedTime(shmem_req->getTime(), ShmemPerfModel::_SIM_THREAD);

      shmem_req->getShmemMsg()->getPerf()->updateTime(now);
      updateShmemPerf(shmem_req, ShmemPerf::TD_ACCESS);

      // An involuntary/voluntary Flush
      if (shmem_req->getShmemMsg()->getMsgType() == ShmemMsg::EX_REQ)
      {
         processExReqFromL2Cache(shmem_req, shmem_msg->getDataBuf());
      }
      else if (shmem_req->getShmemMsg()->getMsgType() == ShmemMsg::SH_REQ)
      {
         // Write Data to Dram
         sendDataToDram(shmem_msg->getPhysAddress(), shmem_msg->getRequester(), shmem_msg->getDataBuf(), now);
         processShReqFromL2Cache(shmem_req, shmem_msg->getDataBuf());
      }
      else if (shmem_req->getShmemMsg()->getMsgType() == ShmemMsg::UPGRADE_REQ)
      {
         // MYLOG("as part of UPGR: got FLUSH_REP from %d, %d sharers left", sender,directory_entry->getNumSharers() );
         // there should be only one M copy that is written back
         processUpgradeReqFromL2Cache(shmem_req, shmem_msg->getDataBuf());
      }
      else // shmem_req->getShmemMsg()->getMsgType() == ShmemMsg::NULLIFY_REQ
      {
         // Write Data To Dram
         sendDataToDram(shmem_msg->getPhysAddress(), shmem_msg->getRequester(), shmem_msg->getDataBuf(), now);
         processNullifyReq(shmem_req);
      }
   }
   else
   {
      // This was just an eviction
      // Write Data to Dram
      sendDataToDram(shmem_msg->getPhysAddress(), shmem_msg->getRequester(), shmem_msg->getDataBuf(), now);
   }

   MYLOG("End @ %lx", address);
}

void
GMMMemPolicy::processWbRepFromL2Cache(core_id_t sender, ShmemMsg* shmem_msg)
{
   IntPtr address = shmem_msg->getAddress();
   SubsecondTime now = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD);

   MYLOG("Start @ %lx", address);

   if (m_req_queue_list->size(address) != 0)
   {
      ShmemReq* shmem_req = m_req_queue_list->front(address);

      // Update Time
      shmem_req->updateTime(now);
      getShmemPerfModel()->updateElapsedTime(shmem_req->getTime(), ShmemPerfModel::_SIM_THREAD);

      shmem_req->getShmemMsg()->getPerf()->updateTime(now);
      updateShmemPerf(shmem_req, ShmemPerf::TD_ACCESS);

      LOG_ASSERT_ERROR(shmem_req->getShmemMsg()->getMsgType() == ShmemMsg::SH_REQ,
            "Address(0x%x), Req(%u)",
            address, shmem_req->getShmemMsg()->getMsgType());
      processShReqFromL2Cache(shmem_req, shmem_msg->getDataBuf());
   }
   else
   {
      LOG_PRINT_ERROR("Should not reach here");
   }
   MYLOG("End @ %lx", address);
}

void
GMMMemPolicy::sendDataToDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now)
{
   MYLOG("Start @ %lx", address);

   // Write data to Dram
   core_id_t dram_node = m_core_id; // m_dram_controller_home_lookup->getHome(address);

   getMemoryManager()->sendMsg(ShmemMsg::DRAM_WRITE_REQ,
         MemComponent::GMM_CORE, MemComponent::DRAM,
         requester /* requester */,
         dram_node /* receiver */,
         INVALID_ADDRESS, /* vaddr is unused */
         address,
         data_buf, getCacheBlockSize(),
         HitWhere::UNKNOWN,
         &m_dummy_shmem_perf,
         ShmemPerfModel::_SIM_THREAD);

   // DRAM latency is ignored on write
   MYLOG("End @ %lx", address);
}

}
