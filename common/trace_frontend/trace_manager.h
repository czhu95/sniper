#ifndef __TRACE_MANAGER_H
#define __TRACE_MANAGER_H

#include "fixed_types.h"
#include "semaphore.h"
#include "core.h" // for lock_signal_t and mem_op_t
#include "_thread.h"

#include <vector>

class TraceThread;

class TraceManager
{
   protected:
      class Monitor : public Runnable
      {
         private:
            void run();
            _Thread *m_thread;
            TraceManager *m_manager;
         public:
            Monitor(TraceManager *manager);
            ~Monitor();
            void spawn();
      };

      Monitor *m_monitor;
      std::vector<TraceThread *> m_threads;
      UInt32 m_num_threads_started;
      UInt32 m_num_threads_running;
      Semaphore m_done;
      std::vector<String> m_tracefiles;
      String m_trace_prefix;

      friend class Monitor;

      TraceManager();
      virtual ~TraceManager();

   public:
      void start();
      void stop();
      void mark_done();
      void wait();
      void run();
      virtual void init() = 0;
      virtual void cleanup() = 0;
      virtual void setupTraceFiles(int index) = 0;
      virtual thread_id_t createThread(app_id_t app_id, SubsecondTime time, thread_id_t creator_thread_id) = 0;
      virtual app_id_t createApplication(SubsecondTime time, thread_id_t creator_thread_id) = 0;
      virtual void signalStarted();
      virtual void signalDone(TraceThread *thread, SubsecondTime time, bool aborted);
      virtual void endApplication(TraceThread *thread, SubsecondTime time) = 0;
      void accessMemory(int core_id, Core::lock_signal_t lock_signal, Core::mem_op_t mem_op_type, IntPtr d_addr, char* data_buffer, UInt32 data_size);

      UInt64 getProgressExpect();
      virtual UInt64 getProgressValue() = 0;
};

class UserTraceManager : public TraceManager
{
   private:
      struct app_info_t
      {
         app_info_t()
            : thread_count(1)
            , num_threads(1)
            , num_runs(0)
         {}
         UInt32 thread_count;       //< Index counter for new thread's FIFO name
         UInt32 num_threads;        //< Number of active threads for this app (when zero, app is done)
         UInt32 num_runs;           //< Number of completed runs
      };

      const bool m_stop_with_first_app;
      const bool m_app_restart;
      const bool m_emulate_syscalls;
      UInt32 m_num_apps;
      UInt32 m_num_apps_nonfinish;  //< Number of applications that have yet to complete their first run
      std::vector<app_info_t> m_app_info;
      std::vector<String> m_responsefiles;
      Lock m_lock;

      String getFifoName(app_id_t app_id, UInt64 thread_num, bool response, bool create);
      thread_id_t newThread(app_id_t app_id, bool first, bool init_fifo, bool spawn, SubsecondTime time, thread_id_t creator_thread_id);

   public:
      UserTraceManager();
      ~UserTraceManager() override;
      void init() override;
      void cleanup() override;
      void setupTraceFiles(int index) override;
      thread_id_t createThread(app_id_t app_id, SubsecondTime time, thread_id_t creator_thread_id) override;
      app_id_t createApplication(SubsecondTime time, thread_id_t creator_thread_id) override;
      void signalDone(TraceThread *thread, SubsecondTime time, bool aborted) override;
      void endApplication(TraceThread *thread, SubsecondTime time) override;

      UInt64 getProgressValue() override;
};

class SystemTraceManager : public TraceManager
{
   private:
      UInt32 m_num_threads;

   public:
      SystemTraceManager();
      ~SystemTraceManager() override;
      void init() override;
      void cleanup() override;
      void setupTraceFiles(int index) override;
      thread_id_t createThread(app_id_t app_id, SubsecondTime time, thread_id_t creator_thread_id) override;
      app_id_t createApplication(SubsecondTime time, thread_id_t creator_thread_id) override;
      void signalDone(TraceThread *thread, SubsecondTime time, bool aborted) override;
      void endApplication(TraceThread *thread, SubsecondTime time) override;

      UInt64 getProgressValue() override;
};

#endif // __TRACE_MANAGER_H
