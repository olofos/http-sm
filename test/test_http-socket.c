#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <stdarg.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <cmocka.h>

#include "http-sm/http.h"
#include "http-private.h"

// Mocks ///////////////////////////////////////////////////////////////////////

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

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    check_expected(sockfd);
    check_expected(addr);
    check_expected(addrlen);

    return mock();
}

int listen(int sockfd, int backlog)
{
    check_expected(sockfd);
    check_expected(backlog);
    return mock();
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    check_expected(sockfd);
    check_expected(addr);
    check_expected(addrlen);

    struct sockaddr_in * addr_in = (struct sockaddr_in *)addr;
    addr_in->sin_family = AF_INET;
    addr_in->sin_port = 1234;
    addr_in->sin_addr.s_addr = (4 << 24) | (1 << 16) | (168 << 8) | (192 << 0);

    return mock();
}

int setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    check_expected(fd);
    check_expected(level);
    check_expected(optname);
    check_expected(optval);
    check_expected(optlen);

    return mock();
}

// Helper Functions ////////////////////////////////////////////////////////////

static void init_server(struct http_server *server)
{
    for(int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
        server->request[i].fd = -1;
    }
    for(int i = 0; i < WEBSOCKET_SERVER_MAX_CONNECTIONS; i++) {
        server->websocket_connection[i].fd = -1;
    }
    server->fd = 3;
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
        .state = HTTP_STATE_SERVER_IDLE,
    };

    expect_value(close, fd, 3);
    will_return(close, 0);

    int ret = http_close(&request);
    assert_int_equal(0, ret);
    assert_int_equal(-1, request.fd);
}

static void test__http_close__closes_the_socket_when_client(void **states)
{
    struct http_request request = {
        .fd = 3,
        .state = HTTP_STATE_CLIENT_IDLE,
    };

    expect_value(close, fd, 3);
    will_return(close, 0);

    int ret = http_close(&request);
    assert_int_equal(0, ret);
    assert_int_equal(-1, request.fd);
}

static void test__http_close__does_not_close_a_closed_socket(void **states)
{
    struct http_request request = {
        .fd = -1,
    };

    int ret = http_close(&request);
    assert_int_equal(-1, ret);
    assert_int_equal(-1, request.fd);
}


static void test__http_open_listen_socket__opens_and_binds_and_listens(void **states)
{
    expect_value(socket, domain, AF_INET);
    expect_value(socket, type, SOCK_STREAM);
    expect_value(socket, protocol, 0);
    will_return(socket, 3);

    const struct sockaddr_in expected_addr = {
        .sin_family = AF_INET,
        .sin_addr = {
            .s_addr = INADDR_ANY,
        },
        .sin_port = htons(8080),
    };

    expect_value(bind, sockfd, 3);
    expect_memory(bind, addr, &expected_addr, sizeof(expected_addr));
    expect_value(bind, addrlen, sizeof(expected_addr));
    will_return(bind, 0);

    expect_value(listen, sockfd, 3);
    expect_value(listen, backlog, HTTP_SERVER_MAX_CONNECTIONS);
    will_return(listen, 0);

    int fd = http_open_listen_socket(8080);
    assert_int_equal(3, fd);
}

static void test__http_open_listen_socket__fails_if_socket_fails(void **states)
{
    expect_any(socket, domain);
    expect_any(socket, type);
    expect_any(socket, protocol);
    will_return(socket, -1);

    int fd = http_open_listen_socket(80);
    assert_int_equal(-1, fd);
}

static void test__http_open_listen_socket__fails_if_bind_fails(void **states)
{
    expect_any(socket, domain);
    expect_any(socket, type);
    expect_any(socket, protocol);
    will_return(socket, 3);

    expect_any(bind, sockfd);
    expect_any(bind, addr);
    expect_any(bind, addrlen);
    will_return(bind, -1);

    expect_value(close, fd, 3);
    will_return(close, 0);

    int fd = http_open_listen_socket(80);
    assert_int_equal(-1, fd);
}

