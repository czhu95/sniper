#pragma once

#include <map>
#include <queue>

#include "shmem_req.h"
#include "req_queue_list_template.h"

namespace SingleLevelMemory
{
   class ReqQueueList : public ReqQueueListTemplate<ShmemReq>
   {
      typedef std::map<IntPtr, std::queue<ShmemReq*>* >::iterator ReqQueueListIterator;
      struct key_iterator : public ReqQueueListIterator
      {
         key_iterator() : ReqQueueListIterator() {};
         key_iterator(ReqQueueListIterator it) : ReqQueueListIterator(it) {}
         IntPtr operator*() { return ReqQueueListIterator::operator*().first; }
      };
      public:
         key_iterator begin() { return m_req_queue_list.begin(); };
         key_iterator end() { return m_req_queue_list.end(); };
   };
}
