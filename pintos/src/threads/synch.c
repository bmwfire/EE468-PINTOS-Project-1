/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value)
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0)
    {
      list_insert_ordered (&sema->waiters, &thread_current()->elem,
                           priority_semaphore_compare, NULL);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema)
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0)
    {
      sema->value--;
      success = true;
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema)
{
  enum intr_level old_level;
  struct thread *waiting_thread = NULL;
  struct thread *cur = thread_current();

  ASSERT (sema != NULL);

  old_level = intr_disable ();

  while (!list_empty (&sema->waiters))
  { // unblock all threads waiting for sema_up
    waiting_thread = list_entry (list_pop_back (&sema->waiters), struct thread, elem);
    thread_unblock(waiting_thread);
  }

  /* since sema->waiters is a list in descending order, waiting thread will
  have the highest priority of the newly ready threads. preempt if
  waiting_thread has higher priority than the currently running thread */
  sema->value++;
  if (waiting_thread != NULL &&
      waiting_thread->priority > cur->priority &&
      waiting_thread->status == THREAD_READY)
  {
    thread_yield();
  }

  intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void)
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++)
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_)
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++)
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
  lock->priority_lock = PRIORITY_FAKE;
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));
  struct thread * cur;
  struct thread *lock_holder;
  struct lock *lock_next;
  int lock_iter;
  enum intr_level old_level;

  old_level = intr_disable();
  cur = thread_current();
  lock_holder = lock->holder;
  lock_next = lock;
  lock_iter = 0;

  if(lock->holder != NULL && !thread_mlfqs)
  {
    cur->waiting_for_lock = lock;
  }

  while(!thread_mlfqs && lock_holder != NULL &&
      lock_holder->priority < cur->priority)
  {
    thread_given_set_priority(lock_holder, cur->priority, true);

    if(lock_next->priority_lock < cur->priority)
      lock_next->priority_lock = cur->priority;

    if(lock_holder->waiting_for_lock != NULL && lock_iter < LOCK_LEVEL)
    {
      lock_next = lock_holder->waiting_for_lock;
      lock_holder = lock_holder->waiting_for_lock->holder;
      lock_iter++;
    } else
      break;
  }

  sema_down (&lock->semaphore);
  lock->holder = thread_current (); /* Uncomment if fails test case */
  //lock_holder = cur;

  if(!thread_mlfqs)
  {
    lock->holder->waiting_for_lock = NULL;
    list_insert_ordered(&lock->holder->locks, &lock->lock_list_elem,
                        lock_priority_compare, NULL);
  }

  intr_set_level (old_level);
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  struct thread *curr;
  enum intr_level old_level;
  curr = thread_current();
  old_level = intr_disable();

  lock->holder = NULL;
  sema_up (&lock->semaphore);

  if(!thread_mlfqs)
  {
      list_remove(&lock->lock_list_elem); /* remove lock from thread's list of locks */
      lock->priority_lock = PRIORITY_FAKE;

      if(list_empty(&curr->locks))
      {
        curr->is_donated = false;
        thread_set_priority(curr->priority_original);
      }
      else
      {
        struct lock *lock_first;
        lock_first = list_entry(list_front(&curr->locks), struct lock,
        lock_list_elem);

        if(lock_first->priority_lock != PRIORITY_FAKE)
          thread_given_set_priority(curr, lock_first->priority_lock, true);
        else
          thread_set_priority(curr->priority_original);
      }
  }
  intr_set_level(old_level);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock)
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
    int highest_priority;               /* the highest priority thread waiting
                                           for this semaphore*/
  };

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock)
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  sema_init (&waiter.semaphore, 0);
  waiter.highest_priority = thread_current()->priority; /* current thread is
                                                           the only waiting
                                                          thread for semaphore*/
  //printf("cond_wait: inserting into list\n");
  list_insert_ordered (&cond->waiters, &waiter.elem, priority_semaphore_compare,
                       NULL);
  //printf("cond_wait: releasing lock\n");
  lock_release (lock);
  //printf("cond_wait: putting sema down\n");
  sema_down (&waiter.semaphore);
  //printf("cond_wait: acquiring lock\n");
  lock_acquire (lock);
  //printf("cond_wait: lock acquired \n");
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters))
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
/* used in list_insert_ordered() calls implemented in list.c.
Returns true when the lock containing e_1 has highest_priority > the
highest_priority of the semaphore containing e_1, thus the list wil be in
descending order. */
bool lock_priority_compare(const struct list_elem *e_1, const struct list_elem *e_2,
                            void *aux)
{
  ASSERT(e_1 != NULL);
  ASSERT(e_2 != NULL);

  int highest_priority_e1;
  int highest_priority_e2;

  struct lock *l_1 = list_entry(e_1, struct lock, lock_list_elem);
  struct lock *l_2 = list_entry(e_2, struct lock, lock_list_elem);

  if(list_empty(&(l_1->semaphore.waiters)))
  {
    //printf("lock_priority_compare(): list l1 empty \n");
    highest_priority_e1 = -1;
  } else
  {
    highest_priority_e1 = list_entry(list_begin(&(l_1->semaphore.waiters)), struct thread, elem)->priority;
  }

  if(list_empty(&(l_2->semaphore.waiters)))
  {
    //printf("lock_priority_compare(): list l2 empty \n");
    highest_priority_e2 = -1;
  } else
  {
    highest_priority_e2 = list_entry(list_begin(&(l_2->semaphore.waiters)), struct thread, elem)->priority;
  }
  return highest_priority_e1 >= highest_priority_e2; /* Change if errors */
}

/* used in list_insert_ordered() calls implemented in list.c.
Returns true when the semaphore containing e_1 has highest_priority > the
highest_priority of the semaphore containing e_1, thus the list wil be in
descending order. */
bool priority_semaphore_compare(const struct list_elem *e_1, const struct list_elem *e_2,
                                void *aux)
{
  ASSERT(e_1 != NULL);
  ASSERT(e_2 != NULL);

  struct semaphore_elem *s_1 = list_entry(e_1, struct semaphore_elem, elem);
  struct semaphore_elem *s_2 = list_entry(e_2, struct semaphore_elem, elem);

  return s_1->highest_priority > s_2->highest_priority;
}
