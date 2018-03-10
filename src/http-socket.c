#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "http.h"
#include "log.h"

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
    close(request->fd);
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

    return fd;
}
