#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <stdarg.h>

#include <cmocka.h>

#include "http.h"
#include "http-private.h"

void LOG(const char *fmt, ...)
{
    // va_list va;
    // va_start(va, fmt);
    // vprintf(fmt, va);
    // va_end(va);
    // printf("\n");
}

void create_request(struct http_request *request, int state)
{
    const int line_len = 32;

    request->state  = state;
    request->method = HTTP_METHOD_UNKNOWN;
    request->line = malloc(line_len);
    request->line_len = line_len;
    request->line_index = 0;
    request->path = 0;
    request->query = 0;
    request->host = 0;
    request->flags = 0;
    request->status = 0;
    request->error = 0;
    request->content_length = -1;
}

void free_request(struct http_request *request)
{
    free(request->line);
    free(request->path);
    free(request->host);
    free(request->query);
}


void parse_header_helper(struct http_request *request, const char *s)
{
    for(int i = 0; i < strlen(s); i++) {
        http_parse_header(request, s[i]);
    }
}


void test__http_parse_header__can_parse_get_request_without_query(void **state)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_REQ_METHOD);

    parse_header_helper(&request, "GET / HTTP/1.1\r\n");

    assert_int_equal(HTTP_METHOD_GET, request.method);
    assert_non_null(request.path);
    assert_null(request.query);
    assert_string_equal("/", request.path);
    assert_int_equal(HTTP_STATE_READ_HEADER, request.state);

    free_request(&request);
}

void test__http_parse_header__can_parse_get_request_with_query(void **state)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_REQ_METHOD);

    parse_header_helper(&request, "GET /test?a=1&b=2 HTTP/1.1\r\n");

    assert_int_equal(HTTP_METHOD_GET, request.method);
    assert_non_null(request.path);
    assert_non_null(request.query);
    assert_string_equal("/test", request.path);
    assert_string_equal("a=1&b=2", request.query);

    assert_int_equal(HTTP_STATE_READ_HEADER, request.state);

    free_request(&request);
}




void test__http_parse_header__can_parse_post_request(void **state)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_REQ_METHOD);

    parse_header_helper(&request, "POST / HTTP/1.1\r\n");

    assert_int_equal(HTTP_METHOD_POST, request.method);
    assert_non_null(request.path);
    assert_null(request.query);
    assert_string_equal("/", request.path);

    assert_int_equal(HTTP_STATE_READ_HEADER, request.state);

    free_request(&request);
}

void test__http_parse_header__unsupported_method_gives_error(void **state)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_REQ_METHOD);

    parse_header_helper(&request, "DELETE / HTTP/1.1\r\n");

    assert_int_equal(HTTP_METHOD_UNSUPPORTED, request.method);
    assert_int_equal(HTTP_STATE_ERROR, request.state);
    assert_int_equal(HTTP_STATUS_METHOD_NOT_ALLOWED, request.error);

    free_request(&request);
}

void test__http_parse_header__http_version_10_gives_error(void **state)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_REQ_METHOD);

    parse_header_helper(&request, "GET / HTTP/1.0\r\n");

    assert_int_equal(HTTP_STATE_ERROR, request.state);
    assert_int_equal(HTTP_STATUS_VERSION_NOT_SUPPORTED, request.error);
    free_request(&request);
}

void test__http_parse_header__unknown_http_version_gives_error(void **state)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_REQ_METHOD);

    parse_header_helper(&request, "GET / XX\r\n");

    assert_int_equal(HTTP_STATE_ERROR, request.state);
    assert_int_equal(HTTP_STATUS_BAD_REQUEST, request.error);
    free_request(&request);
}

void test__http_parse_header__missing_newline_gives_error(void **state)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_REQ_METHOD);

    parse_header_helper(&request, "GET / HTTP/1.1\rX");

    assert_int_equal(HTTP_STATE_ERROR, request.state);
    assert_int_equal(HTTP_STATUS_BAD_REQUEST, request.error);
    free_request(&request);
}

void test__http_parse_header__can_parse_host_header(void **state)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_HEADER);

    parse_header_helper(&request, "Host: www.example.com\r\n");

    assert_non_null(request.host);
    assert_string_equal("www.example.com", request.host);

    free_request(&request);
}

void test__http_parse_header__can_parse_accept_encoding_gzip(void **state)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_HEADER);

    parse_header_helper(&request, "Accept-Encoding: gzip, deflate\r\n");

    assert_int_equal(HTTP_FLAG_ACCEPT_GZIP, request.flags & HTTP_FLAG_ACCEPT_GZIP);
    free_request(&request);
}

void test__http_parse_header__can_parse_accept_encoding_no_gzip(void **state)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_HEADER);

    parse_header_helper(&request, "Accept-Encoding: deflate\r\n");

    assert_int_equal(0, request.flags & HTTP_FLAG_ACCEPT_GZIP);
    free_request(&request);
}

void test__http_parse_header__can_parse_transfer_encoding_chunked(void **state)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_HEADER);

    parse_header_helper(&request, "Transfer-Encoding: chunked\r\n");

    assert_int_equal(HTTP_FLAG_CHUNKED, request.flags & HTTP_FLAG_CHUNKED);

    free_request(&request);
}


