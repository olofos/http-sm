#include <stdio.h>
#include <stdarg.h>

#include "log.h"

const char *log_system = 0;

void LOG(const char *fmt, ...)
{
    if(log_system) {
        printf("[%s] ", log_system);
    }
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    printf("\n");
}