static void test__http_open_listen_socket__fails_if_listen_fails(void **states)
{
    expect_any(socket, domain);
    expect_any(socket, type);
    expect_any(socket, protocol);
    will_return(socket, 3);

    expect_any(bind, sockfd);
    expect_any(bind, addr);
    expect_any(bind, addrlen);
    will_return(bind, 0);

    expect_any(listen, sockfd);
    expect_any(listen, backlog);
    will_return(listen, -1);

    expect_value(close, fd, 3);
    will_return(close, 0);

    int fd = http_open_listen_socket(80);
    assert_int_equal(-1, fd);
}


static void test__http_create_select_sets__can_add_listen_fd(void **states)
{
    struct http_server server;
    int maxfd;
    fd_set set_read, set_write, set_test;

    init_server(&server);

    http_create_select_sets(&server, &set_read, &set_write, &maxfd);

    assert_int_equal(3, maxfd);

    FD_ZERO(&set_test);
    FD_SET(3, &set_test);
    assert_memory_equal(&set_test, &set_read, sizeof(set_test));
    assert_true(FD_ISSET(3, &set_read));

    FD_ZERO(&set_test);
    assert_memory_equal(&set_test, &set_write, sizeof(set_test));
}

static void test__http_create_select_sets__can_add_request_fd_less_than_listen_fd_to_read_set(void **states)
{
    struct http_server server;
    int maxfd;
    fd_set set_read, set_write, set_test;

    init_server(&server);

    server.request[0].fd = 2;
    server.request[0].state = HTTP_STATE_SERVER_READ_METHOD;

    int n = http_create_select_sets(&server, &set_read, &set_write, &maxfd);

    assert_int_equal(3, maxfd);
    assert_int_equal(1, n);

    FD_ZERO(&set_test);
    FD_SET(2, &set_test);
    FD_SET(3, &set_test);
    assert_memory_equal(&set_test, &set_read, sizeof(set_test));

    FD_ZERO(&set_test);
    assert_memory_equal(&set_test, &set_write, sizeof(set_test));
}

static void test__http_create_select_sets__can_add_request_fd_greater_than_listen_fd_to_read_set(void **states)
{
    struct http_server server;
    int maxfd;
    fd_set set_read, set_write, set_test;

    init_server(&server);

    server.request[0].fd = 5;
    server.request[0].state = HTTP_STATE_SERVER_READ_PATH;
    server.request[2].fd = 2;
    server.request[2].state = HTTP_STATE_SERVER_READ_QUERY;

    int n = http_create_select_sets(&server, &set_read, &set_write, &maxfd);

    assert_int_equal(5, maxfd);
    assert_int_equal(2, n);

    FD_ZERO(&set_test);
    FD_SET(2, &set_test);
    FD_SET(3, &set_test);
    FD_SET(5, &set_test);
    assert_memory_equal(&set_test, &set_read, sizeof(set_test));

    FD_ZERO(&set_test);
    assert_memory_equal(&set_test, &set_write, sizeof(set_test));
}

static void test__http_create_select_sets__can_add_request_fd_waiting_for_nl_to_read_set(void **states)
{
    struct http_server server;
    int maxfd;
    fd_set set_read, set_write, set_test;

    init_server(&server);

    server.request[0].fd = 2;
    server.request[0].state = HTTP_STATE_IDLE | HTTP_STATE_READ_NL;

    int n = http_create_select_sets(&server, &set_read, &set_write, &maxfd);

    assert_int_equal(3, maxfd);
    assert_int_equal(1, n);

    FD_ZERO(&set_test);
    FD_SET(2, &set_test);
    FD_SET(3, &set_test);
    assert_memory_equal(&set_test, &set_read, sizeof(set_test));

    FD_ZERO(&set_test);
    assert_memory_equal(&set_test, &set_write, sizeof(set_test));
}


static void test__http_create_select_sets__does_not_add_listen_fd_if_full(void **states)
{
    struct http_server server;
    int maxfd;
    fd_set set_read, set_write;

    init_server(&server);

    for(int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
        server.request[i].fd = 3 + 1 + i;
        server.request[i].state = HTTP_STATE_SERVER_READ_HEADER;
    }

    int n = http_create_select_sets(&server, &set_read, &set_write, &maxfd);

    assert_int_equal(3 + HTTP_SERVER_MAX_CONNECTIONS, maxfd);
    assert_int_equal(HTTP_SERVER_MAX_CONNECTIONS, n);

    assert_false(FD_ISSET(3, &set_read));
    assert_false(FD_ISSET(3, &set_write));
}

