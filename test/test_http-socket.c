#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <stdarg.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <cmocka.h>

#include "http.h"
#include "http-private.h"

// Mocks ///////////////////////////////////////////////////////////////////////

void LOG(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    printf("\n");
}

struct addrinfo *getaddrinfo_res;

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints,
                struct addrinfo **res)
{
    check_expected(node);
    check_expected(service);
    check_expected(hints);

    *res = getaddrinfo_res;

    return mock();
}

void freeaddrinfo(struct addrinfo *res)
{
    check_expected(res);
}

int socket(int domain, int type, int protocol)
{
    check_expected(domain);
    check_expected(type);
    check_expected(protocol);
    return mock();
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    check_expected(sockfd);
    check_expected(addr);
    check_expected(addrlen);
    return mock();
}

int close(int fd)
{
    check_expected(fd);
    return mock();
}

// Tests ///////////////////////////////////////////////////////////////////////


static void test__http_open_request_socket__can_open_a_socket(void **states)
{
    struct http_request request = {
        .host = "www.example.com",
        .path = "/",
        .port = 80,
        .method = HTTP_METHOD_GET,
    };

    struct sockaddr_in sa = {
        .sin_family = AF_INET,
        .sin_port = request.port,
        .sin_addr = {
            .s_addr = (4 << 24) | (1 << 16) | (168 << 8) | (192 << 0) ,
        },
    };

    struct addrinfo res = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_addr = (struct sockaddr *)&sa,
        .ai_addrlen = sizeof(sa),
    };

    getaddrinfo_res = &res;

    expect_string(getaddrinfo, node, "www.example.com");
    expect_string(getaddrinfo, service, "80");

    const struct addrinfo expected_hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    };

    expect_memory(getaddrinfo, hints, &expected_hints, sizeof(expected_hints));
    will_return(getaddrinfo, 0);

    expect_value(freeaddrinfo, res, getaddrinfo_res);

    expect_value(socket, domain, res.ai_family);
    expect_value(socket, type, res.ai_socktype);
    expect_value(socket, protocol, 0);
    will_return(socket, 3);

    expect_value(connect, sockfd, 3);
    expect_value(connect, addr, res.ai_addr);
    expect_value(connect, addrlen, res.ai_addrlen);
    will_return(connect, 0);

    int fd = http_open_request_socket(&request);
    assert_int_not_equal(-1, fd);
    assert_int_equal(3, request.fd);
}


static void test__http_open_request_socket__can_open_a_socket_with_non_default_port(void **states)
{
    struct http_request request = {
        .host = "www.example.com",
        .path = "/",
        .port = 8080,
        .method = HTTP_METHOD_GET,
    };

    struct sockaddr_in sa = {
        .sin_family = AF_INET,
        .sin_port = request.port,
        .sin_addr = {
            .s_addr = (4 << 24) | (1 << 16) | (168 << 8) | (192 << 0) ,
        },
    };

    struct addrinfo res = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_addr = (struct sockaddr *)&sa,
        .ai_addrlen = sizeof(sa),
    };

    getaddrinfo_res = &res;

    expect_any(getaddrinfo, node);
    expect_string(getaddrinfo, service, "8080");

    expect_any(getaddrinfo, hints);
    will_return(getaddrinfo, 0);

    expect_value(freeaddrinfo, res, getaddrinfo_res);

    expect_any(socket, domain);
    expect_any(socket, type);
    expect_any(socket, protocol);
    will_return(socket, 3);

    expect_any(connect, sockfd);
    expect_any(connect, addr);
    expect_any(connect, addrlen);
    will_return(connect, 0);

    int fd = http_open_request_socket(&request);
    assert_int_not_equal(-1, fd);
}


static void test__http_open_request_socket__default_port_is_80(void **states)
{
    struct http_request request = {
        .host = "www.example.com",
        .path = "/",
        .method = HTTP_METHOD_GET,
    };

    struct sockaddr_in sa = {
        .sin_family = AF_INET,
        .sin_port = 80,
        .sin_addr = {
            .s_addr = (4 << 24) | (1 << 16) | (168 << 8) | (192 << 0) ,
        },
    };

    struct addrinfo res = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_addr = (struct sockaddr *)&sa,
        .ai_addrlen = sizeof(sa),
    };

    getaddrinfo_res = &res;

    expect_any(getaddrinfo, node);
    expect_string(getaddrinfo, service, "80");

    expect_any(getaddrinfo, hints);
    will_return(getaddrinfo, 0);

    expect_value(freeaddrinfo, res, getaddrinfo_res);

    expect_any(socket, domain);
    expect_any(socket, type);
    expect_any(socket, protocol);
    will_return(socket, 3);

    expect_any(connect, sockfd);
    expect_any(connect, addr);
    expect_any(connect, addrlen);
    will_return(connect, 0);

    int fd = http_open_request_socket(&request);
    assert_int_not_equal(-1, fd);
}