void test__http_parse_header__can_parse_content_length(void **state)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_HEADER);

    parse_header_helper(&request, "Content-Length: 10\r\n");

    assert_int_equal(10, request.content_length);

    free_request(&request);
}


void test__http_parse_header__missing_newline_in_header_gives_error(void **state)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_HEADER);

    parse_header_helper(&request, "Host: www.example.com\rX");

    assert_int_equal(HTTP_STATE_ERROR, request.state);
    assert_int_equal(HTTP_STATUS_BAD_REQUEST, request.error);
    free_request(&request);
}


void test__http_parse_header__can_read_response(void **state)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_RESP_VERSION);

    parse_header_helper(&request, "HTTP/1.1 200 OK\r\n");

    assert_int_equal(HTTP_STATE_READ_HEADER, request.state);
    assert_int_equal(200, request.status);

    free_request(&request);
}



void test__http_urldecode__returns_the_length_of_the_decoded_string(void **state)
{
    assert_int_equal(0, http_urldecode(0, "", 0));
    assert_int_equal(4, http_urldecode(0, "ABCD", 0));
    assert_int_equal(1, http_urldecode(0, "%20", 0));
    assert_int_equal(1, http_urldecode(0, "+", 0));
    assert_int_equal(2, http_urldecode(0, "X%20", 0));
    assert_int_equal(2, http_urldecode(0, "%20X", 0));
    assert_int_equal(3, http_urldecode(0, "X%20X", 0));
    assert_int_equal(3, http_urldecode(0, "%20%20%20", 0));
}


void test__http_urldecode__copies_up_to_given_number_of_characters(void **state)
{
    char buf[4];
    int ret;

    ret = http_urldecode(buf, "ABC", sizeof(buf));
    assert_int_equal(3, ret);
    assert_string_equal("ABC", buf);

    ret = http_urldecode(buf, "%20", sizeof(buf));
    assert_int_equal(1, ret);
    assert_string_equal(" ", buf);

    ret = http_urldecode(buf, "+", sizeof(buf));
    assert_int_equal(1, ret);
    assert_string_equal(" ", buf);

    ret = http_urldecode(buf, "X%20", sizeof(buf));
    assert_int_equal(2, ret);
    assert_string_equal("X ", buf);

    ret = http_urldecode(buf, "%20X", sizeof(buf));
    assert_int_equal(2, ret);
    assert_string_equal(" X", buf);

    memset(buf, 0, sizeof(buf));
    ret = http_urldecode(buf, "ABC", 2);
    assert_int_equal(2, ret);
    assert_string_equal("AB", buf);

    memset(buf, 0, sizeof(buf));
    ret = http_urldecode(buf, "X%20Y", 2);
    assert_int_equal(2, ret);
    assert_string_equal("X ", buf);

    memset(buf, 0, sizeof(buf));
    ret = http_urldecode(buf, "XY%20", 2);
    assert_int_equal(2, ret);
    assert_string_equal("XY", buf);

    memset(buf, 0, sizeof(buf));
    ret = http_urldecode(buf, "%20%20%20", 2);
    assert_int_equal(2, ret);
    assert_string_equal("  ", buf);
}




const struct CMUnitTest tests_for_http_parse_header[] = {
    cmocka_unit_test(test__http_parse_header__can_parse_get_request_without_query),
    cmocka_unit_test(test__http_parse_header__can_parse_get_request_with_query),
    cmocka_unit_test(test__http_parse_header__can_parse_post_request),
    cmocka_unit_test(test__http_parse_header__unsupported_method_gives_error),
    cmocka_unit_test(test__http_parse_header__http_version_10_gives_error),
    cmocka_unit_test(test__http_parse_header__unknown_http_version_gives_error),
    cmocka_unit_test(test__http_parse_header__missing_newline_gives_error),
    cmocka_unit_test(test__http_parse_header__can_parse_host_header),
    cmocka_unit_test(test__http_parse_header__can_parse_accept_encoding_gzip),
    cmocka_unit_test(test__http_parse_header__can_parse_accept_encoding_no_gzip),
    cmocka_unit_test(test__http_parse_header__can_parse_transfer_encoding_chunked),
    cmocka_unit_test(test__http_parse_header__can_parse_content_length),
    cmocka_unit_test(test__http_parse_header__missing_newline_in_header_gives_error),
    cmocka_unit_test(test__http_parse_header__can_read_response),
};

const struct CMUnitTest tests_for_http_urldecode[] = {
    cmocka_unit_test(test__http_urldecode__returns_the_length_of_the_decoded_string),
    cmocka_unit_test(test__http_urldecode__copies_up_to_given_number_of_characters),
};


int main(void)
{
    int fails = 0;
    fails += cmocka_run_group_tests(tests_for_http_parse_header, NULL, NULL);
    fails += cmocka_run_group_tests(tests_for_http_urldecode, NULL, NULL);
    return fails;
}
