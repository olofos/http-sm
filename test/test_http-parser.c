#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <stdarg.h>

#include <cmocka.h>

#include "http-sm/http.h"
#include "http-private.h"

#include "test-util.h"

// Mocks ///////////////////////////////////////////////////////////////////////

static int enable_malloc_mock = 0;

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

static void init_request(struct http_request *request)
{
    const int line_length = 64;

    request->method = HTTP_METHOD_UNKNOWN;
    request->line = malloc(line_length);
    request->line_length = line_length;
    request->line_index = 0;
    request->path = 0;
    request->query = 0;
    request->host = 0;
    request->flags = 0;
    request->status = 0;
    request->error = 0;
    request->content_type = 0;
    request->read_content_length = -1;
    request->write_content_length = -1;
    request->websocket_key = 0;
    request->etag = 0;
}

static void create_server_request(struct http_request *request)
{
    init_request(request);
    request->state  = HTTP_STATE_SERVER_READ_METHOD;
}

static void create_client_request(struct http_request *request)
{
    init_request(request);
    request->state  = HTTP_STATE_CLIENT_READ_VERSION;
}

static void free_request(struct http_request *request)
{
    free(request->line);
    free(request->path);
    free(request->host);
    free(request->query);
    free(request->content_type);
    free(request->websocket_key);
    free(request->etag);
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

    assert_true(http_is_error(&request));
    assert_int_equal(HTTP_METHOD_UNSUPPORTED, request.method);
    assert_int_equal(HTTP_STATUS_METHOD_NOT_ALLOWED, request.error);

    free_request(&request);
}

static void test__http_parse_header__http_version_10_gives_error(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.0\r\n");

    assert_true(http_is_error(&request));
    assert_int_equal(HTTP_STATUS_VERSION_NOT_SUPPORTED, request.error);
    free_request(&request);
}

static void test__http_parse_header__unknown_http_version_gives_error(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / XX\r\n");

    assert_true(http_is_error(&request));
    assert_int_equal(HTTP_STATUS_BAD_REQUEST, request.error);
    free_request(&request);
}

static void test__http_parse_header__missing_newline_gives_error(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.1\rX");

    assert_true(http_is_error(&request));
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

static void test__http_parse_header__can_parse_upgrade_websocket(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.1\r\nUpgrade: websocket\r\n");

    assert_int_equal(HTTP_FLAG_WEBSOCKET, request.flags & HTTP_FLAG_WEBSOCKET);

    free_request(&request);
}

static void test__http_parse_header__can_parse_sec_websocket_key(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.1\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n");

    assert_non_null(request.websocket_key);
    assert_string_equal("dGhlIHNhbXBsZSBub25jZQ==", request.websocket_key);

    free_request(&request);
}

static void test__http_parse_header__can_parse_if_none_match(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.1\r\nIf-None-Match: \"33a64df551425fcc55e4d42a148795d9f25f89d4\"\r\n");

    assert_non_null(request.etag);
    assert_string_equal("33a64df551425fcc55e4d42a148795d9f25f89d4", request.etag);

    free_request(&request);
}

static void test__http_parse_header__unparseable_content_length_gives_error(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.1\r\nContent-Length: ZZ\r\n");

    assert_true(http_is_error(&request));
    assert_int_equal(HTTP_STATUS_BAD_REQUEST, request.error);

    free_request(&request);
}

static void test__http_parse_header__can_parse_content_type_if_client(void **state)
{
    struct http_request request;
    create_client_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.1\r\nContent-Type: text/plain\r\n");

    assert_non_null(request.content_type);
    assert_string_equal("text/plain", request.content_type);
    free_request(&request);
}


static void test__http_parse_header__missing_newline_in_header_gives_error(void **state)
{
    struct http_request request;
    create_server_request(&request);

    parse_header_helper(&request, "GET / HTTP/1.1\r\nHost: www.example.com\rX");

    assert_true(http_is_error(&request));
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

    assert_true(http_is_error(&request));
    assert_int_equal(HTTP_STATUS_INTERNAL_SERVER_ERROR, request.error);
    free_request(&request);
}

