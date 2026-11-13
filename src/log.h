#pragma once


#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

inline bool ColorOn() {
    static int on = -1;
    if (on < 0) {
        const char* no = getenv("NO_COLOR");
        const char* term = getenv("TERM");
        on = (no == nullptr && isatty(STDOUT_FILENO) && term != nullptr &&
              strcmp(term, "dumb") != 0)
                 ? 1
                 : 0;
    }
    return on != 0;
}

inline void LogEmit(const char* color, const char* fmt, va_list ap) {
    if (ColorOn()) fputs(color, stdout);
    vprintf(fmt, ap);
    if (ColorOn()) fputs("\033[0m", stdout);
    putchar('\n');
    fflush(stdout);
}

inline void LogOk(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    LogEmit("\033[32m", fmt, ap);
    va_end(ap);
}

inline void LogBad(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    LogEmit("\033[31m", fmt, ap);
    va_end(ap);
}

inline void LogInfo(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    LogEmit("\033[33m", fmt, ap);
    va_end(ap);
}
