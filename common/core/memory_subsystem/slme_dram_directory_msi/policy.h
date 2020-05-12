#pragma once

#include "fixed_types.h"
#include "shmem_msg.h"

namespace SingleLevelMemory
{
   class GlobalMemoryManager;
   class PolicyBase
   {
      protected:
         GlobalMemoryManager* m_memory_manager;
         GlobalMemoryManager* getMemoryManager() { return m_memory_manager; }

      public:
         PolicyBase(GlobalMemoryManager* memory_manager) : m_memory_manager(memory_manager) {}
         virtual ~PolicyBase() {}
         virtual void handleMsgFromGMM(core_id_t sender, ShmemMsg* shmem_msg) = 0;
         virtual void handleMsgFromL2Cache(core_id_t sender, ShmemMsg* shmem_msg) = 0;
         virtual void handleMsgFromDRAM(core_id_t sender, ShmemMsg* shmem_msg) = 0;
   };
}
