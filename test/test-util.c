#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <stdarg.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <signal.h>

#include "test-util.h"

static pid_t child_pid;

void LOG(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    printf("\n");
}

void LOG_ERROR(const char* str)
{
    perror(str);
}


int open_tmp_file(void)
{
    char filename[] = "/tmp/http-test-XXXXXX";
    int fd = mkstemp(filename);

    if(fd < 0) {
        perror("mkstemp");
    } else {
        unlink(filename);
    }
    return fd;
}

int write_string(int fd, const char *s)
{
    int num = 0;
    int len = strlen(s);

    while(num < len) {
        int n = write(fd, s, len);
        if(n < 0) {
            perror("write");
            break;
        }
        num += n;
        s += n;
    }
    return num;
}

int open_socket(pid_t *pid)
{
    int fds[2];
    if(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
        perror("socketpair");
        return -1;
    }

    *pid = fork();

    if(*pid < 0) {
        perror("fork");
        return -1;
    }

    if(*pid > 0) {
        close(fds[1]);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 1000;
        setsockopt(fds[0], SOL_SOCKET, SO_RCVTIMEO, (struct timeval *)&tv, sizeof(struct timeval));
        return fds[0];

    } else {
        close(fds[0]);
        return fds[1];
    }
}

void close_socket(int fd)
{
    kill(child_pid, SIGINT);

    int status;
    if(waitpid(child_pid, &status, 0) < 0) {
        perror("wait");
    }

    close(fd);
}

int write_socket(const char *str)
{
    int fd = open_socket(&child_pid);

    if(!child_pid) {
        write_string(fd, str);
        for(;;) {
        }
    }

    return fd;
}

int write_socket_n(const char *s[])
{
    int fd = open_socket(&child_pid);

    if(!child_pid) {
        while(*s) {
            if(write_string(fd, *s++) < 0)
            {
                break;
            }
        }

        for(;;) {
        }
    }

    return fd;
}

int write_tmp_file(const char *s)
{
    int fd = open_tmp_file();
    if(fd < 0) {
        return fd;
    }

    if(write_string(fd, s) < 0) {
        return -1;
    }

    if(lseek(fd, SEEK_SET, 0) != 0) {
        return -1;
    }

    return fd;
}

int write_tmp_file_n(const char *s[])
{
    int fd = open_tmp_file();
    if(fd < 0) {
        return fd;
    }

    while(*s) {
        if(write_string(fd, *s++) < 0)
        {
            return -1;
        }
    }

    if(lseek(fd, SEEK_SET, 0) != 0) {
        return -1;
    }

    return fd;
}

// Support functions for testing writes ////////////////////////////////////////

const char *get_file_content(int fd)
{
    static char buf[4096];

    off_t off;
    if((off = lseek(fd, 0, SEEK_SET)) < 0) {
        perror("lseek");
        return 0;
    }

    int n;

    if((n = read(fd, buf, sizeof(buf) - 1)) < 0) {
        perror("read");
        return 0;
    }

    buf[n] = 0;

    if(lseek(fd, off, SEEK_SET) < 0) {
        perror("lseek");
        return 0;
    }

    return buf;
}

// Trivial implementations of wrapped functions ////////////////////////////////

void *__real_malloc(size_t size);
void __real_free(void *ptr);
ssize_t __real_read(int fd, void *buf, size_t count);
ssize_t __real_write(int fd, const void *buf, size_t count);


void *__wrap_malloc(size_t size) __attribute__((weak));
void __wrap_free(void *ptr) __attribute__((weak));
ssize_t __wrap_read(int fd, void *buf, size_t count) __attribute__((weak));
ssize_t __wrap_write(int fd, const void *buf, size_t count) __attribute__((weak));

void *__wrap_malloc(size_t size)
{
    return __real_malloc(size);
}

void __wrap_free(void *ptr)
{
    __real_free(ptr);
}

ssize_t __wrap_read(int fd, void *buf, size_t count)
{
    return __real_read(fd, buf, count);
}

ssize_t __wrap_write(int fd, const void *buf, size_t count)
{
    return __real_write(fd, buf, count);
}
