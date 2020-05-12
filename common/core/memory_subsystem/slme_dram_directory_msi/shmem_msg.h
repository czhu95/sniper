#pragma once

#include "mem_component.h"
#include "fixed_types.h"
#include "hit_where.h"
#include "../pr_l1_pr_l2_dram_directory_msi/shmem_msg.h"

class ShmemPerf;

namespace SingleLevelMemory
{
   class ShmemMsg
   {
      private:
         PrL1PrL2DramDirectoryMSI::ShmemMsg::msg_t m_msg_type;
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
         ShmemMsg(PrL1PrL2DramDirectoryMSI::ShmemMsg::msg_t msg_type,
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

         PrL1PrL2DramDirectoryMSI::ShmemMsg::msg_t getMsgType() { return m_msg_type; }
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