static void test__http_create_select_sets__can_add_request_fd_to_read_and_write_set(void **states)
{
    struct http_server server;
    int maxfd;
    fd_set set_read, set_write, set_test;

    init_server(&server);

    server.request[0].fd = 2;
    server.request[0].state = HTTP_STATE_SERVER_WRITE_HEADER;
    server.request[1].fd = 4;
    server.request[1].state = HTTP_STATE_SERVER_READ_QUERY;

    int n = http_create_select_sets(&server, &set_read, &set_write, &maxfd);

    assert_int_equal(4, maxfd);
    assert_int_equal(2, n);

    FD_ZERO(&set_test);
    FD_SET(3, &set_test);
    FD_SET(4, &set_test);
    assert_memory_equal(&set_test, &set_read, sizeof(set_test));

    FD_ZERO(&set_test);
    FD_SET(2, &set_test);
    assert_memory_equal(&set_test, &set_write, sizeof(set_test));
}

static void test__http_create_select_sets__does_not_add_nonready_socket_to_sets(void **states)
{
    struct http_server server;
    int maxfd;
    fd_set set_read, set_write, set_test;

    init_server(&server);

    server.request[0].fd = 2;
    server.request[0].state = HTTP_STATE_IDLE;
    server.request[1].fd = 4;
    server.request[1].state = HTTP_STATE_ERROR;

    int n = http_create_select_sets(&server, &set_read, &set_write, &maxfd);

    assert_int_equal(3, maxfd);
    assert_int_equal(2, n);

    FD_ZERO(&set_test);
    FD_SET(3, &set_test);
    assert_memory_equal(&set_test, &set_read, sizeof(set_test));

    FD_ZERO(&set_test);
    assert_memory_equal(&set_test, &set_write, sizeof(set_test));
}

static void test__http_create_select_sets__can_add_websocket_fd_less_than_listen_fd_to_read_set(void **states)
{
    struct http_server server;
    int maxfd;
    fd_set set_read, set_write, set_test;

    init_server(&server);

    server.websocket_connection[0].fd = 2;

    int n = http_create_select_sets(&server, &set_read, &set_write, &maxfd);

    assert_int_equal(3, maxfd);
    assert_int_equal(0, n);

    FD_ZERO(&set_test);
    FD_SET(2, &set_test);
    FD_SET(3, &set_test);
    assert_memory_equal(&set_test, &set_read, sizeof(set_test));

    FD_ZERO(&set_test);
    assert_memory_equal(&set_test, &set_write, sizeof(set_test));
}

static void test__http_create_select_sets__can_add_websocket_fd_greater_than_listen_fd_to_read_set(void **states)
{
    struct http_server server;
    int maxfd;
    fd_set set_read, set_write, set_test;

    init_server(&server);

    server.websocket_connection[0].fd = 5;

    int n = http_create_select_sets(&server, &set_read, &set_write, &maxfd);

    assert_int_equal(5, maxfd);
    assert_int_equal(0, n);

    FD_ZERO(&set_test);
    FD_SET(5, &set_test);
    FD_SET(3, &set_test);
    assert_memory_equal(&set_test, &set_read, sizeof(set_test));

    FD_ZERO(&set_test);
    assert_memory_equal(&set_test, &set_write, sizeof(set_test));
}


static void test__http_accept_new_connection__accepts_new_connection_when_not_all_slots_are_empty(void **states)
{
    struct http_server server = {
        .fd = 3
    };

    server.request[0].fd = 1;
    for(int i = 1; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
        server.request[i].fd = -1;
    }

    socklen_t expected_len = sizeof(struct sockaddr_in);

    expect_value(accept, sockfd, 3);
    expect_any(accept, addr);
    expect_memory(accept, addrlen, &expected_len, sizeof(socklen_t));
    will_return(accept, 4);

    int index = http_accept_new_connection(&server);

    assert_int_equal(1, index);
    assert_int_equal(4, server.request[1].fd);
}