static void test__http_parse_header__returns_error_when_path_is_too_long(void **state)
{
    struct http_request request;
    create_server_request(&request);

    const int path_len = 2 * HTTP_LINE_LEN;

    char *path = malloc(path_len + 1);

    path[0] = '/';
    for(int i = 1; i < path_len; i++) {
        path[i] = 'a';
    }
    path[path_len] = 0;

    parse_header_helper(&request, "GET ");
    parse_header_helper(&request, path);

    assert_true(http_is_error(&request));
    assert_int_equal(HTTP_STATUS_URI_TOO_LONG, request.error);

    free(path);
    free_request(&request);
}

static void test__http_parse_header__returns_error_when_query_is_too_long(void **state)
{
    struct http_request request;
    create_server_request(&request);

    const int query_len = 2 * HTTP_LINE_LEN;

    char *query = malloc(query_len + 1);

    query[0] = '/';
    for(int i = 1; i < query_len; i++) {
        query[i] = 'a';
    }
    query[query_len] = 0;

    parse_header_helper(&request, "GET /query?");
    parse_header_helper(&request, query);

    assert_true(http_is_error(&request));
    assert_int_equal(HTTP_STATUS_URI_TOO_LONG, request.error);

    free(query);
    free_request(&request);
}

static void test__http_parse_header__returns_error_when_malloc_fails_when_allocating_path(void **state)
{
    char buf[HTTP_LINE_LEN];
    const char *path = "/path ";

    struct http_request request = {
        .method = HTTP_METHOD_UNKNOWN,
        .state = HTTP_STATE_SERVER_READ_PATH,
        .line = buf,
        .line_length = HTTP_LINE_LEN,
        .line_index = 0,
        .path = (void *)0xBADBAD,
        .query = 0,
        .host = 0,
        .flags = 0,
        .status = 0,
        .error = 0,
        .content_type = 0,
        .read_content_length = -1,
        .write_content_length = -1,
        .websocket_key = 0,
    };

    expect_value(__wrap_malloc, size, strlen(path));
    will_return(__wrap_malloc, NULL);

    parse_header_helper(&request, path);

    assert_null(request.path);
    assert_true(http_is_error(&request));
    assert_int_equal(HTTP_STATUS_INTERNAL_SERVER_ERROR, request.error);
}

static void test__http_parse_header__returns_error_when_malloc_fails_when_allocating_query(void **state)
{
    char buf[HTTP_LINE_LEN];
    const char *query = "a=1 ";

    struct http_request request = {
        .method = HTTP_METHOD_UNKNOWN,
        .state = HTTP_STATE_SERVER_READ_QUERY,
        .line = buf,
        .line_length = HTTP_LINE_LEN,
        .line_index = 0,
        .path = "/path",
        .query = (void*) 0xBADBAD,
        .host = 0,
        .flags = 0,
        .status = 0,
        .error = 0,
        .content_type = 0,
        .read_content_length = -1,
        .write_content_length = -1,
        .websocket_key = 0,
    };

    expect_value(__wrap_malloc, size, strlen(query));
    will_return(__wrap_malloc, NULL);

    parse_header_helper(&request, query);

    assert_null(request.query);
    assert_true(http_is_error(&request));
    assert_int_equal(HTTP_STATUS_INTERNAL_SERVER_ERROR, request.error);
}

static void test__http_parse_header__returns_error_when_malloc_fails_when_allocating_host(void **state)
{
    char buf[HTTP_LINE_LEN];
    const char *header = "Host: test\r\n";

    struct http_request request = {
        .method = HTTP_METHOD_UNKNOWN,
        .state = HTTP_STATE_SERVER_READ_HEADER,
        .line = buf,
        .line_length = HTTP_LINE_LEN,
        .line_index = 0,
        .path = "/path",
        .query = 0,
        .host = (void*) 0xBADBAD,
        .flags = 0,
        .status = 0,
        .error = 0,
        .content_type = 0,
        .read_content_length = -1,
        .write_content_length = -1,
        .websocket_key = 0,
    };

    expect_any(__wrap_malloc, size);
    will_return(__wrap_malloc, NULL);

    parse_header_helper(&request, header);

    assert_null(request.host);
    assert_true(http_is_error(&request));
    assert_int_equal(HTTP_STATUS_INTERNAL_SERVER_ERROR, request.error);
}

