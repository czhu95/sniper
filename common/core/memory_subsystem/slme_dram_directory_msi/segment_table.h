#pragma once

// #include <map>
#include "fixed_types.h"
#include <set>

namespace SingleLevelMemory
{
   enum dstate_t
   {
      DSTATE_FIRST = 0,
      INVALID = DSTATE_FIRST,
      SHARED,
      SHARED_UPGRADING,
      EXCLUSIVE,
      OWNED,
      MODIFIED,
      NUM_DSTATE_STATES,
   };

   class PolicyBase;

   struct Segment
   {
      uint64_t m_segment_id;
      uint64_t m_start;
      uint64_t m_end;
      PolicyBase *m_policy;


      bool contains(IntPtr address) const
      {
         return address >= m_start && address < m_end;
      }

      friend bool operator<(const Segment& lhs, const Segment& rhs)
      {
         return lhs.m_end <= rhs.m_start;
      }

      friend bool operator==(const Segment& lhs, const Segment& rhs)
      {
         return lhs.m_end > rhs.m_start && rhs.m_end > lhs.m_start;
      }

      friend bool operator!=(const Segment& lhs, const Segment& rhs)
      {
         return !(lhs == rhs);
      }
   };

   struct Policy
   {
      uint64_t m_policy_id;
   };

   struct Subsegment
   {
      uint64_t m_start;
      uint64_t m_end;
      uint64_t m_paddr;
      dstate_t m_state;

      friend bool operator<(const Subsegment& lhs, const Subsegment& rhs)
      {
         return lhs.m_end <= rhs.m_start;
      }

      friend bool operator==(const Subsegment& lhs, const Subsegment& rhs)
      {
         return lhs.m_end > rhs.m_start && rhs.m_end > lhs.m_start;
      }

      friend bool operator!=(const Subsegment& lhs, const Subsegment& rhs)
      {
         return !(lhs == rhs);
      }
   };

   class GlobalMemoryManager;

   class SegmentTable
   {
      protected:
         GlobalMemoryManager* m_memory_manager;
         std::set<Subsegment> m_table;

      public:
         SegmentTable(GlobalMemoryManager *memory_manager);

         void insert(uint64_t start, uint64_t end, uint64_t paddr, dstate_t state);
         dstate_t lookup(IntPtr vaddr, IntPtr &paddr);
   };

}