static void test__http_accept_new_connection__accepts_new_connection_when_all_slots_are_empty(void **states)
{
    struct http_server server = {
        .fd = 3
    };

    for(int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
        server.request[i].fd = -1;
    }

    socklen_t expected_len = sizeof(struct sockaddr_in);

    expect_value(accept, sockfd, 3);
    expect_any(accept, addr);
    expect_memory(accept, addrlen, &expected_len, sizeof(socklen_t));
    will_return(accept, 4);

    int index = http_accept_new_connection(&server);

    assert_int_equal(0, index);
    assert_int_equal(4, server.request[0].fd);
}

static void test__http_accept_new_connection__fails_if_there_are_no_empty_slot(void **states)
{
    struct http_server server = {
        .fd = 3
    };

    for(int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
        server.request[i].fd = 3 + 1 + i;
    }

    int index = http_accept_new_connection(&server);

    assert_int_equal(-1, index);
}
static void test__http_accept_new_connection__fails_if_accept_fails(void **states)
{
    struct http_server server = {
        .fd = 3
    };

    for(int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
        server.request[i].fd = -1;
    }
    expect_any(accept, sockfd);
    expect_any(accept, addr);
    expect_any(accept, addrlen);
    will_return(accept, -1);

    int index = http_accept_new_connection(&server);

    for(int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
        assert_int_equal(-1, server.request[i].fd);
    }

    assert_int_equal(-1, index);
}

static void test__http_accept_new_connection__initialises_the_new_request(void **states)
{
    struct http_server server;

    memset(&server, 0x55, sizeof(server));

    server.fd = 3;

    for(int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
        server.request[i].fd = -1;
    }

    socklen_t expected_len = sizeof(struct sockaddr_in);

    expect_value(accept, sockfd, 3);
    expect_any(accept, addr);
    expect_memory(accept, addrlen, &expected_len, sizeof(socklen_t));
    will_return(accept, 4);

    int index = http_accept_new_connection(&server);

    assert_int_equal(0, index);
    assert_int_equal(server.request[0].fd, 4);
    assert_int_equal(server.request[0].state, HTTP_STATE_SERVER_READ_BEGIN);
}

static void test__http_response_init__initialises_the_request(void **states)
{
    struct http_request request;
    memset(&request, 0x55, sizeof(request));
    request.fd = 3;

    http_response_init(&request);

    assert_int_equal(request.fd, 3);
    assert_int_equal(request.state, HTTP_STATE_SERVER_READ_BEGIN);
    assert_int_equal(request.flags, 0);
    assert_null(request.path);
    assert_null(request.query);
    assert_null(request.host);
    assert_null(request.line);
    assert_int_equal(request.line_length, 0);
    assert_null(request.query_list);
    assert_int_equal(request.read_content_length, -1);
    assert_int_equal(request.write_content_length, -1);
    assert_int_equal(request.poke, -1);
    assert_int_equal(request.chunk_length, 0);
    assert_int_equal(request.status, 0);
    assert_int_equal(request.error, 0);
    assert_int_equal(request.method, HTTP_METHOD_UNKNOWN);
    assert_null(request.handler);
    assert_null(request.cgi_data);
    assert_null(request.cgi_arg);
    assert_null(request.websocket_key);
    assert_true(http_is_server(&request));
    assert_false(http_is_client(&request));
    assert_false(http_is_error(&request));
}

static void test__http_request_init__initialises_the_request(void **states)
{
    struct http_request request;
    memset(&request, 0x55, sizeof(request));
    request.fd = 3;

    http_request_init(&request);

    assert_int_equal(request.fd, 3);
    assert_int_equal(request.state, HTTP_STATE_CLIENT_IDLE);
    assert_int_equal(request.flags, 0);
    assert_null(request.path);
    assert_null(request.query);
    assert_null(request.host);
    assert_null(request.line);
    assert_int_equal(request.line_length, 0);
    assert_null(request.query_list);
    assert_int_equal(request.read_content_length, -1);
    assert_int_equal(request.write_content_length, -1);
    assert_int_equal(request.poke, -1);
    assert_int_equal(request.chunk_length, 0);
    assert_false(http_is_server(&request));
    assert_true(http_is_client(&request));
    assert_false(http_is_error(&request));
}

