#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include "fixed_types.h"
#include "semaphore.h"
#include "core.h"
#include "lock.h"
#include "subsecond_time.h"

#include <vector>
#include <queue>

class TLS;
class Thread;
class Scheduler;

class ThreadManager
{
public:
   typedef void *(*thread_func_t)(void *);

   enum thread_group_t {
      USER_THREAD,
      GMM_THREAD,
      UNKNOWN_THREAD,
   };

   enum stall_type_t {
      STALL_UNSCHEDULED,      // Thread is not scheduled on any core
      STALL_BROKEN,           // Thread is on a core that suffered hardware failure
      STALL_JOIN,             // Thread is calling pthread_join
      STALL_MUTEX,            // Thread is calling pthread_mutex_lock
      STALL_COND,             // Thread is calling pthread_cond_wait
      STALL_BARRIER,          // Thread is calling pthread_barrier_wait
      STALL_FUTEX,            // Thread is calling syscall(SYS_futex, FUTEX_WAIT)
      STALL_PAUSE,            // pause system call
      STALL_SLEEP,            // sleep system call
      STALL_SYSCALL,          // blocking system call
      STALL_VCPU_HALT,        // system vcpu is being halted
      STALL_GMM_PULL,         // GMM core is waiting for message
      STALL_TYPES_MAX,
   };
   static const char* stall_type_names[];

   static thread_group_t getThreadGroup(thread_id_t);

   ThreadManager();
   virtual ~ThreadManager();

   virtual Lock &getLock(thread_id_t thread_id = INVALID_THREAD_ID) { return m_thread_lock; }
   Scheduler *getScheduler() const { return m_scheduler; }

   virtual Thread* createThread(app_id_t app_id, thread_id_t creator_thread_id) = 0;

   Thread *getThreadFromID(thread_id_t thread_id);
   Thread *getCurrentThread(int threadIndex = -1);
   UInt64 getNumThreads() const { return m_threads.size(); }
   Core::State getThreadState(thread_id_t thread_id) const { return m_thread_state.at(thread_id).status; }
   stall_type_t getThreadStallReason(thread_id_t thread_id) const { return m_thread_state.at(thread_id).stalled_reason; }

   Thread *findThreadByTid(pid_t tid);

   // services
   virtual thread_id_t spawnThread(thread_id_t thread_id, app_id_t app_id) = 0;
   virtual void joinThread(thread_id_t thread_id, thread_id_t join_thread_id) = 0;

   virtual thread_id_t getThreadToSpawn(SubsecondTime &time) = 0;
   void waitForThreadStart(thread_id_t thread_id, thread_id_t wait_thread_id);

   // events
   virtual void onThreadStart(thread_id_t thread_id, SubsecondTime time) = 0;
   virtual void onThreadExit(thread_id_t thread_id) = 0;

   // misc
   SubsecondTime stallThread(thread_id_t thread_id, stall_type_t reason, SubsecondTime time);
   void stallThread_async(thread_id_t thread_id, stall_type_t reason, SubsecondTime time);
   void resumeThread(thread_id_t thread_id, thread_id_t thread_id_by, SubsecondTime time, void *msg = NULL);
   void resumeThread_async(thread_id_t thread_id, thread_id_t thread_id_by, SubsecondTime time, void *msg = NULL);
   bool isThreadRunning(thread_id_t thread_id);
   bool isThreadInitializing(thread_id_t thread_id);
   bool anyThreadRunning();

   virtual void moveThread(thread_id_t thread_id, core_id_t core_id, SubsecondTime time) = 0;

   bool areAllCoresRunning();

protected:
   struct ThreadSpawnRequest
   {
      thread_id_t thread_by;
      thread_id_t thread_id;
      SubsecondTime time;
   };

   struct ThreadState
   {
      Core::State status;
      stall_type_t stalled_reason; //< If status == Core::STALLED, why?
      thread_id_t waiter;

      ThreadState() : status(Core::IDLE), waiter(INVALID_THREAD_ID) {}
   };

   Lock m_thread_lock;

   std::vector<ThreadState> m_thread_state;

   std::vector<Thread*> m_threads;
   TLS *m_thread_tls;

   Scheduler *m_scheduler;

   virtual Thread* createThread_unlocked(app_id_t app_id, thread_id_t creator_thread_id);
   void wakeUpWaiter(thread_id_t thread_id, SubsecondTime time);
};

class UserThreadManager: public ThreadManager
{
public:
   Thread* createThread(app_id_t app_id, thread_id_t creator_thread_id) override;

   thread_id_t spawnThread(thread_id_t thread_id, app_id_t app_id) override;
   void joinThread(thread_id_t thread_id, thread_id_t join_thread_id) override;

   thread_id_t getThreadToSpawn(SubsecondTime &time) override;

   void onThreadStart(thread_id_t thread_id, SubsecondTime time) override;
   void onThreadExit(thread_id_t thread_id) override;

   void moveThread(thread_id_t thread_id, core_id_t core_id, SubsecondTime time) override;

protected:
   std::queue<ThreadSpawnRequest> m_thread_spawn_list;
};

class SystemThreadManager: public ThreadManager
{
public:
   SystemThreadManager();
   ~SystemThreadManager() override;

   Thread* createThread(app_id_t app_id, thread_id_t creator_thread_id) override;

   thread_id_t spawnThread(thread_id_t thread_id, app_id_t app_id) override;
   void joinThread(thread_id_t thread_id, thread_id_t join_thread_id) override;

   thread_id_t getThreadToSpawn(SubsecondTime &time) override;

   void onThreadStart(thread_id_t thread_id, SubsecondTime time) override;
   void onThreadExit(thread_id_t thread_id) override;

   void moveThread(thread_id_t thread_id, core_id_t core_id, SubsecondTime time) override;

   Lock &getLock(thread_id_t thread_id) override;

   Scheduler *getGMMScheduler() const { return m_gmm_scheduler; }

protected:

   Lock m_gmm_thread_lock;
   Scheduler *m_gmm_scheduler;

   Thread* createThread_unlocked(app_id_t app_id, thread_id_t creator_thread_id) override;
};

#endif // THREAD_MANAGER_H
