#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <cmocka.h>

#include "http-sm/http.h"

#include "test-util.h"

// Mocks ///////////////////////////////////////////////////////////////////////

static int enable_malloc_mock = 0;

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

void *__real_malloc(size_t size);

void *__wrap_malloc(size_t size)
{
    if(enable_malloc_mock) {
        check_expected(size);
        return (void *)mock();
    } else {
        return __real_malloc(size);
    }
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

    char c;
    int n = read(fd, &c, 1);

    assert_true(ret > 0);
    assert_int_equal(HTTP_STATE_CLIENT_READ_BODY, request.state);
    assert_int_equal(200, request.status);
    assert_int_equal(606, request.read_content_length);
    assert_string_equal("text/html", request.content_type);
    assert_int_equal(0, request.line);
    assert_int_equal(0, request.line_length);
    assert_int_equal(0, n);

    free(request.content_type);

    close(fd);
}

static void test__http_get_request__returns_minus_one_if_http_open_request_socket_fails(void **states)
{
    const char *reply =
        "HTTP/1.1 200 OK\r\n"
        "Accept-Ranges: bytes\r\n"
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
    assert_true(http_is_error(&request));
    assert_int_equal(0, request.line);
    assert_int_equal(0, request.line_length);

    close(fd);
}

static void test__http_get_request__returns_minus_one_if_http_begin_request_fails(void **states)
{
    const char *reply =
        "HTTP/1.1 200 OK\r\n"
        "Accept-Ranges: bytes\r\n"
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
    assert_true(http_is_error(&request));
    assert_int_equal(0, request.line);
    assert_int_equal(0, request.line_length);

    close(fd);
}

static void test__http_get_request__returns_minus_one_if_header_is_incomplete(void **states)
{
    const char *reply =
        "HTTP/1.1 200 OK\r\n"
        "Accept-Ranges: bytes\r\n"
        "Content-Length: 606\r\n";

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
    assert_true(http_is_error(&request));
    assert_int_equal(0, request.line);
    assert_int_equal(0, request.line_length);

    close(fd);
}

static void test__http_get_request__returns_minus_one_if_header_does_not_parse_correctly(void **states)
{
    const char *reply =
        "HTTP/1.1 200 OK\r\n"
        "Accept-Ranges: bytes\rX\n"
        "Content-Length: 606\r\n";

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
    assert_true(http_is_error(&request));
    assert_int_equal(0, request.line);
    assert_int_equal(0, request.line_length);

    close(fd);
}

static void test__http_get_request__returns_minus_one_if_malloc_fails(void **states)
{
    const char *reply =
        "HTTP/1.1 200 OK\r\n"
        "Accept-Ranges: bytes\r\n"
        "Content-Length: 606\r\n";

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

    expect_any(__wrap_malloc, size);
    will_return(__wrap_malloc, 0);

    expect_any(http_close, request);
    will_return(http_close, 0);

    int ret = http_get_request(&request);

    assert_int_equal(-1, ret);
    assert_true(http_is_error(&request));
    assert_int_equal(0, request.line);
    assert_int_equal(0, request.line_length);
    assert_int_equal(HTTP_STATE_CLIENT_ERROR, request.state);

    close(fd);
}


// Setup & Teardown ////////////////////////////////////////////////////////////

static int gr_setup_malloc_mock(void **state)
{
    enable_malloc_mock = 1;
    return 0;
}

static int gr_teardown_malloc_mock(void **state)
{
    enable_malloc_mock = 0;
    return 0;
}


// Main ////////////////////////////////////////////////////////////////////////

const struct CMUnitTest tests_for_http_get_request[] = {
    cmocka_unit_test(test__http_get_request__parses_http_headers),
    cmocka_unit_test(test__http_get_request__returns_minus_one_if_http_open_request_socket_fails),
    cmocka_unit_test(test__http_get_request__returns_minus_one_if_http_begin_request_fails),
    cmocka_unit_test(test__http_get_request__returns_minus_one_if_header_is_incomplete),
    cmocka_unit_test(test__http_get_request__returns_minus_one_if_header_does_not_parse_correctly),
};

const struct CMUnitTest tests_for_http_get_request_malloc_mock[] = {
    cmocka_unit_test(test__http_get_request__returns_minus_one_if_malloc_fails),
};

int main(void)
{
    int fails = 0;
    fails += cmocka_run_group_tests(tests_for_http_get_request, NULL, NULL);
    fails += cmocka_run_group_tests(tests_for_http_get_request_malloc_mock, gr_setup_malloc_mock, gr_teardown_malloc_mock);

    return fails;
}
