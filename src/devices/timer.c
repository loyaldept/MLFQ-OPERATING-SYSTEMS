#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "devices/pit.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
  
/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

/* ===================================================================== */
/* These lines are from Lab 3:                                                   */
/* We added a list to keep track of threads that are sleeping.             */
/* Instead of busy-waiting in a loop, threads will be blocked and        */
/* added to this list. The timer interrupt will check this list          */
/* every tick and wake up threads when their sleep time is over.         */
/* ===================================================================== */
static struct list sleep_list;

/* from Lab3:                                                   */
/* This structure holds information about each sleeping thread.          */
/* We store which thread is sleeping, when it should wake up,            */
/* and a list element so we can link these in the sleep_list.            */
/* ===================================================================== */
/* for lab4:                                                   */
/* we also saveed the thread's MLFQ priority and tick count.               */
/* this way when the thread wakes up, it goes back to the same           */
/* priority queue it was in before sleeping. This prevents sleeping      */
/* threads from losing their place in the MLFQ scheduler.                */
/* ===================================================================== */
struct sleeping_thread
{
  struct thread *thread;              /* The sleeping thread */
  int64_t wake_tick;                  /* When to wake this thread */
  int saved_mlfq_priority;            /* LAB 4: Save priority during sleep */
  int saved_ticks_at_priority;        /* LAB 4: Save quantum usage during sleep */
  struct list_elem elem;              /* Link for the sleep_list */
};
/* ===================================================================== */

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);
static void real_time_delay (int64_t num, int32_t denom);

/* Sets up the timer to interrupt TIMER_FREQ times per second,
   and registers the corresponding interrupt. */
void
timer_init (void) 
{
  pit_configure_channel (0, 2, TIMER_FREQ);
  intr_register_ext (0x20, timer_interrupt, "8254 Timer");
  
  /* ================================================================= */
  /* form lab3:                                               */
  /* Initialized the sleep list as empty when the timer starts.         */
  /* ================================================================= */
  list_init (&sleep_list);
  /* ================================================================= */
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) 
{
  unsigned high_bit, test_bit;

  ASSERT (intr_get_level () == INTR_ON);
  printf ("Calibrating timer...  ");

  /* Approximate loops_per_tick as the largest power-of-two
     still less than one timer tick. */
  loops_per_tick = 1u << 10;
  while (!too_many_loops (loops_per_tick << 1)) 
    {
      loops_per_tick <<= 1;
      ASSERT (loops_per_tick != 0);
    }

  /* Refine the next 8 bits of loops_per_tick. */
  high_bit = loops_per_tick;
  for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
    if (!too_many_loops (loops_per_tick | test_bit))
      loops_per_tick |= test_bit;

  printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) 
{
  enum intr_level old_level = intr_disable ();
  int64_t t = ticks;
  intr_set_level (old_level);
  return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) 
{
  return timer_ticks () - then;
}

/* Sleeps for approximately TICKS timer ticks.  Interrupts must
   be turned on. */
/* ===================================================================== */
/* from lab3:                                                   */
/* our original version had a busy-waiting loop (while loop that kept    */
/* calling thread_yield). This wasted CPU time.                          */
/*                                                                        */
/* we created a sleeping_thread structure, added it to the    */
/* sleep_list, and then blocked the thread. The thread stops running       */
/* completely until the timer interrupt wakes it up later.               */
/* ===================================================================== */
/* additions for lab4:                                                   */
/* We also save the thread's MLFQ priority and quantum usage before      */
/* sleeping. When the thread wakes up, we restore these values so        */
/* the thread continues at the same priority it had before sleeping.     */
/* ===================================================================== */
void
timer_sleep (int64_t ticks) 
{
  int64_t start = timer_ticks ();
  struct sleeping_thread st;
  struct thread *cur = thread_current ();

  ASSERT (intr_get_level () == INTR_ON);
  
  /* Don't sleep if ticks is 0 or negative */
  if (ticks <= 0)
    return;

  /* Set up the sleeping thread structure */
  st.thread = cur;
  st.wake_tick = start + ticks;
  
 
  /* This lets the thread resume at the same priority after waking up. */
  if (thread_mlfqs)
    {
      st.saved_mlfq_priority = cur->mlfq_priority;
      st.saved_ticks_at_priority = cur->ticks_at_priority;
    }

  /* Add this thread to the sleep list and block it */
  /* Blocking means the thread stops running until it's unblocked */
  enum intr_level old_level = intr_disable ();
  list_push_back (&sleep_list, &st.elem);
  thread_block ();
  intr_set_level (old_level);
  
  /* REMOVED FOR LAB 3: The busy-waiting while loop */
  /* Original code that we replaced: */
  /* while (timer_elapsed (start) < ticks)  */
  /*   thread_yield ();                     */
}
/* ===================================================================== */

