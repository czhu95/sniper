#include "dram_cntlr_interface.h"
#include "memory_manager.h"
#include "shmem_perf.h"
#include "log.h"

#include "core.h"
#include <map>
#include <utility>
#include <iostream>

// static std::map<std::pair<IntPtr, IntPtr>, uint64_t> dram_stats[16];

void DramCntlrInterface::handleMsgFromTagDirectory(core_id_t sender, PrL1PrL2DramDirectoryMSI::ShmemMsg* shmem_msg)
{
   PrL1PrL2DramDirectoryMSI::ShmemMsg::msg_t shmem_msg_type = shmem_msg->getMsgType();
   SubsecondTime msg_time = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD);
   shmem_msg->getPerf()->updateTime(msg_time);

   switch (shmem_msg_type)
   {
      case PrL1PrL2DramDirectoryMSI::ShmemMsg::DRAM_READ_REQ:
      {
         // core_id_t core_id = getMemoryManager()->getCore()->getId();
         // auto addr_pair = std::make_pair(shmem_msg->getAddress() >> 12, 0);
         // if (dram_stats[core_id].count(addr_pair))
         //    dram_stats[core_id][addr_pair] ++;
         // else
         //    dram_stats[core_id][addr_pair] = 1;

         IntPtr address = shmem_msg->getAddress();
         Byte data_buf[getCacheBlockSize()];
         SubsecondTime dram_latency;
         HitWhere::where_t hit_where;

         boost::tie(dram_latency, hit_where) = getDataFromDram(address, shmem_msg->getRequester(), data_buf, msg_time, shmem_msg->getPerf());

         getShmemPerfModel()->incrElapsedTime(dram_latency, ShmemPerfModel::_SIM_THREAD);

         shmem_msg->getPerf()->updateTime(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD),
            hit_where == HitWhere::DRAM_CACHE ? ShmemPerf::DRAM_CACHE : ShmemPerf::DRAM);

         getMemoryManager()->sendMsg(PrL1PrL2DramDirectoryMSI::ShmemMsg::DRAM_READ_REP,
               MemComponent::DRAM, MemComponent::TAG_DIR,
               shmem_msg->getRequester() /* requester */,
               sender /* receiver */,
               address,
               data_buf, getCacheBlockSize(),
               hit_where, shmem_msg->getPerf(), ShmemPerfModel::_SIM_THREAD);
         break;
      }

      case PrL1PrL2DramDirectoryMSI::ShmemMsg::DRAM_WRITE_REQ:
      {
         putDataToDram(shmem_msg->getAddress(), shmem_msg->getRequester(), shmem_msg->getDataBuf(), msg_time);

         // DRAM latency is ignored on write

         break;
      }

      default:
         LOG_PRINT_ERROR("Unrecognized Shmem Msg Type: %u", shmem_msg_type);
         break;
   }
}

void DramCntlrInterface::handleMsgFromGMM(core_id_t sender, SingleLevelMemory::ShmemMsg* shmem_msg)
{
   SingleLevelMemory::ShmemMsg::msg_t shmem_msg_type = shmem_msg->getMsgType();
   SubsecondTime msg_time = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD);
   shmem_msg->getPerf()->updateTime(msg_time);

   switch (shmem_msg_type)
   {
      case SingleLevelMemory::ShmemMsg::DRAM_READ_REQ:
      {
         // core_id_t core_id = getMemoryManager()->getCore()->getId();
         // auto addr_pair = std::make_pair(shmem_msg->getAddress() >> 12, shmem_msg->getPhysAddress() >> 12);
         // if (dram_stats[core_id].count(addr_pair))
         //    dram_stats[core_id][addr_pair] ++;
         // else
         //    dram_stats[core_id][addr_pair] = 1;

         IntPtr address = shmem_msg->getPhysAddress();
         Byte data_buf[getCacheBlockSize()];
         SubsecondTime dram_latency;
         HitWhere::where_t hit_where;

         boost::tie(dram_latency, hit_where) = getDataFromDram(address, shmem_msg->getRequester(), data_buf, msg_time, shmem_msg->getPerf());

         getShmemPerfModel()->incrElapsedTime(dram_latency, ShmemPerfModel::_SIM_THREAD);

         shmem_msg->getPerf()->updateTime(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD),
            hit_where == HitWhere::DRAM_CACHE ? ShmemPerf::DRAM_CACHE : ShmemPerf::DRAM);

         getMemoryManager()->sendMsg(SingleLevelMemory::ShmemMsg::DRAM_READ_REP,
               MemComponent::DRAM, MemComponent::GMM,
               shmem_msg->getRequester() /* requester */,
               sender /* receiver */,
               shmem_msg->getAddress(),
               data_buf, getCacheBlockSize(),
               hit_where, shmem_msg->getPerf(), ShmemPerfModel::_SIM_THREAD);
         break;
      }

      case SingleLevelMemory::ShmemMsg::DRAM_WRITE_REQ:
      {
         putDataToDram(shmem_msg->getPhysAddress(), shmem_msg->getRequester(), shmem_msg->getDataBuf(), msg_time);

         // DRAM latency is ignored on write

         break;
      }

      default:
         LOG_PRINT_ERROR("Unrecognized Shmem Msg Type: %u", shmem_msg_type);
         break;
   }
}


DramCntlrInterface::~DramCntlrInterface()
{
   // core_id_t core_id = getMemoryManager()->getCore()->getId();
   // std::cout << "[" << core_id << "]: {";
   // for (auto const& [addr, count] : dram_stats[core_id])
   // {
   //    std::cout << "(" << std::hex << addr.first << "," << addr.second << "):" << std::dec << count << ",";
   // }
   // std::cout << "}" << std::endl;

}