static void test__http_open_request_socket__returns_minus_one_if_getaddrinfo_returns_error(void **states)
{
    struct http_request request = {
        .host = "www.example.com",
        .path = "/",
        .port = 80,
        .method = HTTP_METHOD_GET,
    };

    struct sockaddr_in sa = {
        .sin_family = AF_INET,
        .sin_port = request.port,
        .sin_addr = {
            .s_addr = (4 << 24) | (1 << 16) | (168 << 8) | (192 << 0) ,
        },
    };

    struct addrinfo res = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_addr = (struct sockaddr *)&sa,
        .ai_addrlen = sizeof(sa),
    };

    getaddrinfo_res = &res;

    expect_any(getaddrinfo, node);
    expect_any(getaddrinfo, service);
    expect_any(getaddrinfo, hints);
    will_return(getaddrinfo, -1);

    expect_value(freeaddrinfo, res, getaddrinfo_res);

    int fd = http_open_request_socket(&request);
    assert_int_equal(-1, fd);
}

static void test__http_open_request_socket__returns_minus_one_if_getaddrinfo_gives_null_res(void **states)
{
    struct http_request request = {
        .host = "www.example.com",
        .path = "/",
        .port = 80,
        .method = HTTP_METHOD_GET,
    };

    getaddrinfo_res = 0;

    expect_any(getaddrinfo, node);
    expect_any(getaddrinfo, service);
    expect_any(getaddrinfo, hints);
    will_return(getaddrinfo, 0);

    int fd = http_open_request_socket(&request);
    assert_int_equal(-1, fd);
}

static void test__http_open_request_socket__returns_minus_one_if_socket_fails(void **states)
{
    struct http_request request = {
        .host = "www.example.com",
        .path = "/",
        .port = 80,
        .method = HTTP_METHOD_GET,
    };

    struct sockaddr_in sa = {
        .sin_family = AF_INET,
        .sin_port = request.port,
        .sin_addr = {
            .s_addr = (4 << 24) | (1 << 16) | (168 << 8) | (192 << 0) ,
        },
    };

    struct addrinfo res = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_addr = (struct sockaddr *)&sa,
        .ai_addrlen = sizeof(sa),
    };

    getaddrinfo_res = &res;

    expect_any(getaddrinfo, node);
    expect_any(getaddrinfo, service);
    expect_any(getaddrinfo, hints);
    will_return(getaddrinfo, 0);

    expect_any(socket, domain);
    expect_any(socket, type);
    expect_any(socket, protocol);
    will_return(socket, -1);

    expect_value(freeaddrinfo, res, getaddrinfo_res);

    int fd = http_open_request_socket(&request);
    assert_int_equal(-1, fd);
}

static void test__http_open_request_socket__returns_minus_one_if_connect_fails(void **states)
{
    struct http_request request = {
        .host = "www.example.com",
        .path = "/",
        .port = 80,
        .method = HTTP_METHOD_GET,
    };

    struct sockaddr_in sa = {
        .sin_family = AF_INET,
        .sin_port = request.port,
        .sin_addr = {
            .s_addr = (4 << 24) | (1 << 16) | (168 << 8) | (192 << 0) ,
        },
    };

    struct addrinfo res = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_addr = (struct sockaddr *)&sa,
        .ai_addrlen = sizeof(sa),
    };

    getaddrinfo_res = &res;

    expect_any(getaddrinfo, node);
    expect_any(getaddrinfo, service);
    expect_any(getaddrinfo, hints);
    will_return(getaddrinfo, 0);

    expect_any(socket, domain);
    expect_any(socket, type);
    expect_any(socket, protocol);
    will_return(socket, 3);

    expect_any(connect, sockfd);
    expect_any(connect, addr);
    expect_any(connect, addrlen);
    will_return(connect, -1);

    expect_value(close, fd, 3);
    will_return(close, 0);

    expect_value(freeaddrinfo, res, getaddrinfo_res);

    int fd = http_open_request_socket(&request);
    assert_int_equal(-1, fd);
}

static void test__http_close__closes_the_socket(void **states)
{
    struct http_request request = {
        .fd = 3,
    };

    expect_value(close, fd, 3);
    will_return(close, 0);

    int ret = http_close(&request);
    assert_int_equal(0, ret);
}

// Main ////////////////////////////////////////////////////////////////////////

const struct CMUnitTest tests_for_http_open_request_socket[] = {
    cmocka_unit_test(test__http_open_request_socket__can_open_a_socket),
    cmocka_unit_test(test__http_open_request_socket__can_open_a_socket_with_non_default_port),
    cmocka_unit_test(test__http_open_request_socket__default_port_is_80),
    cmocka_unit_test(test__http_open_request_socket__returns_minus_one_if_getaddrinfo_returns_error),
    cmocka_unit_test(test__http_open_request_socket__returns_minus_one_if_getaddrinfo_gives_null_res),
    cmocka_unit_test(test__http_open_request_socket__returns_minus_one_if_socket_fails),
    cmocka_unit_test(test__http_open_request_socket__returns_minus_one_if_connect_fails),

    cmocka_unit_test(test__http_close__closes_the_socket),
};

int main(void)
{
    return cmocka_run_group_tests(tests_for_http_open_request_socket, NULL, NULL);
}
