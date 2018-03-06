#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <signal.h>

#include "unity.h"

#include "http.h"

static pid_t child_pid;

static int open_tmp_file(void)
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

static int write_string(int fd, const char *s)
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

static int open_socket(pid_t *pid)
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

static void close_socket(int fd)
{
    kill(child_pid, SIGINT);

    int status;
    if(waitpid(child_pid, &status, 0) < 0) {
        perror("wait");
    }

    close(fd);
}

static int write_socket(const char *str)
{
    int fd = open_socket(&child_pid);

    if(!child_pid) {
        write_string(fd, str);
        for(;;) {
        }
    }

    return fd;
}

static int write_socket_n(const char *s[])
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


static int write_tmp_file(const char *s)
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

static int write_tmp_file_n(const char *s[])
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

static void init_request(struct http_request *request, int fd)
{
    memset(request, 0, sizeof(*request));
    request->fd = fd;
    request->poke = -1;
    request->state = HTTP_STATE_READ_BODY;
}

static void test__http_getc__can_read_correctly_with_te_identity(void)
{
    char *str = "0123";

    int fd = write_tmp_file(str);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

    struct http_request request;
    init_request(&request, fd);
    request.content_length = strlen(str);

    for(int i = 0; i < strlen(str); i++) {
        TEST_ASSERT_EQUAL(str[i], http_getc(&request));
    }

    close(fd);
}

static void test__http_getc__doesnt_read_more_than_content_length_with_te_identity(void)
{
    char *str = "01";

    int fd = write_tmp_file(str);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

    struct http_request request;
    init_request(&request, fd);
    request.content_length = 1;

    TEST_ASSERT_EQUAL('0', http_getc(&request));
    TEST_ASSERT_EQUAL(0, http_getc(&request));

    close(fd);
}

static void test__http_getc__can_read_correctly_with_te_chunked(void)
{
    char *str = "0123";
    const char *s[] = {
        "4\r\n",
        str,
        "\r\n",
        "4\r\n",
        str,
        "\r\n",
        "0\r\n",
        0
    };

    int fd = write_tmp_file_n(s);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

    struct http_request request;
    init_request(&request, fd);
    request.flags |= HTTP_FLAG_CHUNKED;

    request.content_length = strlen(str);

    for(int i = 0; i < strlen(str); i++) {
        int c = http_getc(&request);
        TEST_ASSERT_EQUAL(str[i], c);
    }

    for(int i = 0; i < strlen(str); i++) {
        int c = http_getc(&request);
        TEST_ASSERT_EQUAL(str[i], c);
    }

    close(fd);
}

static void test__http_getc__can_read_non_ascii_characters_with_te_identity(void)
{
    char *str = "\xBA\xAD\xF0\x0D";

    int fd = write_tmp_file(str);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

    struct http_request request;
    init_request(&request, fd);
    request.content_length = strlen(str);

    TEST_ASSERT_EQUAL(0xBA, http_getc(&request));
    TEST_ASSERT_EQUAL(0xAD, http_getc(&request));
    TEST_ASSERT_EQUAL(0xF0, http_getc(&request));
    TEST_ASSERT_EQUAL(0x0D, http_getc(&request));

    close(fd);
}

static void test__http_getc__can_read_non_ascii_characters_with_te_chunked(void)
{
    char *str = "\xBA\xAD\xF0\x0D";

    const char *s[] = {
        "4\r\n",
        str,
        "\r\n0\r\n",
        0
    };

    int fd = write_tmp_file_n(s);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

    struct http_request request;
    init_request(&request, fd);
    request.flags |= HTTP_FLAG_CHUNKED;

    TEST_ASSERT_EQUAL(0xBA, http_getc(&request));
    TEST_ASSERT_EQUAL(0xAD, http_getc(&request));
    TEST_ASSERT_EQUAL(0xF0, http_getc(&request));
    TEST_ASSERT_EQUAL(0x0D, http_getc(&request));

    close(fd);
}

static void test__http_getc__returns_zero_when_reading_eof_with_te_identity(void)
{
    char *str = "X";
    int fd = write_socket(str);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

    struct http_request request;
    init_request(&request, fd);
    request.content_length = strlen(str);

    TEST_ASSERT_EQUAL('X', http_getc(&request));
    TEST_ASSERT_EQUAL(0, http_getc(&request));
    TEST_ASSERT_EQUAL(0, http_getc(&request));

    close_socket(fd);
}

static void test__http_getc__returns_zero_when_reading_eof_with_te_chunked(void)
{
    char *str = "X";
    const char *s[] = {
        "1\r\n",
        str,
        "\r\n0\r\n",
        0
    };

    int fd = write_socket_n(s);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

    struct http_request request;
    init_request(&request, fd);
    request.flags |= HTTP_FLAG_CHUNKED;

    TEST_ASSERT_EQUAL('X', http_getc(&request));
    TEST_ASSERT_EQUAL(0, http_getc(&request));
    TEST_ASSERT_EQUAL(0, http_getc(&request));

    close_socket(request.fd);
}

void test__http_peek__returns_the_next_character_with_te_identity(void)
{
    char *str = "0123";

    int fd = write_tmp_file(str);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

    struct http_request request;
    init_request(&request, fd);
    request.content_length = strlen(str);

    TEST_ASSERT_EQUAL('0', http_peek(&request));
    TEST_ASSERT_EQUAL('0', http_peek(&request));
    TEST_ASSERT_EQUAL('0', http_getc(&request));
    TEST_ASSERT_EQUAL('1', http_getc(&request));
    TEST_ASSERT_EQUAL('2', http_peek(&request));
    TEST_ASSERT_EQUAL('2', http_peek(&request));
    TEST_ASSERT_EQUAL('2', http_getc(&request));

    close(fd);
}

void test__http_peek__returns_the_next_character_with_te_chunked(void)
{
    char *str = "0123";

    const char *s[] = {
        "4\r\n",
        str,
        "\r\n0\r\n",
        0
    };

    int fd = write_tmp_file_n(s);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

    struct http_request request;
    init_request(&request, fd);
    request.flags |= HTTP_FLAG_CHUNKED;

    TEST_ASSERT_EQUAL('0', http_peek(&request));
    TEST_ASSERT_EQUAL('0', http_peek(&request));
    TEST_ASSERT_EQUAL('0', http_getc(&request));
    TEST_ASSERT_EQUAL('1', http_getc(&request));
    TEST_ASSERT_EQUAL('2', http_peek(&request));
    TEST_ASSERT_EQUAL('2', http_peek(&request));
    TEST_ASSERT_EQUAL('2', http_getc(&request));

    close(fd);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test__http_getc__can_read_correctly_with_te_identity);
    RUN_TEST(test__http_getc__can_read_correctly_with_te_chunked);
    RUN_TEST(test__http_getc__doesnt_read_more_than_content_length_with_te_identity);

    RUN_TEST(test__http_getc__can_read_non_ascii_characters_with_te_identity);
    RUN_TEST(test__http_getc__can_read_non_ascii_characters_with_te_chunked);

    RUN_TEST(test__http_getc__returns_zero_when_reading_eof_with_te_identity);
    RUN_TEST(test__http_getc__returns_zero_when_reading_eof_with_te_chunked);

    RUN_TEST(test__http_peek__returns_the_next_character_with_te_identity);
    RUN_TEST(test__http_peek__returns_the_next_character_with_te_chunked);

    return UNITY_END();
}
