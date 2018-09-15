#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <stdarg.h>

#include <cmocka.h>

#include "http-sm/http.h"
#include "http-private.h"

#include "test-util.h"

// Tests ///////////////////////////////////////////////////////////////////////

static void create_server_request(struct http_request *request)
{
    const int line_len = 32;

    request->state  = HTTP_STATE_SERVER_READ_METHOD;
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
    request->read_content_length = -1;
    request->write_content_length = -1;
}

static void create_client_request(struct http_request *request)
{
    const int line_len = 32;

    request->state  = HTTP_STATE_CLIENT_READ_VERSION;
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
    request->read_content_length = -1;
    request->write_content_length = -1;
}

static void free_request(struct http_request *request)
{
    free(request->line);
    free(request->path);
    free(request->host);
    free(request->query);
}


static void parse_header_helper(struct http_request *request, const char *s)
{
    for(int i = 0; i < strlen(s); i++) {
        http_parse_header(request, s[i]);
    }
}


static void test__http_parse_header__can_parse_get_request_without_query(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.1\r\n");

    assert_int_equal(HTTP_METHOD_GET, request.method);
    assert_non_null(request.path);
    assert_null(request.query);
    assert_string_equal("/", request.path);

    free_request(&request);
}

static void test__http_parse_header__can_parse_get_request_with_query(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET /test?a=1&b=2 HTTP/1.1\r\n");

    assert_int_equal(HTTP_METHOD_GET, request.method);
    assert_non_null(request.path);
    assert_non_null(request.query);
    assert_string_equal("/test", request.path);
    assert_string_equal("a=1&b=2", request.query);

    free_request(&request);
}




static void test__http_parse_header__can_parse_post_request(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "POST / HTTP/1.1\r\n");

    assert_int_equal(HTTP_METHOD_POST, request.method);
    assert_non_null(request.path);
    assert_null(request.query);
    assert_string_equal("/", request.path);

    free_request(&request);
}

static void test__http_parse_header__can_parse_delete_request(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "DELETE / HTTP/1.1\r\n");

    assert_int_equal(HTTP_METHOD_DELETE, request.method);
    assert_non_null(request.path);
    assert_null(request.query);
    assert_string_equal("/", request.path);

    free_request(&request);
}

static void test__http_parse_header__unsupported_method_gives_error(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "UNKOWN / HTTP/1.1\r\n");

    assert_int_equal(HTTP_METHOD_UNSUPPORTED, request.method);
    assert_int_equal(HTTP_STATUS_METHOD_NOT_ALLOWED, request.error);

    free_request(&request);
}

static void test__http_parse_header__http_version_10_gives_error(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.0\r\n");

    assert_int_equal(HTTP_STATUS_VERSION_NOT_SUPPORTED, request.error);
    free_request(&request);
}

static void test__http_parse_header__unknown_http_version_gives_error(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / XX\r\n");

    assert_int_equal(HTTP_STATUS_BAD_REQUEST, request.error);
    free_request(&request);
}

static void test__http_parse_header__missing_newline_gives_error(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.1\rX");

    assert_int_equal(HTTP_STATUS_BAD_REQUEST, request.error);
    free_request(&request);
}

static void test__http_parse_header__can_parse_host_header_if_server(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.1\r\nHost: www.example.com\r\n");

    assert_non_null(request.host);
    assert_string_equal("www.example.com", request.host);

    free_request(&request);
}

static void test__http_parse_header__does_not_set_host_if_client(void **state)
{
    struct http_request request;
    create_client_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.1\r\nHost: www.example.com\r\n");

    assert_null(request.host);
    free_request(&request);
}

static void test__http_parse_header__can_parse_accept_encoding_gzip(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.1\r\nAccept-Encoding: gzip, deflate\r\n");

    assert_int_equal(HTTP_FLAG_ACCEPT_GZIP, request.flags & HTTP_FLAG_ACCEPT_GZIP);
    free_request(&request);
}

static void test__http_parse_header__can_parse_accept_encoding_no_gzip(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.1\r\nAccept-Encoding: deflate\r\n");

    assert_int_equal(0, request.flags & HTTP_FLAG_ACCEPT_GZIP);
    free_request(&request);
}

static void test__http_parse_header__does_not_set_accept_encoding_if_client(void **state)
{
    struct http_request request;
    create_client_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.1\r\nAccept-Encoding: gzip, deflate\r\n");

    assert_int_equal(0, request.flags & HTTP_FLAG_ACCEPT_GZIP);
    free_request(&request);
}

static void test__http_parse_header__can_parse_transfer_encoding_chunked(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n");

    assert_int_equal(HTTP_FLAG_READ_CHUNKED, request.flags & HTTP_FLAG_READ_CHUNKED);

    free_request(&request);
}


static void test__http_parse_header__can_parse_content_length(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.1\r\nContent-Length: 10\r\n");

    assert_int_equal(10, request.read_content_length);

    free_request(&request);
}

static void test__http_parse_header__unparseable_content_length_gives_error(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.1\r\nContent-Length: ZZ\r\n");

    assert_int_equal(HTTP_STATUS_BAD_REQUEST, request.error);

    free_request(&request);
}


static void test__http_parse_header__missing_newline_in_header_gives_error(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.1\r\nHost: www.example.com\rX");

    assert_int_equal(HTTP_STATUS_BAD_REQUEST, request.error);
    free_request(&request);
}


static void test__http_parse_header__client_can_read_response(void **state)
{
    struct http_request request;
    create_client_request(&request);

    parse_header_helper(&request, "HTTP/1.1 200 OK\r\n");

    assert_int_equal(200, request.status);

    free_request(&request);
}

