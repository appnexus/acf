#include <stdarg.h>
#include <syslog.h>

#include "an_syslog.h"

void
an_syslog(int priority, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	syslog(priority, format, args);
	va_end(args);
}
