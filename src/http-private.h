#ifndef HTTP_PRIVATE_H_
#define HTTP_PRIVATE_H_

#ifdef __XTENSA__
#include "lwip/lwip/sockets.h"
#else
#include <sys/select.h>
#endif

#include "http-sm/http.h"
#include "http-sm/websocket.h"

struct http_server {
    struct http_request request[HTTP_SERVER_MAX_CONNECTIONS];
    struct websocket_connection websocket_connection[WEBSOCKET_SERVER_MAX_CONNECTIONS];
    int fd;
};

int http_hex_to_int(char c);

void http_parse_header(struct http_request *request, char c);
int http_begin_request(struct http_request *request);

int http_open_request_socket(struct http_request *request);

int http_open_listen_socket(int port);

int http_create_select_sets(struct http_server *server, fd_set *set_read, fd_set *set_write, int *maxfd);

int http_accept_new_connection(struct http_server *server);

void http_response_init(struct http_request *request);

int http_server_match_url(const char *server_url, const char *request_url);
const char *http_status_string(enum http_status status);

void http_free(struct http_request *request);

#define http_is_server(request) (!((request)->state & HTTP_STATE_CLIENT))
#define http_is_client(request)   ((request)->state & HTTP_STATE_CLIENT)

int http_write_all(int fd, const char *str, int len);
int http_read_all(int fd, void *buf_, size_t count);

int websocket_init(struct http_server *server, struct http_request *request);
void websocket_send_response(struct http_request *request);
void websocket_read_frame_header(struct websocket_connection *conn);
void websocket_flush(struct websocket_connection *conn);

#endif
