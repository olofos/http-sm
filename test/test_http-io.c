#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "unity.h"

#include "http.h"

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
        int c = http_fgetc(&request);
        TEST_ASSERT_EQUAL(str[i], c);
    }

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
        int c = http_fgetc(&request);
        TEST_ASSERT_EQUAL(str[i], c);
    }

    for(int i = 0; i < strlen(str); i++) {
        int c = http_fgetc(&request);
        TEST_ASSERT_EQUAL(str[i], c);
    }

    close(fd);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test__http_getc__can_read_correctly_with_te_identity);
    RUN_TEST(test__http_getc__can_read_correctly_with_te_chunked);

    return UNITY_END();
}
