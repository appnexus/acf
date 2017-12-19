#ifndef _AN_SYSLOG_H
#define _AN_SYSLOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <syslog.h>

#include "common/an_cc.h"

AN_EXTERN_C_BEGIN

/*
 * Initialize the syslog subsystem.
 */
void an_syslog_init(void);

/*
 * Clean up the syslog subsystem.
 */
void an_syslog_deinit(void);

/*
 * Initialize the consumer thread. Must be called after an_syslog_init().
 *
 * @param num_producers The maximum number of producers that will register
 * @return 0 on success, < 0 on error
 */
int an_syslog_init_consumer(unsigned int num_producers);

/*
 * Register this thread as a producer of syslog work.
 */
void an_syslog_register_producer(void);

/* Open a log in syslog. See openlog(3). */
void an_syslog_openlog(const char *ident, int option, int facility);

/* Close all logs opened by syslog. Optional. See closelog(3). */
void an_syslog_closelog(void);

/* Write to syslog. See syslog(3). */
void an_syslog(int priority, const char *format, ...) __attribute__((format(printf, 2, 3)));

/* Write to syslog. See syslog(3). */
void an_vsyslog(int priority, const char *format, va_list ap);

#define an_syslog_backtrace(priority, format, ...) \
    an_syslog_backtrace_internal(priority, format "\n", ##__VA_ARGS__)

/* Write a backtrace to syslog. See syslog(3) and backtrace(3). */
void an_syslog_backtrace_internal(int priority, const char *format, ...) __attribute__((format(printf, 2, 3)));

AN_EXTERN_C_END

#endif /* _AN_SYSLOG_H */
