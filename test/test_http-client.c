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

#include "test-util.h"

// Mocks ///////////////////////////////////////////////////////////////////////

int http_open_request_socket(struct http_request *request)
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

int http_close(struct http_request *request)
{
    check_expected(request);
    return mock();
}

// Tests ///////////////////////////////////////////////////////////////////////

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

    expect_any(http_open_request_socket, request);
    will_return(http_open_request_socket, 1);

    expect_any(http_begin_request, request);
    will_return(http_begin_request, 1);

    expect_any(http_end_header, request);

    int ret = http_get_request(&request);

    assert_true(ret > 0);
    assert_int_equal(HTTP_STATE_CLIENT_READ_BODY, request.state);
    assert_int_equal(200, request.status);
    assert_int_equal(606, request.content_length);
    assert_int_equal(0, request.line);
    assert_int_equal(0, request.line_len);

    close(fd);
}

static void test__http_get_request__returns_minus_one_if_http_open_request_socket_fails(void **states)
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

    expect_any(http_open_request_socket, request);
    will_return(http_open_request_socket, -1);

    int ret = http_get_request(&request);

    assert_int_equal(-1, ret);
    assert_int_equal(HTTP_STATE_ERROR, request.state);
    assert_int_equal(0, request.line);
    assert_int_equal(0, request.line_len);

    close(fd);
}

static void test__http_get_request__returns_minus_one_if_http_begin_request_fails(void **states)
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

    expect_any(http_open_request_socket, request);
    will_return(http_open_request_socket, 1);

    expect_any(http_begin_request, request);
    will_return(http_begin_request, -1);

    expect_any(http_close, request);
    will_return(http_close, 0);

    int ret = http_get_request(&request);

    assert_int_equal(-1, ret);
    assert_int_equal(HTTP_STATE_ERROR, request.state);
    assert_int_equal(0, request.line);
    assert_int_equal(0, request.line_len);

    close(fd);
}

static void test__http_get_request__returns_minus_one_if_header_is_incomplete(void **states)
{
    const char *reply =
        "HTTP/1.1 200 OK\r\n"
        "Accept-Ranges: bytes\r\n"
        "Content-Type: text/html\r\n";

    int fd = write_tmp_file(reply);

    struct http_request request = {
        .host = "www.example.com",
        .path = "/",

        .fd = fd,
    };

    expect_any(http_open_request_socket, request);
    will_return(http_open_request_socket, 1);

    expect_any(http_begin_request, request);
    will_return(http_begin_request, 1);

    expect_any(http_end_header, request);

    expect_any(http_close, request);
    will_return(http_close, 0);

    int ret = http_get_request(&request);

    assert_int_equal(-1, ret);
    assert_int_equal(HTTP_STATE_ERROR, request.state);
    assert_int_equal(0, request.line);
    assert_int_equal(0, request.line_len);

    close(fd);
}

static void test__http_get_request__returns_minus_one_if_header_does_not_parse_correctly(void **states)
{
    const char *reply =
        "HTTP/1.1 200 OK\r\n"
        "Accept-Ranges: bytes\rX\n"
        "Content-Type: text/html\r\n";

    int fd = write_tmp_file(reply);

    struct http_request request = {
        .host = "www.example.com",
        .path = "/",

        .fd = fd,
    };

    expect_any(http_open_request_socket, request);
    will_return(http_open_request_socket, 1);

    expect_any(http_begin_request, request);
    will_return(http_begin_request, 1);

    expect_any(http_end_header, request);

    expect_any(http_close, request);
    will_return(http_close, 0);

    int ret = http_get_request(&request);

    assert_int_equal(-1, ret);
    assert_int_equal(HTTP_STATE_ERROR, request.state);
    assert_int_equal(0, request.line);
    assert_int_equal(0, request.line_len);

    close(fd);
}

// Main ////////////////////////////////////////////////////////////////////////

const struct CMUnitTest tests_for_http_get_request[] = {
    cmocka_unit_test(test__http_get_request__parses_http_headers),
    cmocka_unit_test(test__http_get_request__returns_minus_one_if_http_open_request_socket_fails),
    cmocka_unit_test(test__http_get_request__returns_minus_one_if_http_begin_request_fails),
    cmocka_unit_test(test__http_get_request__returns_minus_one_if_header_is_incomplete),
    cmocka_unit_test(test__http_get_request__returns_minus_one_if_header_does_not_parse_correctly),
};

int main(void)
{
    int fails = 0;
    fails += cmocka_run_group_tests(tests_for_http_get_request, NULL, NULL);

    return fails;
}
