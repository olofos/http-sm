#ifndef LOG_H_
#define LOG_H_

void LOG(const char *fmt, ...);
void ERROR(const char *str);

#define DEBUG LOG
#define INFO LOG
#define WARNING LOG

extern const char *log_system;

#endif
