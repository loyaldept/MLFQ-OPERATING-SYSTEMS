# Lab 4: Multi-Level Feedback Queue (MLFQ) Scheduler for Pintos

**Course:** CMSC 326 - Operating Systems  
**Authors:** Misha Pryshchepa & Zuhayr Abdullazhanov  
**Institution:** Bard College  
**Date:** November 2025

---

## Overview

This lab implements a Multi-Level Feedback Queue (MLFQ) scheduler for Pintos. The MLFQ scheduler uses 20 priority queues (0-19) with variable-length quantums to provide responsive scheduling for interactive tasks while maintaining fairness for long-running processes.

This implementation builds on our Lab 3 solution for `timer_sleep()`, which eliminated busy-waiting by blocking sleeping threads.

---

## Implementation

### Files Modified

We made changes to three core files:

1. **`src/devices/timer.c`** 
   - Maintains sleep list from Lab 3
   - Saves and restores MLFQ state when threads sleep/wake

2. **`src/threads/thread.h`** 
   - Defines MLFQ constants (20 queues, 50-tick boost interval)
   - Adds MLFQ fields to thread structure

3. **`src/threads/thread.c`** 
   - Implements 20 priority queues
   - Handles quantum tracking and priority demotion
   - Implements priority boosting every 50 ticks
   - Maintains backward compatibility with Lab 3

### Test Files Added

We integrated the three test files provided by Professor Sven:

- **`mlfqs2-fifo.c`** - Tests FIFO ordering at same priority
- **`mlfqs-longproc.c`** - Tests priority demotion over time
- **`mlfqs-shortlong.c`** - Tests interaction between short and long jobs

Modified supporting files:
- `src/tests/threads/Make.tests`
- `src/tests/threads/tests.c`
- `src/tests/threads/tests.h`

---

## MLFQ Rules Implemented

Our scheduler follows these five rules:

1. **Priority Ordering**: If Priority(A) > Priority(B), A runs (B doesn't)
2. **Round Robin**: Threads at same priority run in round-robin fashion
3. **New Threads Start High**: All new threads start at priority 19 (highest)
4. **Quantum Exhaustion**: After using a quantum, threads move down one priority level
5. **Priority Boosting**: Every 50 ticks, all threads boost to priority 19

---

## Key Design Decisions

### Array of 20 Queues
We use `static struct list mlfq_queues[20]` where each index represents a priority level. The scheduler searches from index 19 down to 0, taking the first available thread.

**Why this approach?**
- Simple and efficient (O(1) operations)
- Queue index directly maps to priority
- Round-robin happens naturally with list operations
- Easy to implement priority boosting

### Variable Quantum Lengths
- **Formula**: `quantum = (19 - priority) + 1`
- Priority 19: 1 tick (most responsive)
- Priority 18: 2 ticks
- Priority 0: 20 ticks (longest)

Higher priority threads get shorter quantums for better responsiveness.

### MLFQ State Preservation During Sleep
When a thread sleeps, we save:
- `mlfq_priority` - Current priority level
- `ticks_at_priority` - Quantum usage at that level

This prevents sleeping threads from being unfairly demoted.

---

## Building and Testing

### Compilation

```bash
cd pintos/src/threads
make clean
make
```

### Running Tests

#### With MLFQ Scheduler (Lab 4):
```bash
pintos -v -k -T 480 --bochs -- -q -mlfqs run mlfqs2-fifo
pintos -v -k -T 480 --bochs -- -q -mlfqs run mlfqs-longproc
pintos -v -k -T 480 --bochs -- -q -mlfqs run mlfqs-shortlong
```

#### Without MLFQ (Lab 3 compatibility):
```bash
pintos --bochs -- -q run alarm-single
pintos --bochs -- -q run alarm-multiple
```

### Expected Results

All three MLFQ tests should show:
- Consistent thread ordering (mlfqs2-fifo)
- Gradual priority demotion (mlfqs-longproc)
- Proper preemption behavior (mlfqs-shortlong)

Lab 3 alarm tests should still pass when run without `-mlfqs` flag.

---

## Implementation Details

### Scheduler Logic (`thread_tick`)

Every timer tick:
1. Increment `ticks_at_priority` for running thread
2. Calculate quantum for current priority
3. If quantum exhausted and not at minimum priority:
   - Decrement priority
   - Reset tick counter
4. Check if 50 ticks have passed since last boost
5. If yes, call `mlfq_boost_all()` to boost all threads

### Priority Boosting (`mlfq_boost_all`)

This function prevents starvation by:
1. Moving all threads from lower queues to queue 19
2. Iterating through `all_list` to boost blocked threads
3. Resetting quantum counters for all threads

**Critical**: We boost ALL threads including blocked/sleeping ones, as required by the assignment.

### Thread Scheduling (`next_thread_to_run`)

```c
for (i = 19; i >= 0; i--)
  if (!list_empty(&mlfq_queues[i]))
    return first thread from queue[i];
return idle_thread;
```

This ensures strict priority ordering.

---

## Backward Compatibility

The implementation uses `if (thread_mlfqs)` checks throughout to maintain Lab 3 functionality:

- When `-mlfqs` flag is **present**: Use MLFQ scheduler
- When `-mlfqs` flag is **absent**: Use simple round-robin with ready_list

This allows both Lab 3 and Lab 4 to work from the same codebase.

---

## Known Limitations

- We did not implement the advanced priority donation mechanism
- Nice values and load average functions are stubs (not required for this lab)
- Priority is not considered for threads waiting on locks/semaphores (as specified)

---

## References

- [Operating Systems: Three Easy Pieces - MLFQ Chapter](https://pages.cs.wisc.edu/~remzi/OSTEP/cpu-sched-mlfq.pdf)
- [Stanford Pintos Documentation](https://www.andrew.cmu.edu/course/14-712-s20/pintos/pintos_2.html)
- Pintos Official Documentation (Section 2.2.2)

---

## Testing Verification

To verify the implementation:

1. **Compile**: `make` in `src/threads`
2. **Run MLFQ tests**: All three should pass
3. **Run Lab 3 tests**: Should still work without `-mlfqs`
4. **Check diff**: Compare with original Pintos to see clean additions

---

## Design Document

See `design_document.pdf` for detailed explanation of:
- Data structures used
- Algorithm implementations
- Rationale for design choices
- Test descriptions and expected outputs

---

## Contact

**Misha Pryshchepa** - mp5720@bard.edu  
**Zuhayr Abdullazhanov** - za2328@bard.edu

---

## Acknowledgments

- Professor Sven Anderson for providing test cases and guidance
- Pintos development team for the base operating system
- OSTEP textbook for MLFQ algorithm explanation

---

*This implementation represents our original work for CMSC 326 Lab 4.*
