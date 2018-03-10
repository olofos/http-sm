#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <cmocka.h>

#include "http.h"

static int open_tmp_file(void);
static int write_tmp_file(const char *s);

void LOG(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    printf("\n");
}


int http_open(struct http_request *request)
{
    check_expected(request);
    return mock();
}

int http_begin_request(struct http_request *request)
{
    check_expected(request);
    return mock();
}

void http_end_header(struct http_request *request)
{
    check_expected(request);
}


static void test__http_get_request__parses_http_headers(void **states)
{
    const char *reply =
        "HTTP/1.1 200 OK\r\n"
        "Accept-Ranges: bytes\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: 606\r\n"
        "\r\n";

    int fd = write_tmp_file(reply);

    struct http_request request = {
        .host = "www.example.com",
        .path = "/",

        .fd = fd,
    };

    expect_any(http_open, request);
    will_return(http_open, 1);

    expect_any(http_begin_request, request);
    will_return(http_begin_request, 1);

    expect_any(http_end_header, request);

    int ret = http_get_request(&request);

    assert_true(ret > 0);
    assert_int_equal(HTTP_STATE_READ_BODY, request.state);
    assert_int_equal(200, request.status);
    assert_int_equal(606, request.content_length);
    assert_int_equal(0, request.line);
    assert_int_equal(0, request.line_len);

    close(fd);
}

const struct CMUnitTest tests_for_http_get_request[] = {
    cmocka_unit_test(test__http_get_request__parses_http_headers),
};

int main(void)
{
    return cmocka_run_group_tests(tests_for_http_get_request, NULL, NULL);
}

// Support functions for testing reads /////////////////////////////////////////

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
