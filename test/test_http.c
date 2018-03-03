#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "unity.h"

#include "http.h"

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

void test__http_parse_header__can_parse_get_request_without_query(void)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_REQ_METHOD);

    parse_header_helper(&request, "GET / HTTP/1.1\r\n");

    TEST_ASSERT_EQUAL(HTTP_METHOD_GET, request.method);
    TEST_ASSERT_NOT_NULL(request.path);
    TEST_ASSERT_NULL(request.query);
    TEST_ASSERT_EQUAL_STRING("/", request.path);

    TEST_ASSERT_EQUAL(HTTP_STATE_READ_HEADER, request.state);

    free_request(&request);
}

void test__http_parse_header__can_parse_get_request_with_query(void)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_REQ_METHOD);

    parse_header_helper(&request, "GET /test?a=1&b=2 HTTP/1.1\r\n");

    TEST_ASSERT_EQUAL(HTTP_METHOD_GET, request.method);
    TEST_ASSERT_NOT_NULL(request.path);
    TEST_ASSERT_NOT_NULL(request.query);
    TEST_ASSERT_EQUAL_STRING("/test", request.path);
    TEST_ASSERT_EQUAL_STRING("a=1&b=2", request.query);

    TEST_ASSERT_EQUAL(HTTP_STATE_READ_HEADER, request.state);

    free_request(&request);
}

void test__http_parse_header__can_parse_post_request(void)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_REQ_METHOD);

    parse_header_helper(&request, "POST / HTTP/1.1\r\n");

    TEST_ASSERT_EQUAL(HTTP_METHOD_POST, request.method);
    TEST_ASSERT_NOT_NULL(request.path);
    TEST_ASSERT_NULL(request.query);
    TEST_ASSERT_EQUAL_STRING("/", request.path);

    TEST_ASSERT_EQUAL(HTTP_STATE_READ_HEADER, request.state);

    free_request(&request);
}

void test__http_parse_header__unsupported_method_gives_error(void)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_REQ_METHOD);

    parse_header_helper(&request, "DELETE / HTTP/1.1\r\n");

    TEST_ASSERT_EQUAL(HTTP_METHOD_UNSUPPORTED, request.method);
    TEST_ASSERT_EQUAL(HTTP_STATE_ERROR, request.state);
    TEST_ASSERT_EQUAL(HTTP_STATUS_METHOD_NOT_ALLOWED, request.error);

    free_request(&request);
}

void test__http_parse_header__http_version_10_gives_error(void)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_REQ_METHOD);

    parse_header_helper(&request, "GET / HTTP/1.0\r\n");

    TEST_ASSERT_EQUAL(HTTP_STATE_ERROR, request.state);
    TEST_ASSERT_EQUAL(HTTP_STATUS_VERSION_NOT_SUPPORTED, request.error);
    free_request(&request);
}

void test__http_parse_header__unknown_http_version_gives_error(void)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_REQ_METHOD);

    parse_header_helper(&request, "GET / XX\r\n");

    TEST_ASSERT_EQUAL(HTTP_STATE_ERROR, request.state);
    TEST_ASSERT_EQUAL(HTTP_STATUS_BAD_REQUEST, request.error);
    free_request(&request);
}

void test__http_parse_header__missing_newline_gives_error(void)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_REQ_METHOD);

    parse_header_helper(&request, "GET / HTTP/1.1\rX");

    TEST_ASSERT_EQUAL(HTTP_STATE_ERROR, request.state);
    TEST_ASSERT_EQUAL(HTTP_STATUS_BAD_REQUEST, request.error);
    free_request(&request);
}

void test__http_parse_header__can_parse_host_header(void)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_HEADER);

    parse_header_helper(&request, "Host: www.example.com\r\n");

    TEST_ASSERT_NOT_NULL(request.host);
    TEST_ASSERT_EQUAL_STRING("www.example.com", request.host);

    free_request(&request);
}

void test__http_parse_header__can_parse_accept_encoding_gzip(void)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_HEADER);

    parse_header_helper(&request, "Accept-Encoding: gzip, deflate\r\n");

    TEST_ASSERT_BITS_HIGH(HTTP_FLAG_ACCEPT_GZIP, request.flags);
    free_request(&request);
}

void test__http_parse_header__can_parse_accept_encoding_no_gzip(void)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_HEADER);

    parse_header_helper(&request, "Accept-Encoding: deflate\r\n");

    TEST_ASSERT_BITS_LOW(HTTP_FLAG_ACCEPT_GZIP, request.flags);
    free_request(&request);
}

void test__http_parse_header__can_parse_transfer_encoding_chunked(void)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_HEADER);

    parse_header_helper(&request, "Transfer-Encoding: chunked\r\n");

    TEST_ASSERT_BITS_HIGH(HTTP_FLAG_CHUNKED, request.flags);

    free_request(&request);
}


void test__http_parse_header__can_parse_content_length(void)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_HEADER);

    parse_header_helper(&request, "Content-Length: 10\r\n");

    TEST_ASSERT_EQUAL(10, request.content_length);

    free_request(&request);
}


void test__http_parse_header__missing_newline_in_header_gives_error(void)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_HEADER);

    parse_header_helper(&request, "Host: www.example.com\rX");

    TEST_ASSERT_EQUAL(HTTP_STATE_ERROR, request.state);
    TEST_ASSERT_EQUAL(HTTP_STATUS_BAD_REQUEST, request.error);
    free_request(&request);
}


void test__http_parse_header__can_read_response(void)
{
    struct http_request request;
    create_request(&request, HTTP_STATE_READ_RESP_VERSION);

    parse_header_helper(&request, "HTTP/1.1 200 OK\r\n");

    TEST_ASSERT_EQUAL(HTTP_STATE_READ_HEADER, request.state);
    TEST_ASSERT_EQUAL(200, request.status);

    free_request(&request);
}



int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test__http_parse_header__can_parse_get_request_without_query);
    RUN_TEST(test__http_parse_header__can_parse_get_request_with_query);

    RUN_TEST(test__http_parse_header__can_parse_post_request);
    RUN_TEST(test__http_parse_header__unsupported_method_gives_error);
    RUN_TEST(test__http_parse_header__http_version_10_gives_error);
    RUN_TEST(test__http_parse_header__unknown_http_version_gives_error);
    RUN_TEST(test__http_parse_header__missing_newline_gives_error);

    RUN_TEST(test__http_parse_header__can_parse_host_header);
    RUN_TEST(test__http_parse_header__can_parse_accept_encoding_gzip);
    RUN_TEST(test__http_parse_header__can_parse_accept_encoding_no_gzip);
    RUN_TEST(test__http_parse_header__can_parse_transfer_encoding_chunked);
    RUN_TEST(test__http_parse_header__can_parse_content_length);

    RUN_TEST(test__http_parse_header__missing_newline_in_header_gives_error);

    RUN_TEST(test__http_parse_header__can_read_response);

    return UNITY_END();
}
