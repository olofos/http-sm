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

    LOG("HTTP error: %s", message);

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
            request->line = malloc(HTTP_LINE_LEN);
            if(!request->line) {
                request->state = HTTP_STATE_ERROR;
                request->error = HTTP_STATUS_INTERNAL_SERVER_ERROR;
                return -1;
            }

            request->line_length = HTTP_LINE_LEN;
            request->line_index = 0;

            request->state = HTTP_STATE_SERVER_READ_METHOD;
        }

        char c;
        int n = read(request->fd, &c, 1);
        if(n < 0) {
            ERROR("read failed in header");
            request->state = HTTP_STATE_ERROR;
            request->error = HTTP_STATUS_ERROR;
            return -1;
        } else if(n==0) {
            LOG("Unexpected EOF");
            request->state = HTTP_STATE_ERROR;
            request->error = HTTP_STATUS_ERROR;
            return -1;
        } else {
            http_parse_header(request, c);
            if(request->state == HTTP_STATE_SERVER_IDLE) {
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

static void websocket_handle_message(struct websocket_connection *conn)
{
    if(conn->handler->cb_message) {
        conn->handler->cb_message(conn);
    } else {
        int len = conn->frame_length - conn->frame_index;
        int ret;
        do {
            char buf[32];
            int to_read = (sizeof(buf) < len) ? sizeof(buf) : len;
            ret = websocket_read(conn, buf, to_read);
        } while(ret > 0);
    }
}

void websocket_close(struct websocket_connection *conn, uint8_t *buf, int len)
{
    websocket_send(conn, buf, len, WEBSOCKET_FRAME_FIN | WEBSOCKET_FRAME_OPCODE_CLOSE);

    conn->state = WEBSOCKET_STATE_CLOSED;

    if(conn->handler->cb_close) {
        conn->handler->cb_close(conn);
    }

    close(conn->fd);
    conn->fd = -1;
}

static void websocket_handle_close(struct websocket_connection *conn)
{
    uint8_t *str = malloc(conn->frame_length);

    if(str) {
        websocket_read(conn, str, conn->frame_length);
        websocket_close(conn, str, conn->frame_length);
        free(str);
    } else {
        websocket_close(conn, NULL, 0);
    }
}

static void websocket_handle_ping(struct websocket_connection *conn)
{
    LOG("WS: ping %d", conn->fd);
    char *str = malloc(conn->frame_length);
    if(str) {
        websocket_read(conn, str, conn->frame_length);
        websocket_send(conn, str, conn->frame_length, WEBSOCKET_FRAME_FIN | WEBSOCKET_FRAME_OPCODE_PONG);
        free(str);
    } else {
        websocket_send(conn, "", 0, WEBSOCKET_FRAME_FIN | WEBSOCKET_FRAME_OPCODE_PONG);
    }
}

static void websocket_handle_connection(struct websocket_connection *conn)
{
    if(conn->state != WEBSOCKET_STATE_BODY) {
        char c;
        int ret = read(conn->fd, &c, 1);

        if(ret < 0) {
            ERROR("Reading websocket");
            conn->state = WEBSOCKET_STATE_ERROR;
        } else if(ret == 0) {
            LOG("Unexpected EOF in state %02x", conn->state);
            conn->state = WEBSOCKET_STATE_ERROR;
        } else {
            websocket_parse_frame_header(conn, c);
        }
    }

    if(conn->state == WEBSOCKET_STATE_BODY) {
        switch(conn->frame_opcode & WEBSOCKET_FRAME_OPCODE) {
        case WEBSOCKET_FRAME_OPCODE_CONT:
        case WEBSOCKET_FRAME_OPCODE_BIN:
        case WEBSOCKET_FRAME_OPCODE_TEXT:
        case WEBSOCKET_FRAME_OPCODE_PONG:
        {
            websocket_handle_message(conn);
            break;
        }
        case WEBSOCKET_FRAME_OPCODE_CLOSE:
        {
            websocket_handle_close(conn);
            break;
        }
        case WEBSOCKET_FRAME_OPCODE_PING:
        {
            websocket_handle_ping(conn);
            break;
        }
        }

        if(conn->state == WEBSOCKET_STATE_DONE) {
            conn->state = WEBSOCKET_STATE_OPCODE;
        }
    }

    if(conn->state == WEBSOCKET_STATE_ERROR) {
        uint8_t error_code[] = { 0x03, 0xEA };
        websocket_close(conn, error_code, sizeof(error_code));
    }
}

static void http_handle_request_read(struct http_request *request, struct http_server *server)
{
    if(request->state == HTTP_STATE_SERVER_READ_BODY) {
        http_server_call_handler(request);
    } else {
        http_server_read_headers(request);

        if(request->state == HTTP_STATE_SERVER_UPGRADE_WEBSOCKET) {
            if(websocket_init(server, request) >= 0) {
                http_free(request);
                request->fd = -1;
            }
        }
    }
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

        for(int i = 0; i < WEBSOCKET_SERVER_MAX_CONNECTIONS; i++) {
            if(server->websocket_connection[i].fd >= 0) {
                if(FD_ISSET(server->websocket_connection[i].fd, &set_read)) {
                    websocket_handle_connection(&server->websocket_connection[i]);
                }
            }
        }

        for(int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
            struct http_request *request = &server->request[i];
            if(request->fd >= 0) {
                if(FD_ISSET(request->fd, &set_read)) {
                    http_handle_request_read(&server->request[i], server);
                } else if(FD_ISSET(server->request[i].fd, &set_write)) {
                    http_server_call_handler(&server->request[i]);
                }
                if(http_is_error(request)) {
                    free(request->line);
                    request->line = 0;
                    request->line_length = 0;

                    if(request->error > 0) {
                        http_write_error_response(request);
                    }

                    http_close(request);
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
                    connection->state = WEBSOCKET_STATE_OPCODE;
                    return request->fd;
                } else {
                    break;
                }
            }
        }
        request->state = HTTP_STATE_ERROR;
        request->error = HTTP_STATUS_NOT_FOUND;
        return -1;
    } else {
        request->state = HTTP_STATE_ERROR;
        request->error = HTTP_STATUS_SERVICE_UNAVAILABLE;
        LOG("WS: no room for new connection %d", request->fd);
        return -1;
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