/* Sleeps for approximately MS milliseconds.  Interrupts must be
   turned on. */
void
timer_msleep (int64_t ms) 
{
  real_time_sleep (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts must be
   turned on. */
void
timer_usleep (int64_t us) 
{
  real_time_sleep (us, 1000 * 1000);
}

/* Sleeps for approximately NS nanoseconds.  Interrupts must be
   turned on. */
void
timer_nsleep (int64_t ns) 
{
  real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Busy-waits for approximately MS milliseconds.  Interrupts need
   not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_msleep()
   instead if interrupts are enabled. */
void
timer_mdelay (int64_t ms) 
{
  real_time_delay (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts need not
   be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_usleep()
   instead if interrupts are enabled. */
void
timer_udelay (int64_t us) 
{
  real_time_delay (us, 1000 * 1000);
}

/* Sleeps execution for approximately NS nanoseconds.  Interrupts
   need not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_nsleep()
   instead if interrupts are enabled.*/
void
timer_ndelay (int64_t ns) 
{
  real_time_delay (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) 
{
  printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
/* ===================================================================== */                                                  */
/* The original version just incremented ticks and called thread_tick(). */
/* We now check the sleep_list every tick to see if any    */
/* sleeping threads need to wake up. If a thread's wake_tick has been    */
/* reached, we remove it from the sleep_list and unblock it (which       */
/* adds it back to the ready queue so it can run again).                 */
/* ===================================================================== */
/* for lab4:                                                   */
/* Before unblocking a thread, we restore its MLFQ priority and          */
/* quantum usage that we saved when it went to sleep. This ensures       */
/* the thread continues at the same priority level it had before.        */
/* ===================================================================== */
static void
timer_interrupt (struct intr_frame *args UNUSED)
{
  struct list_elem *e;
  
  ticks++;
  thread_tick ();

  /* Walk through the sleep list and check each sleeping thread */
  e = list_begin (&sleep_list);
  while (e != list_end (&sleep_list))
    {
      struct sleeping_thread *st = list_entry (e, struct sleeping_thread, elem);
      
      /* Is it time to wake this thread? */
      if (ticks >= st->wake_tick)
        {
          struct thread *t = st->thread;
          
          /* addition made for lab4: restore the thread's MLFQ state before waking it up */
          /* this puts the thread back in the same priority queue it was */
          /* in before sleeping, with the same quantum usage. */
          if (thread_mlfqs)
            {
              t->mlfq_priority = st->saved_mlfq_priority;
              t->ticks_at_priority = st->saved_ticks_at_priority;
            }
          
          /* Remove from sleep list and wake up the thread */
          e = list_remove (e);
          thread_unblock (t);
        }
      else
        {
          /* Not time yet, check the next sleeping thread */
          e = list_next (e);
        }
    }
}
/* ===================================================================== */

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) 
{
  /* Wait for a timer tick. */
  int64_t start = ticks;
  while (ticks == start)
    barrier ();

  /* Run LOOPS loops. */
  start = ticks;
  busy_wait (loops);

  /* If the tick count changed, we iterated too long. */
  barrier ();
  return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) 
{
  while (loops-- > 0)
    barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) 
{
  /* Convert NUM/DENOM seconds into timer ticks, rounding down.
          
        (NUM / DENOM) s          
     ---------------------- = NUM * TIMER_FREQ / DENOM ticks. 
     1 s / TIMER_FREQ ticks
  */
  int64_t ticks = num * TIMER_FREQ / denom;

  ASSERT (intr_get_level () == INTR_ON);
  if (ticks > 0)
    {
      /* We're waiting for at least one full timer tick.  Use
         timer_sleep() because it will yield the CPU to other
         processes. */                
      timer_sleep (ticks); 
    }
  else 
    {
      /* Otherwise, use a busy-wait loop for more accurate
         sub-tick timing. */
      real_time_delay (num, denom); 
    }
}

/* Busy-wait for approximately NUM/DENOM seconds. */
static void
real_time_delay (int64_t num, int32_t denom)
{
  /* Scale the numerator and denominator down by 1000 to avoid
     the possibility of overflow. */
  ASSERT (denom % 1000 == 0);
  busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000)); 
}