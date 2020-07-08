#pragma once

#include "mem_component.h"
#include "fixed_types.h"
#include "hit_where.h"

class ShmemPerf;

namespace SingleLevelMemory
{
   class ShmemMsg
   {
      public:
         enum msg_t
         {
            INVALID_MSG_TYPE = 0,
            MIN_MSG_TYPE,
            // Cache > tag directory
            EX_REQ = MIN_MSG_TYPE,
            SH_REQ,
            UPGRADE_REQ,
            INV_REQ,
            FLUSH_REQ,
            WB_REQ,
            SUB_REQ,
            CORE_REQ,
            RP_REQ,
            ATOMIC_UPDATE_REQ,
            // Tag directory > cache
            EX_REP,
            SH_REP,
            UPGRADE_REP,
            INV_REP,
            FLUSH_REP,
            WB_REP,
            SUB_REP,
            CORE_REP,
            NULLIFY_REQ,
            RP_REP,
            // Tag directory > DRAM
            DRAM_READ_REQ,
            DRAM_WRITE_REQ,
            // DRAM > tag directory
            DRAM_READ_REP,
            // GMM > TLB
            TLB_INSERT,
            // GMM > GMM
            ATOMIC_UPDATE_MSG,
            ATOMIC_UPDATE_REP,
            ATOMIC_UPDATE_DONE,

            POLICY_INIT,

            GMM_USER_DONE,

            MAX_MSG_TYPE = GMM_USER_DONE,
            NUM_MSG_TYPES = MAX_MSG_TYPE - MIN_MSG_TYPE + 1
         };

      private:
         ShmemMsg::msg_t m_msg_type;
         MemComponent::component_t m_sender_mem_component;
         MemComponent::component_t m_receiver_mem_component;
         core_id_t m_requester;
         HitWhere::where_t m_where;
         IntPtr m_vaddr;
         IntPtr m_paddr;
         Byte* m_data_buf;
         UInt32 m_data_length;
         ShmemPerf* m_perf;

      public:
         ShmemMsg() = delete;
         ShmemMsg(ShmemPerf* perf);
         ShmemMsg(ShmemMsg::msg_t msg_type,
               MemComponent::component_t sender_mem_component,
               MemComponent::component_t receiver_mem_component,
               core_id_t requester,
               IntPtr vaddr,
               IntPtr paddr,
               Byte* data_buf,
               UInt32 data_length,
               ShmemPerf* perf);
         ShmemMsg(ShmemMsg* shmem_msg);

         ~ShmemMsg();

         static ShmemMsg* getShmemMsg(Byte* msg_buf, ShmemPerf* perf);
         Byte* makeMsgBuf();
         UInt32 getMsgLen();

         // Modeling
         UInt32 getModeledLength();

         ShmemMsg::msg_t getMsgType() { return m_msg_type; }
         MemComponent::component_t getSenderMemComponent() { return m_sender_mem_component; }
         MemComponent::component_t getReceiverMemComponent() { return m_receiver_mem_component; }
         core_id_t getRequester() { return m_requester; }
         IntPtr getAddress() { return m_vaddr; }
         void setPhysAddress(IntPtr addr) { m_paddr = addr; }
         IntPtr getPhysAddress() { return m_paddr; }
         Byte* getDataBuf() { return m_data_buf; }
         UInt32 getDataLength() { return m_data_length; }
         HitWhere::where_t getWhere() { return m_where; }

         void setDataBuf(Byte* data_buf) { m_data_buf = data_buf; }
         void setWhere(HitWhere::where_t where) { m_where = where; }

         ShmemPerf* getPerf() { return m_perf; }

   };

}