static void test__http_parse_header__client_can_read_response_with_unknown_http_method(void **state)
{
    struct http_request request;
    create_client_request(&request);

    parse_header_helper(&request, "HTTP/X.X 200 OK\r\n");

    assert_int_equal(200, request.status);

    free_request(&request);
}

static void test__http_parse_header__client_can_read_response_with_unparseable_status(void **state)
{
    struct http_request request;
    create_client_request(&request);

    parse_header_helper(&request, "HTTP/1.1 XXX OK\r\n");

    assert_int_equal(0, request.status);

    free_request(&request);
}

static void test__http_parse_header__returns_error_when_in_an_unkown_state(void **state)
{
    struct http_request request;
    create_server_request(&request);
    request.state = 0xFF & ~HTTP_STATE_READ_NL;

    parse_header_helper(&request, "X");

    assert_int_equal(HTTP_STATUS_BAD_REQUEST, request.error);
    free_request(&request);
}


static void test__http_urldecode__returns_the_length_of_the_decoded_string(void **state)
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


static void test__http_urldecode__copies_up_to_given_number_of_characters(void **state)
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


static void test__http_get_query_arg__can_find_args(void **states)
{
    char buf[] = "a=1&bcd=123";
    struct http_request request = {
        .query = buf,
    };

    const char *a1 = http_get_query_arg(&request, "a");
    const char *a2 = http_get_query_arg(&request, "bcd");

    assert_string_equal("1", a1);
    assert_string_equal("123", a2);

    free(request.query_list);
}

static void test__http_get_query_arg__returns_null_when_there_are_no_query(void **states)
{
    struct http_request request = {
        .query = 0,
    };

    const char *a = http_get_query_arg(&request, "a");

    assert_null(a);
}

static void test__http_get_query_arg__returns_null_when_arg_not_found(void **states)
{
    char buf[] = "a=1&b=2";
    struct http_request request = {
        .query = buf,
    };

    const char *a = http_get_query_arg(&request, "c");

    assert_null(a);

    free(request.query_list);
}

static void test__http_get_query_arg__url_decode_arg(void **states)
{
    char buf[] = "a=1&bcd=1%203";
    struct http_request request = {
        .query = buf,
    };

    const char *a = http_get_query_arg(&request, "bcd");

    assert_string_equal("1 3", a);

    free(request.query_list);
}

static void test__http_get_query_arg__can_handle_missing_value(void **states)
{
    char buf[] = "a&bcd";
    struct http_request request = {
        .query = buf,
    };

    const char *a1 = http_get_query_arg(&request, "a");
    const char *a2 = http_get_query_arg(&request, "bcd");

    assert_null(a1);
    assert_null(a2);

    free(request.query_list);
}

// Main ////////////////////////////////////////////////////////////////////////

const struct CMUnitTest tests_for_http_parse_header[] = {
    cmocka_unit_test(test__http_parse_header__can_parse_get_request_without_query),
    cmocka_unit_test(test__http_parse_header__can_parse_get_request_with_query),
    cmocka_unit_test(test__http_parse_header__can_parse_post_request),
    cmocka_unit_test(test__http_parse_header__can_parse_delete_request),
    cmocka_unit_test(test__http_parse_header__unsupported_method_gives_error),
    cmocka_unit_test(test__http_parse_header__http_version_10_gives_error),
    cmocka_unit_test(test__http_parse_header__unknown_http_version_gives_error),
    cmocka_unit_test(test__http_parse_header__missing_newline_gives_error),
    cmocka_unit_test(test__http_parse_header__can_parse_host_header_if_server),
    cmocka_unit_test(test__http_parse_header__does_not_set_host_if_client),
    cmocka_unit_test(test__http_parse_header__can_parse_accept_encoding_gzip),
    cmocka_unit_test(test__http_parse_header__can_parse_accept_encoding_no_gzip),
    cmocka_unit_test(test__http_parse_header__does_not_set_accept_encoding_if_client),
    cmocka_unit_test(test__http_parse_header__can_parse_transfer_encoding_chunked),
    cmocka_unit_test(test__http_parse_header__can_parse_content_length),
    cmocka_unit_test(test__http_parse_header__unparseable_content_length_gives_error),
    cmocka_unit_test(test__http_parse_header__missing_newline_in_header_gives_error),
    cmocka_unit_test(test__http_parse_header__client_can_read_response),
    cmocka_unit_test(test__http_parse_header__client_can_read_response_with_unknown_http_method),
    cmocka_unit_test(test__http_parse_header__client_can_read_response_with_unparseable_status),
    cmocka_unit_test(test__http_parse_header__returns_error_when_in_an_unkown_state),
};

const struct CMUnitTest tests_for_http_urldecode[] = {
    cmocka_unit_test(test__http_urldecode__returns_the_length_of_the_decoded_string),
    cmocka_unit_test(test__http_urldecode__copies_up_to_given_number_of_characters),
};

const struct CMUnitTest tests_for_http_get_query_arg[] = {
    cmocka_unit_test(test__http_get_query_arg__can_find_args),
    cmocka_unit_test(test__http_get_query_arg__returns_null_when_there_are_no_query),
    cmocka_unit_test(test__http_get_query_arg__returns_null_when_arg_not_found),
    cmocka_unit_test(test__http_get_query_arg__url_decode_arg),
    cmocka_unit_test(test__http_get_query_arg__can_handle_missing_value),
};

int main(void)
{
    int fails = 0;
    fails += cmocka_run_group_tests(tests_for_http_parse_header, NULL, NULL);
    fails += cmocka_run_group_tests(tests_for_http_urldecode, NULL, NULL);
    fails += cmocka_run_group_tests(tests_for_http_get_query_arg, NULL, NULL);
    return fails;
}
