#ifndef __SCHEDULER_GMM_H
#define __SCHEDULER_GMM_H

#include "scheduler_pinned.h"

class SchedulerGMM : public SchedulerPinned
{
   public:
      SchedulerGMM(ThreadManager *thread_manager)
         : SchedulerPinned(thread_manager)
      {
         m_group = ThreadManager::GMM_THREAD;
      }

      void threadSetInitialAffinity(thread_id_t thread_id) override
      {
         core_id_t core_id = thread_id;
         m_thread_info[thread_id].setAffinitySingle(core_id);
      }
};

#endif // __SCHEDULER_GMM_H
