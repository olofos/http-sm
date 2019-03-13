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
#include <errno.h>
#endif

#include "http-sm/http.h"
#include "http-private.h"
#include "log.h"

static void http_write_error_response(struct http_request *request)
{
    const char *message = http_status_string(request->error);

    http_begin_response(request, request->error, "text/plain");
    http_set_content_length(request, strlen(message));
    http_end_header(request);
    http_write_string(request, message);
    http_end_body(request);
}


static int http_server_read_headers(struct http_request *request)
{
    if(request->state == HTTP_STATE_SERVER_READ_BODY) {
        return 0;
    } else if(request->state == HTTP_STATE_SERVER_READ_DONE) {
        char c;
        int n = read(request->fd, &c, 1);

        if(n < 0) {
            ERROR("read failed before EOF");
        } else if(n == 0) {
            INFO("Connection %d done", request->fd);
        } else {
            LOG("Expected EOF but got %c", c);
        }

        request->state = HTTP_STATE_SERVER_IDLE;
        http_close(request);
    } else {
        if(request->state == HTTP_STATE_SERVER_READ_BEGIN) {
            request->state = HTTP_STATE_SERVER_READ_METHOD;
            const int line_length = HTTP_LINE_LEN;
            request->line = malloc(line_length);
            request->line_length = line_length;
            request->line_index = 0;
        }

        char c;
        int n = read(request->fd, &c, 1);
        if(n < 0) {
            ERROR("read failed in header");
            request->state = HTTP_STATE_ERROR;

            free(request->line);
            request->line = 0;
            request->line_length = 0;

            http_close(request);
            return -1;
        } else if(n==0) {
            LOG("Unexpected EOF");
            request->state = HTTP_STATE_ERROR;

            free(request->line);
            request->line = 0;
            request->line_length = 0;

            http_close(request);
            return -1;
        } else {
            http_parse_header(request, c);
            if(request->state == HTTP_STATE_ERROR) {
                free(request->line);
                request->line = 0;
                request->line_length = 0;

                http_write_error_response(request);

                http_close(request);
            } else if(request->state == HTTP_STATE_SERVER_IDLE) {
                free(request->line);
                request->line = 0;
                request->line_length = 0;

                if(request->flags & HTTP_FLAG_WEBSOCKET) {
                    request->state = HTTP_STATE_SERVER_UPGRADE_WEBSOCKET;
                } else if((request->read_content_length > 0) || (request->flags & HTTP_FLAG_READ_CHUNKED)) {
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

    if(request->state == HTTP_STATE_SERVER_READ_BODY) {
        if(http_read(request, buf, sizeof(buf)) > 0) {
            LOG("Beginning response before reading body");
        }

        do {
            http_read(request, buf, sizeof(buf));
        } while(request->state != HTTP_STATE_SERVER_IDLE);
    }

    request->state = HTTP_STATE_SERVER_WRITE_HEADER;

    snprintf(buf, sizeof(buf), "HTTP/1.1 %d %s\r\n", status, http_status_string(status));
    http_write_string(request, buf);
    http_write_header(request, "Connection", "close");

    if(content_type) {
        http_write_header(request, "Content-Type", content_type);
    }

    return 0;
}

enum http_cgi_state cgi_not_found(struct http_request* request);

static int http_server_call_handler(struct http_request *request)
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

static int http_server_main_loop(struct http_server *server)
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
        LOG("select failed with %s", strerror(errno));
        return -1;
    }
#endif

    if(n <= 0) {
        for(int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
            if(server->request[i].fd >= 0) {
                INFO("Socket %d timed out. Closing.", server->request[i].fd);
                http_close(&server->request[i]);
            }
        }
    } else {
        if(FD_ISSET(server->fd, &set_read)) {
            http_accept_new_connection(server);
        }

        for(int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
            struct http_request *request = &server->request[i];
            if(request->fd >= 0) {
                if(FD_ISSET(request->fd, &set_read)) {
                    if(request->state == HTTP_STATE_SERVER_READ_BODY) {
                        http_server_call_handler(request);
                    } else {
                        http_server_read_headers(request);

                        if(request->state == HTTP_STATE_SERVER_UPGRADE_WEBSOCKET) {
                            if(websocket_init(server, request)) {
                                FD_CLR(request->fd, &set_read);
                                http_free(request);
                                request->fd = -1;
                            } else {
                                http_close(request);
                            }
                        }
                    }
                } else if(FD_ISSET(server->request[i].fd, &set_write)) {
                    http_server_call_handler(&server->request[i]);
                }
            }
        }

        for(int i = 0; i < WEBSOCKET_SERVER_MAX_CONNECTIONS; i++) {
            if(server->websocket_connection[i].fd >= 0) {
                if(FD_ISSET(server->websocket_connection[i].fd, &set_read)) {
                    struct websocket_connection *conn = &server->websocket_connection[i];
                    websocket_read_frame_header(conn);

                    switch(conn->frame_opcode & WEBSOCKET_FRAME_OPCODE) {
                    case WEBSOCKET_FRAME_OPCODE_BIN:
                    case WEBSOCKET_FRAME_OPCODE_TEXT:
                    {
                        if(conn->handler->cb_message) {
                            conn->handler->cb_message(conn);
                        }
                        break;
                    }
                    case WEBSOCKET_FRAME_OPCODE_CLOSE:
                    {
                        char *str = malloc(conn->frame_length);
                        websocket_read(conn, str, conn->frame_length);
                        websocket_send(conn, str, conn->frame_length, WEBSOCKET_FRAME_FIN | WEBSOCKET_FRAME_OPCODE_CLOSE);

                        if(conn->handler->cb_close) {
                            conn->handler->cb_close(conn);
                        }

                        close(conn->fd);
                        conn->fd = -1;

                        free(str);
                        break;
                    }
                    case WEBSOCKET_FRAME_OPCODE_PING:
                    {
                        LOG("WS: ping %d", conn->fd);
                        char *str = malloc(conn->frame_length);
                        websocket_read(conn, str, conn->frame_length);
                        websocket_send(conn, str, conn->frame_length, WEBSOCKET_FRAME_FIN | WEBSOCKET_FRAME_OPCODE_PONG);
                        free(str);
                        break;
                    }
                    }
                }
            }
        }
    }

    return 0;
}

int websocket_init(struct http_server *server, struct http_request *request)
{
    struct websocket_connection *connection = 0;
    for(int i = 0; i < WEBSOCKET_SERVER_MAX_CONNECTIONS; i++) {
        if(server->websocket_connection[i].fd == -1) {
            connection = &server->websocket_connection[i];
            break;
        }
    }
    if(connection) {
        for(struct websocket_url_handler *handler = &websocket_url_tab[0]; handler->url != NULL; handler++) {
            if(http_server_match_url(handler->url, request->path)) {
                LOG("WS: %s matches", handler->url);
                if(handler->cb_open(connection, request)) {
                    websocket_send_response(request);
                    connection->fd = request->fd;
                    connection->handler = handler;
                    return 1;
                } else {
                    break;
                }
            }
        }
        // TODO: Send 404 instead
        websocket_send_error_response(request);
        LOG("WS: connection %d not accepted", request->fd);
        return 0;
    } else {
        websocket_send_error_response(request);
        LOG("WS: no room for new connection %d", request->fd);
        return 0;
    }
}


static int http_server_start(struct http_server *server, int port)
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

    for(int i = 0; i < WEBSOCKET_SERVER_MAX_CONNECTIONS; i++) {
        server->websocket_connection[i].fd = -1;
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
#ifndef __XTENSA__
            if(errno != EINTR) {
                return -1;
            }
#else
            return -1;
#endif
        }
    }
}
