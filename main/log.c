#include <stdio.h>
#include <stdarg.h>
#include <string.h>

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

void ERROR(const char* str)
{
    if(log_system) {
        printf("[%s] ", log_system);
    }
    perror(str);
}

const char *escaped_string(const char *in)
{
    static char out[256];
    int j = 0;

    for(int i = 0; i < strlen(in) && j < sizeof(out) - 2; i++) {
        if(in[i] == '\r') {
            out[j++] = '\\';
            out[j++] = 'r';
        } else if(in[i] == '\n') {
            out[j++] = '\\';
            out[j++] = 'n';
        } else {
            out[j++] = in[i];
        }
    }
    out[j] = 0;

    return out;
}
