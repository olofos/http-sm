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


void test__http_urldecode__returns_the_length_of_the_decoded_string(void)
{
    TEST_ASSERT_EQUAL(0, http_urldecode(0, "", 0));
    TEST_ASSERT_EQUAL(4, http_urldecode(0, "ABCD", 0));
    TEST_ASSERT_EQUAL(1, http_urldecode(0, "%20", 0));
    TEST_ASSERT_EQUAL(2, http_urldecode(0, "X%20", 0));
    TEST_ASSERT_EQUAL(2, http_urldecode(0, "%20X", 0));
    TEST_ASSERT_EQUAL(3, http_urldecode(0, "X%20X", 0));
    TEST_ASSERT_EQUAL(3, http_urldecode(0, "%20%20%20", 0));
}

void test__http_urldecode__copies_up_to_given_number_of_characters(void)
{
    char buf[4];
    int ret;

    ret = http_urldecode(buf, "ABC", sizeof(buf));
    TEST_ASSERT_EQUAL(3, ret);
    TEST_ASSERT_EQUAL_STRING("ABC", buf);

    ret = http_urldecode(buf, "%20", sizeof(buf));
    TEST_ASSERT_EQUAL(1, ret);
    TEST_ASSERT_EQUAL_STRING(" ", buf);

    ret = http_urldecode(buf, "X%20", sizeof(buf));
    TEST_ASSERT_EQUAL(2, ret);
    TEST_ASSERT_EQUAL_STRING("X ", buf);

    ret = http_urldecode(buf, "%20X", sizeof(buf));
    TEST_ASSERT_EQUAL(2, ret);
    TEST_ASSERT_EQUAL_STRING(" X", buf);

    memset(buf, 0, sizeof(buf));
    ret = http_urldecode(buf, "ABC", 2);
    TEST_ASSERT_EQUAL(2, ret);
    TEST_ASSERT_EQUAL_STRING("AB", buf);

    memset(buf, 0, sizeof(buf));
    ret = http_urldecode(buf, "X%20Y", 2);
    TEST_ASSERT_EQUAL(2, ret);
    TEST_ASSERT_EQUAL_STRING("X ", buf);

    memset(buf, 0, sizeof(buf));
    ret = http_urldecode(buf, "XY%20", 2);
    TEST_ASSERT_EQUAL(2, ret);
    TEST_ASSERT_EQUAL_STRING("XY", buf);

    memset(buf, 0, sizeof(buf));
    ret = http_urldecode(buf, "%20%20%20", 2);
    TEST_ASSERT_EQUAL(2, ret);
    TEST_ASSERT_EQUAL_STRING("  ", buf);
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

    RUN_TEST(test__http_urldecode__returns_the_length_of_the_decoded_string);
    RUN_TEST(test__http_urldecode__copies_up_to_given_number_of_characters);

    return UNITY_END();
}
