#ifndef AN_SYSLOG_H
#define AN_SYSLOG_H

#include "an_cc.h"

AN_EXTERN_C_BEGIN

#include "acf_export.h"

ACF_EXPORT void AN_CC_WEAK an_syslog(int priority, const char *format, ...) __attribute__((format(printf, 2, 3)));

AN_EXTERN_C_END

#endif