static void test__websocket_flush__flushes_the_socket(void **states)
{
    struct websocket_connection conn = {
        .fd = 3,
    };

    int val_zero = 0;
    int val_one = 1;

    expect_value(setsockopt, fd, 3);
    expect_value(setsockopt, level, IPPROTO_TCP);
    expect_value(setsockopt, optname, TCP_NODELAY);
    expect_memory(setsockopt, optval, &val_one, sizeof(val_one));
    expect_value(setsockopt, optlen, sizeof(val_one));

    will_return(setsockopt, 0);

    expect_value(setsockopt, fd, 3);
    expect_value(setsockopt, level, IPPROTO_TCP);
    expect_value(setsockopt, optname, TCP_NODELAY);
    expect_memory(setsockopt, optval, &val_zero, sizeof(val_zero));
    expect_value(setsockopt, optlen, sizeof(val_zero));
    will_return(setsockopt, 0);

    websocket_flush(&conn);
}


// Main ////////////////////////////////////////////////////////////////////////

const struct CMUnitTest tests_for_http_socket[] = {
    cmocka_unit_test(test__http_open_request_socket__can_open_a_socket),
    cmocka_unit_test(test__http_open_request_socket__can_open_a_socket_with_non_default_port),
    cmocka_unit_test(test__http_open_request_socket__default_port_is_80),
    cmocka_unit_test(test__http_open_request_socket__returns_minus_one_if_getaddrinfo_returns_error),
    cmocka_unit_test(test__http_open_request_socket__returns_minus_one_if_getaddrinfo_gives_null_res),
    cmocka_unit_test(test__http_open_request_socket__returns_minus_one_if_socket_fails),
    cmocka_unit_test(test__http_open_request_socket__returns_minus_one_if_connect_fails),

    cmocka_unit_test(test__http_close__closes_the_socket),
    cmocka_unit_test(test__http_close__closes_the_socket_when_client),
    cmocka_unit_test(test__http_close__does_not_close_a_closed_socket),

    cmocka_unit_test(test__http_open_listen_socket__opens_and_binds_and_listens),

    cmocka_unit_test(test__http_open_listen_socket__fails_if_socket_fails),
    cmocka_unit_test(test__http_open_listen_socket__fails_if_bind_fails),
    cmocka_unit_test(test__http_open_listen_socket__fails_if_listen_fails),

    cmocka_unit_test(test__http_create_select_sets__can_add_listen_fd),
    cmocka_unit_test(test__http_create_select_sets__can_add_request_fd_less_than_listen_fd_to_read_set),
    cmocka_unit_test(test__http_create_select_sets__can_add_request_fd_greater_than_listen_fd_to_read_set),
    cmocka_unit_test(test__http_create_select_sets__can_add_request_fd_waiting_for_nl_to_read_set),
    cmocka_unit_test(test__http_create_select_sets__does_not_add_listen_fd_if_full),
    cmocka_unit_test(test__http_create_select_sets__can_add_request_fd_to_read_and_write_set),
    cmocka_unit_test(test__http_create_select_sets__does_not_add_nonready_socket_to_sets),
    cmocka_unit_test(test__http_create_select_sets__can_add_websocket_fd_less_than_listen_fd_to_read_set),
    cmocka_unit_test(test__http_create_select_sets__can_add_websocket_fd_greater_than_listen_fd_to_read_set),

    cmocka_unit_test(test__http_accept_new_connection__fails_if_there_are_no_empty_slot),
    cmocka_unit_test(test__http_accept_new_connection__accepts_new_connection_when_all_slots_are_empty),
    cmocka_unit_test(test__http_accept_new_connection__accepts_new_connection_when_not_all_slots_are_empty),
    cmocka_unit_test(test__http_accept_new_connection__fails_if_accept_fails),
    cmocka_unit_test(test__http_accept_new_connection__initialises_the_new_request),

    cmocka_unit_test(test__http_response_init__initialises_the_request),
    cmocka_unit_test(test__http_request_init__initialises_the_request),

    cmocka_unit_test(test__websocket_flush__flushes_the_socket),
};

int main(void)
{
    int fails = 0;
    fails += cmocka_run_group_tests(tests_for_http_socket, NULL, NULL);

    return fails;
}
