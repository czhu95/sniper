#pragma once

#include "core.h"
#include "fixed_types.h"
#include "shmem_req.h"
#include "shmem_perf_model.h"
#include "shmem_perf.h"
#include "segment_table.h"
#include "lock.h"
#include "cond.h"
#include "req_queue_list.h"

#include <deque>
#include <set>

class AddressHomeLookup;
class QueueModel;

namespace Sift
{
   struct GMMCoreMessage;
}

namespace SingleLevelMemory
{
   class DirectoryMSIPolicy;
   class PolicyBase;
   class ShmemMsg;
   class GlobalMemoryManager;

   struct TLBEntry
   {
      uint64_t m_start;
      uint64_t m_end;

      bool contains(IntPtr address)
      {
         return address >= m_start && address < m_end;
      }

      friend bool operator<(const TLBEntry& lhs, const TLBEntry& rhs)
      {
         return lhs.m_end <= rhs.m_start;
      }
   };

   // class TLB
   // {
   //    protected:
   //       std::set<TLBEntry> m_table;
   // };

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

         void enqueueMessage(Sift::GMMCoreMessage *core_msg);
         Sift::GMMCoreMessage *dequeueMessage();

         void handleGMMCorePull(SubsecondTime now);
         void handleGMMCoreMessage(Sift::GMMCoreMessage *msg, SubsecondTime now);

         void signalStop();
         void policyInit(int seg_id, int policy_id, uint64_t start, uint64_t end);

      protected:
         DirectoryMSIPolicy* m_directory_policy;
         AddressHomeLookup *m_dram_controller_home_lookup;

         GlobalMemoryManager *m_global_memory_manager;

         std::deque<std::pair<Sift::GMMCoreMessage *, SubsecondTime>> m_msg_queue;
         Lock m_msg_queue_lock;
         ConditionVariable m_msg_cond;

         std::set<TLBEntry> m_tlb;

         ReqQueueList* m_dram_queue_list;

         QueueModel* m_queue_model;
         SubsecondTime m_dequeue_time;
         std::deque<SubsecondTime> m_dequeued_msg_time;
         SubsecondTime m_msg_time;
         int m_num_pending_core_msg;

         GlobalMemoryManager* getGlobalMemoryManager() { return m_global_memory_manager; }

         void buildGMMCoreMessage(uint64_t seg_id, policy_id_t policy, core_id_t sender, ShmemMsg *shmem_msg, Sift::GMMCoreMessage &msg);

         void updateShmemPerf(ShmemReq *shmem_req, ShmemPerf::shmem_times_type_t reason = ShmemPerf::UNKNOWN)
         {
            updateShmemPerf(shmem_req->getShmemMsg(), reason);
         }

         void updateShmemPerf(ShmemMsg *shmem_msg, ShmemPerf::shmem_times_type_t reason = ShmemPerf::UNKNOWN)
         {
            shmem_msg->getPerf()->updateTime(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD), reason);
         }

         void retrieveDataAndSendToL2Cache(ShmemMsg::msg_t reply_msg_type,
                                           core_id_t receiver, IntPtr address,
                                           Byte* cached_data_buf, ShmemMsg *orig_shmem_msg);

         void retrieveDataAndSendToGMM(ShmemMsg::msg_t reply_msg_type,
                                       IntPtr address, ShmemMsg *orig_shmem_msg);


         void processDRAMReply(core_id_t sender, ShmemMsg* shmem_msg);
         void handleMsgFromDRAM(core_id_t sender, ShmemMsg* shmem_msg);
         void sendDataToDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now);
         void processShReqFromL2Cache(ShmemReq* shmem_req, Byte* cached_data_buf = NULL);
         void processExReqFromL2Cache(ShmemReq* shmem_req, Byte* cached_data_buf = NULL);
         void processInvRepFromL2Cache(core_id_t sender, ShmemMsg* shmem_msg);
         void processNextReqFromL2Cache(IntPtr address);
         void handleMsgFromL2Cache(core_id_t sender, ShmemMsg* shmem_msg);
         void handleMsgFromGMM(core_id_t sender, ShmemMsg* shmem_msg);


         void hookPeriodicInsCheck() override {};
         void hookPeriodicInsCall() override {};
   };
}