static void test__http_parse_header__returns_error_when_malloc_fails_when_allocating_websocket_key(void **state)
{
    char buf[HTTP_LINE_LEN];
    const char *header = "Sec-WebSocket-Key: test\r\n";

    struct http_request request = {
        .method = HTTP_METHOD_UNKNOWN,
        .state = HTTP_STATE_SERVER_READ_HEADER,
        .line = buf,
        .line_length = HTTP_LINE_LEN,
        .line_index = 0,
        .path = "/path",
        .query = 0,
        .host = 0,
        .flags = 0,
        .status = 0,
        .error = 0,
        .content_type = 0,
        .read_content_length = -1,
        .write_content_length = -1,
        .websocket_key = (void*) 0xBADBAD,
    };

    expect_any(__wrap_malloc, size);
    will_return(__wrap_malloc, NULL);

    parse_header_helper(&request, header);

    assert_null(request.websocket_key);
    assert_true(http_is_error(&request));
    assert_int_equal(HTTP_STATUS_INTERNAL_SERVER_ERROR, request.error);
}

static void test__http_parse_header__returns_error_when_malloc_fails_when_allocating_content_type(void **state)
{
    char buf[HTTP_LINE_LEN];
    const char *header = "Content-Type: test\r\n";

    struct http_request request = {
        .method = HTTP_METHOD_UNKNOWN,
        .state = HTTP_STATE_CLIENT_READ_HEADER,
        .line = buf,
        .line_length = HTTP_LINE_LEN,
        .line_index = 0,
        .flags = 0,
        .status = 0,
        .error = 0,
        .content_type = (void*) 0xBADABAD,
        .read_content_length = -1,
        .write_content_length = -1,
    };

    expect_any(__wrap_malloc, size);
    will_return(__wrap_malloc, NULL);

    parse_header_helper(&request, header);

    assert_null(request.content_type);
    assert_true(http_is_error(&request));
    assert_int_equal(HTTP_STATUS_INTERNAL_SERVER_ERROR, request.error);
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


static void test__http_urlencode__returns_the_length_of_the_encoded_string(void **state)
{
    assert_int_equal(0, http_urlencode(0, "", 0));
    assert_int_equal(4, http_urlencode(0, "ABCD", 0));
    assert_int_equal(4, http_urlencode(0, "-_.~", 0));
    assert_int_equal(3, http_urlencode(0, " ", 0));
    assert_int_equal(3, http_urlencode(0, "+", 0));
    assert_int_equal(4, http_urlencode(0, "X ", 0));
    assert_int_equal(4, http_urlencode(0, " X", 0));
    assert_int_equal(5, http_urlencode(0, "X X", 0));
    assert_int_equal(9, http_urlencode(0, "   ", 0));
    assert_int_equal(3, http_urlencode(0, "%", 0));
    assert_int_equal(6, http_urlencode(0, "% ", 0));
}

static void test__http_urlencode__copies_up_to_given_number_of_characters(void **state)
{
    char buf[32];
    int ret;

    ret = http_urlencode(buf, "ABC", sizeof(buf));
    assert_int_equal(3, ret);
    assert_string_equal("ABC", buf);

    ret = http_urlencode(buf, " ", sizeof(buf));
    assert_int_equal(3, ret);
    assert_string_equal("%20", buf);

    ret = http_urlencode(buf, " !", sizeof(buf));
    assert_int_equal(6, ret);
    assert_string_equal("%20%21", buf);

    ret = http_urlencode(buf, "@!", 4);
    assert_int_equal(3, ret);
    assert_string_equal("%40", buf);
}

static void test__http_urlencode__does_not_copy_too_many_characters(void **state)
{
    char buf[5];
    buf[3] = 0x0E;
    buf[4] = 0x0F;

    int ret;

    ret = http_urlencode(buf, "ABCDEF", 3);
    assert_int_equal(ret, 3);
    assert_int_equal(buf[0], 'A');
    assert_int_equal(buf[1], 'B');
    assert_int_equal(buf[2], 'C');
    assert_int_equal(buf[3], 0x0E);

    ret = http_urlencode(buf, "A CDEF", 3);
    assert_int_equal(ret, 1);
    assert_int_equal(buf[0], 'A');
    assert_int_equal(buf[1], 0);
    assert_int_equal(buf[2], 'C');
    assert_int_equal(buf[3], 0x0E);

    ret = http_urlencode(buf, " BCDEF", 3);
    assert_int_equal(ret, 3);
    assert_int_equal(buf[0], '%');
    assert_int_equal(buf[1], '2');
    assert_int_equal(buf[2], '0');
    assert_int_equal(buf[3], 0x0E);

    ret = http_urlencode(buf, " BCDEF", 4);
    assert_int_equal(ret, 4);
    assert_int_equal(buf[0], '%');
    assert_int_equal(buf[1], '2');
    assert_int_equal(buf[2], '0');
    assert_int_equal(buf[3], 'B');
    assert_int_equal(buf[4], 0x0F);

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

static void test__http_get_query_arg__returns_null_when_malloc_fails(void **states)
{
    char buf[] = "a=1&b=2";
    struct http_request request = {
        .query = buf,
    };

    expect_any(__wrap_malloc, size);
    will_return(__wrap_malloc, NULL);

    const char *a = http_get_query_arg(&request, "c");

    assert_null(a);
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
    cmocka_unit_test(test__http_parse_header__can_parse_content_type_if_client),
    cmocka_unit_test(test__http_parse_header__can_parse_content_length),
    cmocka_unit_test(test__http_parse_header__can_parse_upgrade_websocket),
    cmocka_unit_test(test__http_parse_header__can_parse_sec_websocket_key),
    cmocka_unit_test(test__http_parse_header__can_parse_if_none_match),
    cmocka_unit_test(test__http_parse_header__unparseable_content_length_gives_error),
    cmocka_unit_test(test__http_parse_header__missing_newline_in_header_gives_error),
    cmocka_unit_test(test__http_parse_header__client_can_read_response),
    cmocka_unit_test(test__http_parse_header__client_can_read_response_with_unknown_http_method),
    cmocka_unit_test(test__http_parse_header__client_can_read_response_with_unparseable_status),
    cmocka_unit_test(test__http_parse_header__returns_error_when_in_an_unkown_state),
    cmocka_unit_test(test__http_parse_header__returns_error_when_path_is_too_long),
    cmocka_unit_test(test__http_parse_header__returns_error_when_query_is_too_long),
};

const struct CMUnitTest tests_for_http_parse_header_mock_malloc[] = {
    cmocka_unit_test(test__http_parse_header__returns_error_when_malloc_fails_when_allocating_path),
    cmocka_unit_test(test__http_parse_header__returns_error_when_malloc_fails_when_allocating_query),
    cmocka_unit_test(test__http_parse_header__returns_error_when_malloc_fails_when_allocating_host),
    cmocka_unit_test(test__http_parse_header__returns_error_when_malloc_fails_when_allocating_websocket_key),
    cmocka_unit_test(test__http_parse_header__returns_error_when_malloc_fails_when_allocating_content_type),
};

const struct CMUnitTest tests_for_http_urldecode[] = {
    cmocka_unit_test(test__http_urldecode__returns_the_length_of_the_decoded_string),
    cmocka_unit_test(test__http_urldecode__copies_up_to_given_number_of_characters),
};

const struct CMUnitTest tests_for_http_urlencode[] = {
    cmocka_unit_test(test__http_urlencode__returns_the_length_of_the_encoded_string),
    cmocka_unit_test(test__http_urlencode__copies_up_to_given_number_of_characters),
    cmocka_unit_test(test__http_urlencode__does_not_copy_too_many_characters),
};

const struct CMUnitTest tests_for_http_get_query_arg[] = {
    cmocka_unit_test(test__http_get_query_arg__can_find_args),
    cmocka_unit_test(test__http_get_query_arg__returns_null_when_there_are_no_query),
    cmocka_unit_test(test__http_get_query_arg__returns_null_when_arg_not_found),
    cmocka_unit_test(test__http_get_query_arg__url_decode_arg),
    cmocka_unit_test(test__http_get_query_arg__can_handle_missing_value),
};

const struct CMUnitTest tests_for_http_get_query_arg_mock_malloc[] = {
    cmocka_unit_test(test__http_get_query_arg__returns_null_when_malloc_fails),
};

int main(void)
{
    int fails = 0;
    fails += cmocka_run_group_tests(tests_for_http_parse_header, NULL, NULL);
    fails += cmocka_run_group_tests(tests_for_http_parse_header_mock_malloc, gr_setup_malloc_mock, gr_teardown_malloc_mock);
    fails += cmocka_run_group_tests(tests_for_http_urldecode, NULL, NULL);
    fails += cmocka_run_group_tests(tests_for_http_urlencode, NULL, NULL);
    fails += cmocka_run_group_tests(tests_for_http_get_query_arg, NULL, NULL);
    fails += cmocka_run_group_tests(tests_for_http_get_query_arg_mock_malloc, gr_setup_malloc_mock, gr_teardown_malloc_mock);
    return fails;
}
