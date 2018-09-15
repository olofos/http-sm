#include <unistd.h>
#include <stdio.h>
#include <string.h>

#ifdef __XTENSA__
#include "lwip/lwip/sockets.h"
#include "lwip/lwip/netdb.h"
#include <esp_common.h>

#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#endif

#include "http-sm/http.h"
#include "http-private.h"
#include "log.h"

int http_server_read(struct http_request *request)
{
    if(request->state == HTTP_STATE_SERVER_READ_BODY) {
        return 0;
    } else if(request->state == HTTP_STATE_SERVER_READ_DONE) {
        char c;
        int n = read(request->fd, &c, 1);

        if(n < 0) {
            LOG_ERROR("read failed");
        } else if(n == 0) {
            LOG("Connection %d done", request->fd);
        } else {
            LOG("Expected EOF but got %c", c);
        }

        request->state = HTTP_STATE_IDLE;
        http_close(request);
    } else {
        if(request->state == HTTP_STATE_SERVER_READ_BEGIN) {
            request->state = HTTP_STATE_SERVER_READ_METHOD;
            const int line_len = HTTP_LINE_LEN;
            request->line = malloc(line_len);
            request->line_len = line_len;
            request->line_index = 0;
        }

        char c;
        int n = read(request->fd, &c, 1);
        if(n < 0) {
            LOG_ERROR("read2 failed");
            request->state = HTTP_STATE_ERROR;

            free(request->line);
            request->line = 0;
            request->line_len = 0;

            http_close(request);
            return -1;
        } else if(n==0) {
            LOG("Unexpected EOF");
            request->state = HTTP_STATE_ERROR;

            free(request->line);
            request->line = 0;
            request->line_len = 0;

            http_close(request);
            return -1;
        } else {
            http_parse_header(request, c);
            if(request->state == HTTP_STATE_IDLE) {
                free(request->line);
                request->line = 0;
                request->line_len = 0;

                if(request->method == HTTP_METHOD_POST || request->method == HTTP_METHOD_DELETE) {
                    request->state = HTTP_STATE_SERVER_READ_BODY;
                } else {
                    request->state = HTTP_STATE_SERVER_WRITE_BEGIN;
                }
            }
        }
    }
    return 0;
}

int http_begin_response(struct http_request *request, int status, const char *content_type)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "HTTP/1.1 %d %s\r\n", status, http_status_string(status));
    http_write_string(request, buf);
    http_write_header(request, "Connection", "close");
    http_write_header(request, "Content-Type", content_type);

    return 0;
}

int http_end_body(struct http_request *request)
{
    if(request->flags & HTTP_FLAG_WRITE_CHUNKED) {
        http_write_string(request, "");
    }
    return 0;
}

enum http_cgi_state cgi_not_found(struct http_request* request);

int http_server_write(struct http_request *request)
{
    int i = 0;

    for(;;) {

        if(request->handler) {
            enum http_cgi_state state = request->handler(request);

            if(state == HTTP_CGI_DONE) {
                request->state = HTTP_STATE_SERVER_READ_DONE;
                break;
            } else if(state == HTTP_CGI_MORE) {
                break;
            }
        }

        if(http_url_tab[i].url == NULL) {
            request->handler = cgi_not_found;
            continue;
        } else if(http_server_match_url(http_url_tab[i].url, request->path)) {
            request->handler = http_url_tab[i].handler;
            request->cgi_arg = http_url_tab[i].cgi_arg;
        }

        i++;
    }

    return 0;
}

int http_server_main_loop(struct http_server *server)
{
    fd_set set_read, set_write;
    int maxfd;

    int num_open = http_create_select_sets(server, &set_read, &set_write, &maxfd);

    int n;
    if(num_open != 0) {
        struct timeval t;
        t.tv_sec = HTTP_SERVER_TIMEOUT_SECS;
        t.tv_usec = HTTP_SERVER_TIMEOUT_USECS;

        n = select(maxfd+1, &set_read, &set_write, 0, &t);
    } else {
        n = select(maxfd+1, &set_read, &set_write, 0, 0);
    }

    // select seems to return -1 on timeout on esp8266 RTOS SDK
    // and does not set errno...
#ifndef __XTENSA__
    if(n < 0) {
        LOG("select failed");
        return -1;
    }
#endif

    if(n <= 0) {
        for(int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
            if(server->request[i].fd >= 0) {
                LOG("Socket %d timed out. Closing.", server->request[i].fd);
                http_close(&server->request[i]);
            }
        }
    } else {
        if(FD_ISSET(server->fd, &set_read)) {
            http_accept_new_connection(server);
        }

        for(int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
            if(server->request[i].fd >= 0) {
                if(FD_ISSET(server->request[i].fd, &set_read)) {
                    http_server_read(&server->request[i]);
                } else if(FD_ISSET(server->request[i].fd, &set_write)) {
                    http_server_write(&server->request[i]);
                }
            }
        }
    }

    return 0;
}

int http_server_start(struct http_server *server, int port)
{
    int listen_fd = http_open_listen_socket(port);

    if(listen_fd < 0) {
        return -1;
    }

    server->fd = listen_fd;

    for(int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
        server->request[i].fd = -1;
        server->request[i].state = HTTP_STATE_IDLE;
    }

    return 0;
}

int http_server_main(int port)
{
    struct http_server server;

    if(http_server_start(&server, port) < 0) {
        return -1;
    }

    for(;;) {
        if(http_server_main_loop(&server) < 0) {
            return -1;
        }
    }
}
