#ifndef HTTP_PRIVATE_H_
#define HTTP_PRIVATE_H_

struct http_request;

int http_hex_to_int(char c);

void http_parse_header(struct http_request *request, char c);
int http_urldecode(char *dest, const char* src, int max_len);
int http_begin_request(struct http_request *request);

int http_open_request_socket(struct http_request *request);

int http_open_listen_socket(int port);

#endif
