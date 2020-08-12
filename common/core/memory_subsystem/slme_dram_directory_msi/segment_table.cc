#include "segment_table.h"
#include "simulator.h"
#include "core_manager.h"
#include "gmm_core.h"

#include "log.h"

#if 0
   extern Lock iolock;
#  include "core_manager.h"
#  include "simulator.h"
#  define MYLOG(...) { ScopedLock l(iolock); fflush(stderr); fprintf(stderr, "[%s] %d%cmm %-25s@%03u: ", itostr(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD)).c_str(), getCore()->getId(), Sim()->getCoreManager()->amiUserThread() ? '^' : '_', __FUNCTION__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); fflush(stderr); }
#else
#  define MYLOG(...) {}
#endif

const uint64_t node_offset = 8;

namespace SingleLevelMemory
{

SegmentTable::SegmentTable()
{
}

policy_id_t
SegmentTable::lookup(IntPtr address)
{
   policy_id_t policy_id = DIRECTORY_COHERENCE;
   m_lock.acquire_read();
   auto it = m_table.find({0, address, address + 1});
   if (it != m_table.end())
   {
      policy_id = it->second;
   }
   m_lock.release_read();

   return policy_id;
}

void
SegmentTable::command(uint64_t cmd_type, IntPtr start, uint64_t arg1)
{
   if (cmd_type == 0)
      create(start, arg1);
   else if (cmd_type == 1)
      assign(start, arg1);
}

void
SegmentTable::create(IntPtr start, uint64_t length)
{
   Segment new_seg{0, start, start + length};
   m_lock.acquire();

   LOG_ASSERT_ERROR(m_table.count(new_seg) == 0, "Segment overlapped.");

   m_table[new_seg] = DIRECTORY_COHERENCE;
   m_lock.release();

   LOG_PRINT_WARNING("Created segment: %p - %p", (void *)new_seg.m_start, (void *)new_seg.m_end);
}

void
SegmentTable::assign(IntPtr start, policy_id_t policy_id)
{
   m_lock.acquire();
   auto it = m_table.find({0, start, start + 1});
   if (it != m_table.end())
   {
      it->second = policy_id;
      LOG_PRINT_WARNING("Segment assign policy: %p - %d", (void *)it->first.m_start, policy_id);

      for (core_id_t core_id = (core_id_t)Sim()->getConfig()->getApplicationCores();
           core_id < (core_id_t)Sim()->getConfig()->getTotalCores(); core_id ++)
      {
         Sim()->getGMMCoreManager()->getGMMCoreFromID(core_id)->policyInit(policy_id, it->first.m_start, it->first.m_end);
      }
   }
   m_lock.release();

}

bool
SegmentTable::bypassCache(policy_id_t policy_id)
{
   return policy_id == ATOMIC_UPDATE;
}

core_id_t
SegmentTable::get_home(IntPtr vaddr)
{
   core_id_t gmm_core_id;
   policy_id_t policy_id = lookup(vaddr);
   switch (policy_id)
   {
      case SUBSCRIPTION:
         gmm_core_id = (vaddr >> node_offset) & (Sim()->getConfig()->getGMMCores() - 1);
         break;
      case ATOMIC_SWAP:
         gmm_core_id = 0;
         break;
      default:
         return INVALID_CORE_ID;
   }

   return Sim()->getConfig()->getApplicationCores() + gmm_core_id;
}

}
