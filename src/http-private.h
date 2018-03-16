#ifndef HTTP_PRIVATE_H_
#define HTTP_PRIVATE_H_

#include <sys/select.h>

#include "http.h"

struct http_server {
    struct http_request request[HTTP_SERVER_MAX_CONNECTIONS];
    int fd;
};

int http_hex_to_int(char c);

void http_parse_header(struct http_request *request, char c);
int http_urldecode(char *dest, const char* src, int max_len);
int http_begin_request(struct http_request *request);

int http_open_request_socket(struct http_request *request);

int http_open_listen_socket(int port);

int http_create_select_sets(struct http_server *server, fd_set *set_read, fd_set *set_write, int *maxfd);

int http_accept_new_connection(struct http_server *server);

void http_response_init(struct http_request *request);

int http_server_match_url(const char *server_url, const char *request_url);
const char *http_status_string(enum http_status status);

#define http_is_response(request) (!((request)->flags & HTTP_FLAG_REQUEST))
#define http_is_request(request)    ((request)->flags & HTTP_FLAG_REQUEST)


#endif
