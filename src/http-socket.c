#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "http.h"
#include "http-private.h"
#include "log.h"

#ifndef IP2STR
#define IP2STR(ip) (((ip) >> 24) & 0xFF), (((ip) >> 16) & 0xFF), (((ip) >> 8) & 0xFF), ((ip) & 0xFF)
#endif


int http_open_request_socket(struct http_request *request)
{
    const struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;

    char port_str[6];

    if(request->port <= 0) {
        request->port = 80;
    }

    sprintf(port_str, "%d", request->port);

    int err = getaddrinfo(request->host, port_str, &hints, &res);

    if (err != 0 || res == NULL)
    {
        perror("getaddrinfo");
        if(res) {
            freeaddrinfo(res);
        }

        return -1;
    }

    struct sockaddr *sa = res->ai_addr;
    if (sa->sa_family == AF_INET)
    {
        LOG("DNS lookup for %s succeeded. IP=%s", request->host, inet_ntoa(((struct sockaddr_in *)sa)->sin_addr));
    }

    int s = socket(res->ai_family, res->ai_socktype, 0);

    if(s < 0) {
        perror("socket");
        freeaddrinfo(res);
        return -1;
    }

    if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        perror("connect");
        close(s);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);

    request->fd = s;
    return s;
}

int http_close(struct http_request *request)
{
    if(request->fd < 0) {
        return -1;
    }

    if(http_is_server(request)) {
        free(request->host);
        free(request->path);
        free(request->query);
        free(request->query_list);
    }

    close(request->fd);
    request->fd = -1;
    return 0;
}

int http_open_listen_socket(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if(fd < 0) {
        perror("socket");
        return -1;
    }

    const struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr = {
            .s_addr = INADDR_ANY,
        },
        .sin_port = htons(port),
    };

    if(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    if(listen(fd, HTTP_SERVER_MAX_CONNECTIONS) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    LOG("Listening on port %d", port);

    return fd;
}

int http_create_select_sets(struct http_server *server, fd_set *set_read,
                            fd_set *set_write, int *maxfd)
{
    *maxfd = 0;

    FD_ZERO(set_read);
    FD_ZERO(set_write);

    int num = 0;

    for(int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
        int fd = server->request[i].fd;
        if(fd >= 0) {
            num++;
            if(server->request[i].state & (HTTP_STATE_READ | HTTP_STATE_READ_NL))
            {
                FD_SET(fd, set_read);

                if(fd > *maxfd) {
                    *maxfd = fd;
                }
            } else if(server->request[i].state & HTTP_STATE_WRITE) {
                FD_SET(fd, set_write);

                if(fd > *maxfd) {
                    *maxfd = fd;
                }
            } else {
                LOG("Request %d (fd %d) is neither reading nor writing", i, fd);
            }
        }
    }

    if(num < HTTP_SERVER_MAX_CONNECTIONS) {
        FD_SET(server->fd, set_read);

        if(server->fd > *maxfd) {
            *maxfd = server->fd;
        }
    };

#ifdef LOG_VERBOSE
    char buf[64];
    char *s = buf;

    s += sprintf(s, "Selecting on %d: ( ", num);
    for(int i = 0; i <= *maxfd; i++) {
        if(FD_ISSET(i, set_read)) {
            s += sprintf(s, "%d ", i);
        }
    }
    s += sprintf(s, ") ( ");
    for(int i = 0; i <= *maxfd; i++) {
        if(FD_ISSET(i, set_write)) {
            s += sprintf(s, "%d ", i);
        }
    }
    s += sprintf(s, ")");
    LOG(buf);
#endif

    return num;
}

int http_accept_new_connection(struct http_server *server)
{
    int i;
    for(i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
        if(server->request[i].fd < 0) {
            break;
        }
    }

    if(i == HTTP_SERVER_MAX_CONNECTIONS) {
        return -1;
    }

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    int fd = accept(server->fd, (struct sockaddr *)&addr, &len);

    if(fd < 0) {
        perror("accept");
        return -1;
    }

    uint32_t remote_ip = ntohl(addr.sin_addr.s_addr);
    LOG("Connection %d from %d.%d.%d.%d:%d", fd, IP2STR(remote_ip), addr.sin_port);

    struct http_request *request = &server->request[i];

    http_response_init(request);
    request->fd = fd;

    return i;
}

void http_response_init(struct http_request *request)
{
    request->state = HTTP_STATE_SERVER_READ_BEGIN;
    request->flags = 0;
    request->path = 0;
    request->query = 0;
    request->host = 0;
    request->line = 0;
    request->line_len = 0;
    request->query_list = 0;
    request->content_length = -1;
    request->poke = -1;
    request->method = HTTP_METHOD_UNKNOWN;
    request->status = 0;
    request->error = 0;
    request->chunk_length = 0;
    request->handler = 0;
    request->cgi_arg = 0;
    request->cgi_data = 0;
}
