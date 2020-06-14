#include "replication_policy.h"
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
#include <set>

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

ReplicationPolicy::ReplicationPolicy(
      Core* core,
      GlobalMemoryManager* memory_manager,
      AddressHomeLookup* dram_controller_home_lookup,
      UInt32 replica_block_size,
      ShmemPerfModel* shmem_perf_model):
   PolicyBase(memory_manager),
   m_dram_controller_home_lookup(dram_controller_home_lookup),
   m_core_id(core->getId()),
   m_replica_block_size(replica_block_size),
   m_shmem_perf_model(shmem_perf_model),
   m_access_time(NULL, 0)
{
   m_access_time = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/gmm_access_cycles"));
   m_dram_directory_req_queue_list = new ReqQueueList();
}

ReplicationPolicy::~ReplicationPolicy()
{
   delete m_dram_directory_req_queue_list;
}

void
ReplicationPolicy::handleMsgFromGMM(core_id_t sender, ShmemMsg* shmem_msg)
{
   LOG_PRINT_ERROR("Should not reach here.");
}

void
ReplicationPolicy::handleMsgFromL2Cache(core_id_t sender, ShmemMsg* shmem_msg)
{
   ShmemMsg::msg_t shmem_msg_type = shmem_msg->getMsgType();
   SubsecondTime msg_time = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD);
   IntPtr va = shmem_msg->getAddress();

   MYLOG("begin for address %lx, pa %lx, %d in queue, requester is %u", va,
         shmem_msg->getPhysAddress(), m_dram_directory_req_queue_list->size(va),
         shmem_msg->getRequester());

   switch (shmem_msg_type)
   {
      case ShmemMsg::SH_REQ:
      {
         MYLOG("S REQ<%u @ %lx", sender, va);

         // Add request onto a queue
         ShmemReq* shmem_req = new ShmemReq(shmem_msg, msg_time);

         m_dram_directory_req_queue_list->enqueue(va, shmem_req);
         MYLOG("ENqueued S REQ for address %lx", va );
         if (m_dram_directory_req_queue_list->size(va) == 1)
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

      default:
         LOG_PRINT_ERROR("Unrecognized Shmem Msg Type: %u", shmem_msg_type);
         break;
   }
MYLOG("done for %lx", va);
}

void
ReplicationPolicy::handleMsgFromDRAM(core_id_t sender, ShmemMsg* shmem_msg)
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
ReplicationPolicy::processNextReqFromL2Cache(IntPtr address)
{
   MYLOG("Start processNextReqFromL2Cache(%lx): %d in Queue", address, m_dram_directory_req_queue_list->size(address) );

   MYLOG("about to dequeue request for address %lx", address );
   assert(m_dram_directory_req_queue_list->size(address) >= 1);
   ShmemReq* completed_shmem_req = m_dram_directory_req_queue_list->dequeue(address);
   delete completed_shmem_req;

   if (! m_dram_directory_req_queue_list->empty(address))
   {
      MYLOG("A new shmem req for address(%lx) found", address);

      ShmemReq* shmem_req = m_dram_directory_req_queue_list->front(address);

      // Update the Shared Mem Cycle Counts appropriately
      getShmemPerfModel()->updateElapsedTime(shmem_req->getTime(), ShmemPerfModel::_SIM_THREAD);

      if (shmem_req->getShmemMsg()->getMsgType() == ShmemMsg::SH_REQ)
      {
         MYLOG("A new SH_REQ for address(%lx) found", address);
         processShReqFromL2Cache(shmem_req);
      }
      else
         LOG_PRINT_ERROR("Unrecognized Request(%u)", shmem_req->getShmemMsg()->getMsgType());
   }
   MYLOG("End processNextReqFromL2Cache(%lx)", address);
}

void
ReplicationPolicy::processShReqFromL2Cache(ShmemReq* shmem_req, Byte* cached_data_buf)
{
   IntPtr address = shmem_req->getShmemMsg()->getAddress();
   core_id_t requester = shmem_req->getShmemMsg()->getRequester();

   MYLOG("Start @ %lx", address);
   updateShmemPerf(shmem_req);
   retrieveDataAndSendToL2Cache(ShmemMsg::SH_REP, requester, address, NULL, shmem_req->getShmemMsg());

   MYLOG("End @ %lx", address);
}

