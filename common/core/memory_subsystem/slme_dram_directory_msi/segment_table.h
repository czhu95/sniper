#pragma once

// #include <map>
#include "lock.h"
#include "fixed_types.h"
#include <map>

typedef int32_t policy_id_t;

#define INVALID_POLICY      -1
#define DIRECTORY_COHERENCE 0
#define REPLICATION         1
#define ATOMIC_UPDATE       2
#define SUBSCRIPTION        3
#define ATOMIC_SWAP         4
#define HASH_CAS            5

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
         std::map<Segment, policy_id_t> m_table;
         Lock m_lock;

      public:
         SegmentTable();

         policy_id_t lookup(IntPtr vaddr);
         void command(uint64_t cmd_type, IntPtr start, uint64_t arg1);
         void create(IntPtr start, uint64_t length);
         void assign(IntPtr start, policy_id_t policy_id);
         core_id_t get_home(IntPtr vaddr);

         static bool bypassCache(policy_id_t policy_id);
   };
}
