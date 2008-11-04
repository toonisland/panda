// Filename: threadSimpleManager.cxx
// Created by:  drose (19Jun07)
//
////////////////////////////////////////////////////////////////////
//
// PANDA 3D SOFTWARE
// Copyright (c) Carnegie Mellon University.  All rights reserved.
//
// All use of this software is subject to the terms of the revised BSD
// license.  You should have received a copy of this license along
// with this source code in a file named "LICENSE."
//
////////////////////////////////////////////////////////////////////

#include "threadSimpleManager.h"

#ifdef THREAD_SIMPLE_IMPL

#include "threadSimpleImpl.h"
#include "blockerSimple.h"
#include "mainThread.h"

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

bool ThreadSimpleManager::_pointers_initialized;
ThreadSimpleManager *ThreadSimpleManager::_global_ptr;

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::Constructor
//       Access: Private
//  Description: 
////////////////////////////////////////////////////////////////////
ThreadSimpleManager::
ThreadSimpleManager() :
  _simple_thread_epoch_timeslice
  ("simple-thread-epoch-timeslice", 0.01,
   PRC_DESC("When SIMPLE_THREADS is defined, this defines the amount of time, "
            "in seconds, that should be considered the "
            "typical timeslice for one epoch (to run all threads once).")),
  _simple_thread_window
  ("simple-thread-window", 1.0,
   PRC_DESC("When SIMPLE_THREADS is defined, this defines the amount of time, "
            "in seconds, over which to average all the threads' runtimes, "
            "for the purpose of scheduling threads.")),
  _simple_thread_low_weight
  ("simple-thread-low-weight", 0.1,
   PRC_DESC("When SIMPLE_THREADS is defined, this determines the relative "
            "amount of time that is given to threads with priority TP_low.")),
  _simple_thread_normal_weight
  ("simple-thread-normal-weight", 1.0,
   PRC_DESC("When SIMPLE_THREADS is defined, this determines the relative "
            "amount of time that is given to threads with priority TP_normal.")),
  _simple_thread_high_weight
  ("simple-thread-high-weight", 5.0,
   PRC_DESC("When SIMPLE_THREADS is defined, this determines the relative "
            "amount of time that is given to threads with priority TP_high.")),
  _simple_thread_urgent_weight
  ("simple-thread-urgent-weight", 10.0,
   PRC_DESC("When SIMPLE_THREADS is defined, this determines the relative "
            "amount of time that is given to threads with priority TP_urgent."))
{
  _tick_scale = 1000000.0;
  _total_ticks = 0;
  _current_thread = NULL;
  _clock = TrueClock::get_global_ptr();
  _waiting_for_exit = NULL;

#ifdef HAVE_POSIX_THREADS
  _posix_system_thread_id = pthread_self();
#endif
#ifdef WIN32
  _win32_system_thread_id = GetCurrentThreadId();
#endif

  // Install these global pointers so very low-level code (code
  // defined before the pipeline directory) can yield when necessary.
  global_thread_yield = &Thread::force_yield;
  global_thread_consider_yield = &Thread::consider_yield;
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::enqueue_ready
//       Access: Public
//  Description: Adds the indicated thread to the ready queue.  The
//               thread will be executed when its turn comes.  If the
//               thread is not the currently executing thread, its
//               _jmp_context should be filled appropriately.
//
//               If volunteer is true, the thread is volunteering to
//               sleep before its timeslice has been used up.  If
//               volunteer is false, the thread would still be running
//               if it could.
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
enqueue_ready(ThreadSimpleImpl *thread, bool volunteer) {
  // We actually add it to _next_ready, so that we can tell when we
  // have processed every thread in a given epoch.
  _next_ready.push_back(thread);
  if (volunteer) {
    ++_num_next_ready_volunteers;
  }
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::enqueue_sleep
//       Access: Public
//  Description: Adds the indicated thread to the sleep queue, until
//               the indicated number of seconds have elapsed.  Then
//               the thread will be automatically moved to the ready
//               queue.
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
enqueue_sleep(ThreadSimpleImpl *thread, double seconds) {
  if (thread_cat->is_debug()) {
    thread_cat.debug()
      << *_current_thread->_parent_obj << " sleeping for " 
      << seconds << " seconds\n";
  }

  double now = get_current_time();
  thread->_wake_time = now + seconds;
  _sleeping.push_back(thread);
  push_heap(_sleeping.begin(), _sleeping.end(), CompareStartTime());
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::enqueue_block
//       Access: Public
//  Description: Adds the indicated thread to the blocked queue for
//               the indicated blocker.  The thread will be awoken by
//               a later call to unblock_one() or unblock_all().
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
enqueue_block(ThreadSimpleImpl *thread, BlockerSimple *blocker) {
  _blocked[blocker].push_back(thread);
  blocker->_flags |= BlockerSimple::F_has_waiters;
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::unblock_one
//       Access: Public
//  Description: Unblocks one thread waiting on the indicated blocker,
//               if any.  Returns true if anything was unblocked,
//               false otherwise.
////////////////////////////////////////////////////////////////////
bool ThreadSimpleManager::
unblock_one(BlockerSimple *blocker) {
  Blocked::iterator bi = _blocked.find(blocker);
  if (bi != _blocked.end()) {
    nassertr(blocker->_flags & BlockerSimple::F_has_waiters, false);

    FifoThreads &threads = (*bi).second;
    nassertr(!threads.empty(), false);
    ThreadSimpleImpl *thread = threads.front();
    threads.pop_front();
    _ready.push_back(thread);
    if (threads.empty()) {
      blocker->_flags &= ~BlockerSimple::F_has_waiters;
      _blocked.erase(bi);
    }

    return true;
  }

  return false;
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::unblock_all
//       Access: Public
//  Description: Unblocks all threads waiting on the indicated
//               blocker.  Returns true if anything was unblocked,
//               false otherwise.
////////////////////////////////////////////////////////////////////
bool ThreadSimpleManager::
unblock_all(BlockerSimple *blocker) {
  Blocked::iterator bi = _blocked.find(blocker);
  if (bi != _blocked.end()) {
    nassertr(blocker->_flags & BlockerSimple::F_has_waiters, false);

    FifoThreads &threads = (*bi).second;
    nassertr(!threads.empty(), false);
    while (!threads.empty()) {
      ThreadSimpleImpl *thread = threads.front();
      threads.pop_front();
      _ready.push_back(thread);
    }
    blocker->_flags &= ~BlockerSimple::F_has_waiters;
    _blocked.erase(bi);
    return true;
  }
  return false;
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::enqueue_finished
//       Access: Public
//  Description: Adds the indicated thread to the finished queue.  
//               The manager will drop the reference count on the
//               indicated thread at the next epoch.  (A thread can't
//               drop its own reference count while it is running,
//               since that might deallocate its own stack.)
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
enqueue_finished(ThreadSimpleImpl *thread) {
  _finished.push_back(thread);
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::preempt
//       Access: Public
//  Description: Moves the indicated thread to the head of the ready
//               queue.  If it is not already on the ready queue, does
//               nothing.
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
preempt(ThreadSimpleImpl *thread) {
  FifoThreads::iterator ti;
  ti = find(_ready.begin(), _ready.end(), thread);
  if (ti != _ready.end()) {
    _ready.erase(ti);
    _ready.push_front(thread);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::next_context
//       Access: Public
//  Description: Switches out the currently executing thread and
//               chooses a new thread for execution.  Before calling
//               this, the current thread should have already
//               re-enqueued itself with a call to enqueue(), if it
//               intends to run again.
//
//               This will fill in the current thread's _jmp_context
//               member appropriately, and then change the global
//               current_thread pointer.
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
next_context() {
  // Delete any threads that need it.  We can't delete the current
  // thread, though.
  while (!_finished.empty() && _finished.front() != _current_thread) {
    ThreadSimpleImpl *finished_thread = _finished.front();
    _finished.pop_front();
    unref_delete(finished_thread->_parent_obj);
  }

  // Mark the current thread's resume point.

#ifdef HAVE_PYTHON
  // Save the current Python thread state.
  _current_thread->_python_state = PyThreadState_Swap(NULL);
#endif  // HAVE_PYTHON

#ifdef DO_PSTATS
  Thread::PStatsCallback *pstats_callback = _current_thread->_parent_obj->get_pstats_callback();
  if (pstats_callback != NULL) {
    pstats_callback->deactivate_hook(_current_thread->_parent_obj);
  }
#endif  // DO_PSTATS

  save_thread_context(&_current_thread->_context, st_choose_next_context, this);
  // Pass 2: we have returned into the context, and are now resuming
  // the current thread.

#ifdef DO_PSTATS
  if (pstats_callback != NULL) {
    pstats_callback->activate_hook(_current_thread->_parent_obj);
  }
#endif  // DO_PSTATS

#ifdef HAVE_PYTHON
  PyThreadState_Swap(_current_thread->_python_state);
#endif  // HAVE_PYTHON
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::prepare_for_exit
//       Access: Public
//  Description: Blocks until all running threads (other than the
//               current thread) have finished.  This only works when
//               called from the main thread; if called on any other
//               thread, nothing will happen.
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
prepare_for_exit() {
  if (!_current_thread->_parent_obj->is_exact_type(MainThread::get_class_type())) {
    return;
  }

  nassertv(_waiting_for_exit == NULL);
  _waiting_for_exit = _current_thread;

  // At this point, any non-joinable threads on any of the queues are
  // automatically killed.
  kill_non_joinable(_ready);

  Blocked::iterator bi = _blocked.begin();
  while (bi != _blocked.end()) {
    Blocked::iterator bnext = bi;
    ++bnext;
    BlockerSimple *blocker = (*bi).first;
    FifoThreads &threads = (*bi).second;
    kill_non_joinable(threads);
    if (threads.empty()) {
      blocker->_flags &= ~BlockerSimple::F_has_waiters;
      _blocked.erase(bi);
    }
    bi = bnext;
  }

  kill_non_joinable(_sleeping);

  next_context();

  // Delete any remaining threads.
  while (!_finished.empty() && _finished.front() != _current_thread) {
    ThreadSimpleImpl *finished_thread = _finished.front();
    _finished.pop_front();
    unref_delete(finished_thread->_parent_obj);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::set_current_thread
//       Access: Public
//  Description: Sets the initial value of the current_thread pointer,
//               i.e. the main thread.  It is valid to call this
//               method only exactly once.
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
set_current_thread(ThreadSimpleImpl *current_thread) {
  nassertv(_current_thread == (ThreadSimpleImpl *)NULL);
  _current_thread = current_thread;
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::remove_thread
//       Access: Public
//  Description: Removes the indicated thread from the accounting, for
//               instance just before the thread destructs.
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
remove_thread(ThreadSimpleImpl *thread) {
  TickRecords new_records;
  TickRecords::iterator ri;
  for (ri = _tick_records.begin(); ri != _tick_records.end(); ++ri) {
    if ((*ri)._thread != thread) {
      // Keep this record.
      new_records.push_back(*ri);
    } else {
      // Lose this record.
      nassertv(_total_ticks >= (*ri)._tick_count);
      _total_ticks -= (*ri)._tick_count;
    }
  }

  _tick_records.swap(new_records);
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::system_sleep
//       Access: Public, Static
//  Description: Calls the appropriate system sleep function to sleep
//               the whole process for the indicated number of
//               seconds.
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
system_sleep(double seconds) {
#ifdef WIN32
  Sleep((int)(seconds * 1000));

#else
  struct timespec rqtp;
  rqtp.tv_sec = time_t(seconds);
  rqtp.tv_nsec = long((seconds - (double)rqtp.tv_sec) * 1000000000.0);
  nanosleep(&rqtp, NULL);
#endif  // WIN32
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::write_status
//       Access: Public
//  Description: Writes a list of threads running and threads blocked.
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
write_status(ostream &out) const {
  out << "Currently running: " << *_current_thread->_parent_obj << "\n";

  out << "Ready:";
  FifoThreads::const_iterator ti;
  for (ti = _ready.begin(); ti != _ready.end(); ++ti) {
    out << " " << *(*ti)->_parent_obj;
  }
  for (ti = _next_ready.begin(); ti != _next_ready.end(); ++ti) {
    out << " " << *(*ti)->_parent_obj;
  }
  out << "\n";

  double now = get_current_time();
  out << "Sleeping:";
  // Copy and sort for convenience.
  Sleeping s2 = _sleeping;
  sort(s2.begin(), s2.end(), CompareStartTime());
  Sleeping::const_iterator si;
  for (si = s2.begin(); si != s2.end(); ++si) {
    out << " " << *(*si)->_parent_obj << "(" << (*si)->_wake_time - now
        << "s)";
  }
  out << "\n";

  Blocked::const_iterator bi;
  for (bi = _blocked.begin(); bi != _blocked.end(); ++bi) {
    BlockerSimple *blocker = (*bi).first;
    const FifoThreads &threads = (*bi).second;
    out << "On blocker " << blocker << ":\n";
    FifoThreads::const_iterator ti;
    for (ti = threads.begin(); ti != threads.end(); ++ti) {
      ThreadSimpleImpl *thread = (*ti);
      out << " " << *thread->_parent_obj;
#ifdef DEBUG_THREADS
      out << " (";
      thread->_parent_obj->output_blocker(out);
      out << ")";
#endif  // DEBUG_THREADS
    }
    out << "\n";
  }
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::system_yield
//       Access: Public, Static
//  Description: Calls the appropriate system function to yield
//               the whole process to any other system processes.
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
system_yield() {
#ifdef WIN32
  Sleep(0);

#else
  struct timespec rqtp;
  rqtp.tv_sec = 0;
  rqtp.tv_nsec = 0;
  nanosleep(&rqtp, NULL);
#endif  // WIN32
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::init_pointers
//       Access: Private, Static
//  Description: Should be called at startup to initialize the
//               simple threading system.
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
init_pointers() {
  if (!_pointers_initialized) {
    _pointers_initialized = true;
    _global_ptr = new ThreadSimpleManager;
    Thread::get_main_thread();

#ifdef HAVE_PYTHON
    // Ensure that the Python threading system is initialized and ready
    // to go.
    PyEval_InitThreads();
#endif
  }
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::st_choose_next_context
//       Access: Private, Static
//  Description: Select the next context to run.  Continuing the work
//               of next_context().
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
st_choose_next_context(void *data) {
  ThreadSimpleManager *self = (ThreadSimpleManager *)data;
  self->choose_next_context();
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::choose_next_context
//       Access: Private
//  Description: Select the next context to run.  Continuing the work
//               of next_context().
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
choose_next_context() {
  double now = get_current_time();

  do_timeslice_accounting(_current_thread, now);
  _current_thread = NULL;

  if (!_sleeping.empty()) {
    wake_sleepers(now);
  }

  bool new_epoch = !_ready.empty() && _next_ready.empty();

  // Choose a new thread to execute.
  while (true) {
    // If there are no threads, sleep.
    while (_ready.empty()) {
      if (!_next_ready.empty()) {
        // We've finished an epoch.
        bool all_volunteers = (_num_next_ready_volunteers == (int)_next_ready.size());
        _ready.swap(_next_ready);
        _num_next_ready_volunteers = 0;

        if (new_epoch && !_tick_records.empty()) {
          // Pop the oldest timeslice record off when we finish an
          // epoch without executing any threads, to ensure we don't
          // get caught in an "all threads reached budget" loop.
          if (thread_cat->is_debug()) {
            thread_cat.debug()
              << "All threads exceeded budget.\n";
          }
          TickRecord &record = _tick_records.front();
          _total_ticks -= record._tick_count;
          nassertv(record._thread->_run_ticks >= record._tick_count);
          record._thread->_run_ticks -= record._tick_count;
          _tick_records.pop_front();

        } else {
          // Otherwise, we're legitimately at the end of an epoch.
          if (all_volunteers) {
            // All of our non-blocked, non-sleeping threads have
            // voluntarily yielded.  Therefore, yield the whole
            // process.
            system_yield();
          }
        }
        new_epoch = true;
        
      } else if (!_sleeping.empty()) {
        // All threads are sleeping.
        double wait = _sleeping.front()->_wake_time - now;
        if (wait > 0.0) {
          if (thread_cat->is_debug()) {
            thread_cat.debug()
              << "Sleeping all threads " << wait << " seconds\n";
          }
          system_sleep(wait);
        }
        now = get_current_time();
        wake_sleepers(now);
        
      } else {
        // No threads are ready!
        if (!_blocked.empty()) {
          thread_cat->error()
            << "Deadlock!  All threads blocked.\n";
          report_deadlock();
          abort();
        }
        
        // All threads have finished execution.
        if (_waiting_for_exit != NULL) {
          // And one thread--presumably the main thread--was waiting for
          // that.
          _ready.push_back(_waiting_for_exit);
          _waiting_for_exit = NULL;
          break;
        }
        
        // No threads are queued anywhere.  This is some kind of
        // internal error, since normally the main thread, at least,
        // should be queued somewhere.
        thread_cat->error()
          << "All threads disappeared!\n";
        exit(0);
      }
    }

    ThreadSimpleImpl *chosen_thread = _ready.front();
    _ready.pop_front();
    
    double timeslice = determine_timeslice(chosen_thread);
    if (timeslice > 0.0) {
      // This thread is ready to roll.  Break out of the loop.
      chosen_thread->_start_time = now;
      chosen_thread->_stop_time = now + timeslice;
      _current_thread = chosen_thread;
      break;
    }

    // This thread is not ready to wake up yet.  Put it back for next
    // epoch.  It doesn't count as a volunteer, though--its timeslice
    // was used up.
    _next_ready.push_back(chosen_thread);
  }

  // All right, the thread is ready to roll.  Begin.
  if (thread_cat->is_debug()) {
    size_t blocked_count = 0;
    Blocked::const_iterator bi;
    for (bi = _blocked.begin(); bi != _blocked.end(); ++bi) {
      const FifoThreads &threads = (*bi).second;
      blocked_count += threads.size();
    }

    double timeslice = _current_thread->_stop_time - _current_thread->_start_time;
    thread_cat.debug()
      << "Switching to " << *_current_thread->_parent_obj
      << " for " << timeslice << " s ("
      << _ready.size() + _next_ready.size()
      << " other threads ready, " << blocked_count
      << " blocked, " << _sleeping.size() << " sleeping)\n";
  }

  switch_to_thread_context(&_current_thread->_context);

  // Shouldn't get here.
  nassertv(false);
  abort();
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::do_timeslice_accounting
//       Access: Private
//  Description: Records the amount of time the indicated thread has
//               run, and updates the moving average.
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
do_timeslice_accounting(ThreadSimpleImpl *thread, double now) {
  double elapsed = now - thread->_start_time;
  if (thread_cat.is_debug()) {
    thread_cat.debug()
      << *thread->_parent_obj << " ran for " << elapsed << " s of "
      << thread->_stop_time - thread->_start_time << " requested.\n";
  }
    
  nassertv(elapsed >= 0.0);
  unsigned int ticks = (unsigned int)(elapsed * _tick_scale + 0.5);
  thread->_run_ticks += ticks;

  // Now remove any old records.
  unsigned int ticks_window = (unsigned int)(_simple_thread_window * _tick_scale + 0.5);
  while (_total_ticks > ticks_window) {
    nassertv(!_tick_records.empty());
    TickRecord &record = _tick_records.front();
    _total_ticks -= record._tick_count;
    nassertv(record._thread->_run_ticks >= record._tick_count);
    record._thread->_run_ticks -= record._tick_count;
    _tick_records.pop_front();
  }

  // Finally, record the new record.
  TickRecord record;
  record._tick_count = ticks;
  record._thread = thread;
  _tick_records.push_back(record);
  _total_ticks += ticks;
}


////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::wake_sleepers
//       Access: Private
//  Description: Moves any threads due to wake up from the sleeping
//               queue to the ready queue.
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
wake_sleepers(double now) {
  while (!_sleeping.empty() && _sleeping.front()->_wake_time <= now) {
    ThreadSimpleImpl *thread = _sleeping.front();
    pop_heap(_sleeping.begin(), _sleeping.end(), CompareStartTime());
    _sleeping.pop_back();
    _ready.push_back(thread);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::report_deadlock
//       Access: Private
//  Description: 
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
report_deadlock() {
  Blocked::const_iterator bi;
  for (bi = _blocked.begin(); bi != _blocked.end(); ++bi) {
    BlockerSimple *blocker = (*bi).first;
    const FifoThreads &threads = (*bi).second;
    thread_cat.info()
      << "On blocker " << blocker << ":\n";
    FifoThreads::const_iterator ti;
    for (ti = threads.begin(); ti != threads.end(); ++ti) {
      ThreadSimpleImpl *thread = (*ti);
      thread_cat.info()
        << "  " << *thread->_parent_obj;
#ifdef DEBUG_THREADS
      thread_cat.info(false) << " (";
      thread->_parent_obj->output_blocker(thread_cat.info(false));
      thread_cat.info(false) << ")";
#endif  // DEBUG_THREADS
      thread_cat.info(false) << "\n";
    }
  }
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::determine_timeslice
//       Access: Private
//  Description: Determines the amount of time that should be
//               allocated to the next timeslice of this thread, based
//               on its priority weight and the amount of time it has
//               run recently relative to other threads.
////////////////////////////////////////////////////////////////////
double ThreadSimpleManager::
determine_timeslice(ThreadSimpleImpl *chosen_thread) {
  if (_ready.empty() && _next_ready.empty()) {
    // This is the only ready thread.  It gets the full timeslice.
    return _simple_thread_epoch_timeslice;
  }

  // Count up the total runtime and weight of all ready threads.
  unsigned int total_ticks = chosen_thread->_run_ticks;
  double total_weight = chosen_thread->_priority_weight;

  FifoThreads::const_iterator ti;
  for (ti = _ready.begin(); ti != _ready.end(); ++ti) {
    total_ticks += (*ti)->_run_ticks;
    total_weight += (*ti)->_priority_weight;
  }
  for (ti = _next_ready.begin(); ti != _next_ready.end(); ++ti) {
    total_ticks += (*ti)->_run_ticks;
    total_weight += (*ti)->_priority_weight;
  }

  nassertr(total_weight != 0.0, 0.0);
  double budget_ratio = chosen_thread->_priority_weight / total_weight;

  if (total_ticks == 0) {
    // This must be the first thread.  Special case.
    return budget_ratio * _simple_thread_epoch_timeslice;
  }

  double run_ratio = (double)chosen_thread->_run_ticks / (double)total_ticks;
  double remaining_ratio = budget_ratio - run_ratio;

  if (thread_cat->is_debug()) {
    thread_cat.debug()
      << *chosen_thread->_parent_obj << " accrued "
      << chosen_thread->_run_ticks / _tick_scale << " s of "
      << total_ticks / _tick_scale << "; budget is "
      << budget_ratio * total_ticks / _tick_scale << ".\n";
    if (remaining_ratio <= 0.0) {
      thread_cat.debug()
        << "Exceeded budget.\n";
    }
  }

  return remaining_ratio * _simple_thread_epoch_timeslice;
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::kill_non_joinable
//       Access: Private
//  Description: Removes any non-joinable threads from the indicated
//               queue and marks them killed.
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
kill_non_joinable(ThreadSimpleManager::FifoThreads &threads) {
  FifoThreads new_threads;
  FifoThreads::iterator ti;
  for (ti = threads.begin(); ti != threads.end(); ++ti) {
    ThreadSimpleImpl *thread = (*ti);
    if (thread->_joinable) {
      new_threads.push_back(thread);
    } else {
      if (thread_cat->is_debug()) {
        thread_cat.debug()
          << "Killing " << *thread->_parent_obj << "\n";
      }
      thread->_status = ThreadSimpleImpl::S_killed;
      enqueue_finished(thread);
    }
  }

  threads.swap(new_threads);
}

////////////////////////////////////////////////////////////////////
//     Function: ThreadSimpleManager::kill_non_joinable
//       Access: Private
//  Description: Removes any non-joinable threads from the indicated
//               queue and marks them killed.
////////////////////////////////////////////////////////////////////
void ThreadSimpleManager::
kill_non_joinable(ThreadSimpleManager::Sleeping &threads) {
  Sleeping new_threads;
  Sleeping::iterator ti;
  for (ti = threads.begin(); ti != threads.end(); ++ti) {
    ThreadSimpleImpl *thread = (*ti);
    if (thread->_joinable) {
      new_threads.push_back(thread);
    } else {
      if (thread_cat->is_debug()) {
        thread_cat.debug()
          << "Killing " << *thread->_parent_obj << "\n";
      }
      thread->_status = ThreadSimpleImpl::S_killed;
      enqueue_finished(thread);
    }
  }
  make_heap(new_threads.begin(), new_threads.end(), CompareStartTime());
  threads.swap(new_threads);
}

#endif // THREAD_SIMPLE_IMPL
