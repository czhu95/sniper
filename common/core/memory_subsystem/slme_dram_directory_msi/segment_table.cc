#include "segment_table.h"

namespace SingleLevelMemory
{

SegmentTable::SegmentTable(GlobalMemoryManager *memory_manager) :
   m_memory_manager(memory_manager)
{
}

void
SegmentTable::insert(uint64_t start, uint64_t end, uint64_t paddr, dstate_t state)
{
   m_table.insert({start, end, paddr, state});
}

dstate_t
SegmentTable::lookup(IntPtr vaddr, IntPtr &paddr)
{
   auto it = m_table.find({vaddr, vaddr, INVALID_ADDRESS, INVALID});
   if (it != m_table.end())
   {
      paddr = (*it).m_paddr + vaddr - (*it).m_start;
      return (*it).m_state;
   }
   else
   {
      return INVALID;
   }
}

}