void
ReplicationPolicy::retrieveDataAndSendToL2Cache(ShmemMsg::msg_t reply_msg_type,
      core_id_t receiver, IntPtr address, Byte* cached_data_buf, ShmemMsg *orig_shmem_msg)
{
   MYLOG("Start @ %lx", address);
   assert(m_dram_directory_req_queue_list->size(address) > 0);
   ShmemReq* shmem_req = m_dram_directory_req_queue_list->front(address);

   // Get the data from DRAM
   // This could be directly forwarded to the cache or passed
   // through the Dram Directory Controller

   if (shmem_req->getShmemMsg()->getMsgType() == ShmemMsg::UPGRADE_REQ)
   {
      MYLOG("Have to get data from DRAM while doing an UPGRADE_REQ: lost data somehow\n");
   }

   // Remember that this request is waiting for data, and should not be woken up by voluntary invalidates
   shmem_req->setWaitForData(true);

   IntPtr repl_addr = address - address % m_replica_block_size;
   IntPtr phys_address = orig_shmem_msg->getPhysAddress();
   assert(phys_address != INVALID_ADDRESS);

   core_id_t dram_node;
   if (m_replicas.count(repl_addr) == 0)
   {
      dram_node = m_dram_controller_home_lookup->getHome(phys_address);
      if (dram_node != m_core_id)
         getShmemPerfModel()->incrElapsedTime(m_access_time.getLatency(), ShmemPerfModel::_SIM_THREAD);
   }
   else
   {
      dram_node = m_core_id;
   }


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
   MYLOG("End @ %lx", address);
}

void
ReplicationPolicy::processDRAMReply(core_id_t sender, ShmemMsg* shmem_msg)
{
   IntPtr address = shmem_msg->getAddress();

   MYLOG("Start @ %lx", address);
   // Data received from DRAM

   //   Which node to reply to?

   assert(m_dram_directory_req_queue_list->size(address) >= 1);
   ShmemReq* shmem_req = m_dram_directory_req_queue_list->front(address);
   updateShmemPerf(shmem_req);

   //   Which reply type to use?

   ShmemMsg::msg_t reply_msg_type;
   updateShmemPerf(shmem_req, ShmemPerf::TD_ACCESS);

   switch(shmem_req->getShmemMsg()->getMsgType())
   {
      case ShmemMsg::SH_REQ:
         reply_msg_type = ShmemMsg::SH_REP;
         break;
      default:
         LOG_PRINT_ERROR("Unsupported request type: %u", shmem_req->getShmemMsg()->getMsgType());
   }

   //   Which HitWhere to report?
   HitWhere::where_t hit_where = shmem_msg->getWhere();
   if (hit_where == HitWhere::DRAM)
      hit_where = (sender == shmem_msg->getRequester()) ? HitWhere::DRAM_LOCAL : HitWhere::DRAM_REMOTE;

   if (hit_where == HitWhere::DRAM_REMOTE)
   {
      IntPtr repl_addr = address - address % m_replica_block_size;
      // assert(m_replicas.count(repl_addr) == 0);
      m_replicas.insert(repl_addr);
      LOG_PRINT_WARNING("Replication Policy: Remote DRAM access: va = %p", address);
      MYLOG("Replicate @ %lx", repl_addr);
   }
   // else
   // {
   //    LOG_PRINT_WARNING("Replication Policy: Local DRAM access: va = %p", address);
   // }

   //   Send reply
   MYLOG("MSG DRAM>%d for %lx", shmem_req->getShmemMsg()->getRequester(), address )
   getMemoryManager()->sendMsg(reply_msg_type,
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
ReplicationPolicy::processInvRepFromL2Cache(core_id_t sender, ShmemMsg* shmem_msg)
{
   IntPtr address = shmem_msg->getAddress();

   MYLOG("Start @ %lx", address);

   if (m_dram_directory_req_queue_list->size(address) > 0)
   {
      MYLOG("More requests outstanding for address %lx", address);
      ShmemReq* shmem_req = m_dram_directory_req_queue_list->front(address);

      // Update Times in the Shmem Perf Model and the Shmem Req
      shmem_req->updateTime(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD));
      getShmemPerfModel()->updateElapsedTime(shmem_req->getTime(), ShmemPerfModel::_SIM_THREAD);

      if (shmem_req->getShmemMsg()->getMsgType() == ShmemMsg::SH_REQ)
      {
         if (shmem_req->getWaitForData())
         {
            // This is a voluntary invalidate (probably part of an upgrade or eviction),
            // the next request should only be woken up once its data arrives from DRAM.
         }
         else if (shmem_req->isForwarding() && sender != shmem_req->getForwardingFrom())
         {
            // This is a voluntary invalidate (probably part of an upgrade or eviction),
            // the next request should only be woken up once its data arrives from the forwarder.
         }
         else
         {
            // A PrL1PrL2DramDirectoryMSI::ShmemMsg::SH_REQ caused the invalidation
            updateShmemPerf(shmem_req, ShmemPerf::INV_IMBALANCE);
            processShReqFromL2Cache(shmem_req);
         }
      }
      else
      {
         LOG_PRINT_ERROR("Unsupported request type: %u", shmem_req->getShmemMsg()->getMsgType());
      }
   }
   MYLOG("End @ %lx", address);
}

}
