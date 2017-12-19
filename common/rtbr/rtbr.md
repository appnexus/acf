Real-Time-Based Reclamation: not your forebears' TBR
====================================================

RTBR takes the EPC (epoch proxy collection), mixes it with the
time-ordered pool trick of pool party, and squirts a bit of ParSec in
there.  With constant forward progress and minimal OS cooperation,
we're getting rid of the store/load barrier in epoch SMR.  We also
centralise all but one of our transaction managers (we still have to
deal with SSTM, but that's just more work).

The basic theory goes as follows.  Assume all threads are always
making forward progress (always have an active section).  Each thread
can keep track of the oldest active section it owns, and we can scan
the list of all threads to find the oldest active section in the
system.  Anything that was logically deleted before that section was
created is OK to delete physically.  In practice, time is hard and
even RDTSC has some clock *offset* because the BIOS doesn't boot
everything simultanenously; I've observed offset < 200K cycles for 4
sockets, and the current config assumes it can be as bad as 1M cycles.

We also use the ParSec trick to get some easy progress while marking
sleeping sections without incurring RDTSC: we find the current time
when we open a new section, but, when we notice that all sections are
closed, only advance the timestamp to make it even (inactive), without
bothering to compute a real timestamp.  Reclamation can still note
that no task created before the inactive timestamp is still running on
that thread.  When no thread is idle for too long, we don't even need
to get the OS involved.

The hard part is handling threads that are idle for a long time.  We
could assume that idle threads will still check in from time to time
and update their "oldest section" timestamp, but that doesn't seem
ideal.  We instead rely on the operating system to let us detect
sleeping tasks and context switches.

Sleeping tasks are straightforward:

1. Time is now T0;
2. Task is sleeping at time T1 > T0;
3. Task has no visible active section at time T2 > T1.

We can assume that all active sections in that tasks were either
terminated before T2, or created after T0.  In either case, it is now
safe to physically delete resources that were logically released
before T0.  We can assume that because sleeping implies a context
switch, which implies a memory barrier.  Even if the next instruction
the task will execute creates a new section with a timestamp before
T0, the section itself will execute after T0.

For running tasks, we must detect context switches that happen between
a timestamp in the past and now.  The sequence is:

1. Time is T0;
2. Observe that the task has used N1 units of CPU time at T1 > T0;
3. Task has no visible active section at T2 > T1;
3. Observe that the task has used N3 > N1 units of CPU time at T3 > T2;
4. Task has no visible active section at T4 > T3.

The change in units of CPU time implies a context switch to update the
counters in kernel space, and that implies a memory barrier.  Thus,
any section that started execution before T0 must be visible by T4.
If we have no visible active section by T4, there was either no such
section, or all such sections are completed.  In either case, it is
safe to reclaim for time T0.

A task can also be outright dead.  We detect that by noticing that the
task is either gone from `/proc`, or that the task's creation
timestamp changed.

Directory layout
----------------

* `rtbr.c`: entry points, simple logic, iterating over all active
  records.
* `rtbr_tid.c`: `/proc/pid/stat` tricks to find dead/reborn tasks and
  track runqueue status and CPU time usage.
* `rtbr_record.c`: atomically acquire an RTBR record and expose enough
  info to detect thread shutdown.
* `rtbr_poll.c`: RTBR poll, both fast path and slow path with all the
  scheduler and implied membar trickery.
* `rtbr_impl.c`: record allocation.  We CAS race to allocate
  geometrically growing arrays of records.  Once an array is visible,
  we never free it (we reuse records with a stack-as-free-list).
