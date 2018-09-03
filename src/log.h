#ifndef LOG_H_
#define LOG_H_

void LOG(const char *fmt, ...);
void LOG_ERROR(const char *str);

extern const char *log_system;

#endif
