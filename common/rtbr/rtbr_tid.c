/*
 * RTBR TID: poll /proc/ for scheduling information on a task:
 *  1. is there such a task?
 *  2. what is the status of the task?
 *  3. when was the task started?
 *  4. how much CPU time (user and system) has the task used?
 *
 * Item 3 provides ABA protection against the kernel reusing tids, and
 * item 4 is interesting because it is only incremented after a
 * context switch.  If we observe a change in the values, the thread
 * went through at least one context switch, which also implies a
 * memory barrier.
 */

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "common/rtbr/rtbr_impl.h"
#include "common/util.h"

/* -(1ULL << 127).  Should be big enough (:  */
#define MAX_INT_STRING "-170141183460469231731687303715884105728 "

#define INPUT_STRING					\
	MAX_INT_STRING /* %d pid */			\
	"(0123456789abcdef) " /* comm (16 chars) */	\
	"R "           /* %c state */			\
	MAX_INT_STRING /* ppid */			\
	MAX_INT_STRING /* pgrp */			\
	MAX_INT_STRING /* session */			\
	MAX_INT_STRING /* tty_nr */			\
	MAX_INT_STRING /* tpgid */			\
	MAX_INT_STRING /* flags */			\
	MAX_INT_STRING /* minflt */			\
	MAX_INT_STRING /* cminflt */			\
	MAX_INT_STRING /* majflt */			\
	MAX_INT_STRING /* cmajflt */			\
	MAX_INT_STRING /* "%llu " &utime */		\
	MAX_INT_STRING /* "%llu " &stime */		\
	MAX_INT_STRING /* cutime */			\
	MAX_INT_STRING /* cstime */			\
	MAX_INT_STRING /* priority */			\
	MAX_INT_STRING /* nice */			\
	MAX_INT_STRING /* num threads */		\
	MAX_INT_STRING /* itrealvalue */		\
	MAX_INT_STRING /* "%llu " &starttime */


static pid_t
gettid(void)
{

        return syscall(__NR_gettid);
}

struct an_rtbr_tid_info
an_rtbr_tid_info(pid_t tid)
{
	char buf[sizeof(INPUT_STRING)];
	char path[256];
	struct an_rtbr_tid_info ret;
	int fd = -1;

	if (tid == 0) {
		tid = gettid();
	}

	memset(&ret, 0, sizeof(ret));
	ret.tid = tid;

	{
		int n;

		n = snprintf(path, sizeof(path), "/proc/%"PRIu64"/stat", (uint64_t)tid);
		assert((size_t)n < sizeof(path));
	}

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		ret.dead = true;
		goto out;
	}

	memset(buf, 0, sizeof(buf));
	an_readall(fd, buf, sizeof(buf) - 1);

	{
		unsigned long long start_time;
		unsigned long long utime;
		unsigned long long stime;
		int n;
		char state;

		n = sscanf(buf,
		    "%*d " /* pid */
		    "%*s " /* comm */
		    "%c  " /* &state */
		    "%*d " /* ppid */
		    "%*d " /* pgrp */
		    "%*d " /* session */
		    "%*d " /* tty_nr */
		    "%*d " /* tpgid */
		    "%*u " /* flags */
		    "%*u " /* minflt */
		    "%*u " /* cminflt */
		    "%*u " /* majflt */
		    "%*u " /* cmajflt */
		    "%llu " /* &utime */
		    "%llu " /* &stime */
		    "%*d " /* cutime */
		    "%*d " /* cstime */
		    "%*d " /* priority */
		    "%*d " /* nice */
		    "%*d " /* num threads */
		    "%*d " /* itrealvalue */
		    "%llu " /* &starttime */,
		    &state, &utime, &stime, &start_time);
		if (n != 4) {
			ret.dead = true;
			goto out;
		}

		ret.running = (state == 'R');
		/* Z: zombie. x/X: "dead" (some linuxism). */
		ret.dead = ((state == 'Z') || (state == 'x') || (state == 'X'));
		ret.start_time = (uint64_t)start_time;
		ret.total_time = (uint64_t)utime + (uint64_t)stime;
	}

out:
	if (fd >= 0) {
		close(fd);
	}

	return ret;
}
