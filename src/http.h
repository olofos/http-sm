#ifndef HTTP_H_
#define HTTP_H_

#include <stdlib.h>
#include <stdint.h>

#ifndef HTTP_USER_AGENT
#define HTTP_USER_AGENT "esp8266-http/0.1"
#endif

#define HTTP_SERVER_MAX_CONNECTIONS 3
#define HTTP_LINE_LEN 64

enum http_state
{
    HTTP_STATE_CLIENT                  = 0x80,
    HTTP_STATE_ERROR                   = 0x40,
    HTTP_STATE_READ_NL                 = 0x20,
    HTTP_STATE_READ                    = 0x10,
    HTTP_STATE_WRITE                   = 0x08,

    HTTP_STATE_IDLE                    = 0x00,
    HTTP_STATE_HEADER                  = 0x05,
    HTTP_STATE_BODY                    = 0x06,
    HTTP_STATE_DONE                    = 0x07,
    //

    HTTP_STATE_SERVER_READ_BEGIN        = HTTP_STATE_READ | HTTP_STATE_IDLE,
    HTTP_STATE_SERVER_READ_METHOD       = HTTP_STATE_READ | 0x01,
    HTTP_STATE_SERVER_READ_PATH         = HTTP_STATE_READ | 0x02,
    HTTP_STATE_SERVER_READ_QUERY        = HTTP_STATE_READ | 0x03,
    HTTP_STATE_SERVER_READ_VERSION      = HTTP_STATE_READ | 0x04,
    HTTP_STATE_SERVER_READ_HEADER       = HTTP_STATE_READ | HTTP_STATE_HEADER,
    HTTP_STATE_SERVER_READ_BODY         = HTTP_STATE_READ | HTTP_STATE_BODY,
    HTTP_STATE_SERVER_READ_DONE         = HTTP_STATE_READ | HTTP_STATE_DONE,

    HTTP_STATE_SERVER_WRITE_BEGIN       = HTTP_STATE_WRITE | HTTP_STATE_IDLE,
    HTTP_STATE_SERVER_WRITE_HEADER      = HTTP_STATE_WRITE | HTTP_STATE_HEADER,
    HTTP_STATE_SERVER_WRITE_BODY        = HTTP_STATE_WRITE | HTTP_STATE_BODY,
    HTTP_STATE_SERVER_WRITE_DONE        = HTTP_STATE_WRITE | HTTP_STATE_DONE,


    HTTP_STATE_CLIENT_READ_BEGIN        = HTTP_STATE_CLIENT | HTTP_STATE_READ | HTTP_STATE_IDLE,
    HTTP_STATE_CLIENT_READ_VERSION      = HTTP_STATE_CLIENT | HTTP_STATE_READ | 0x01,
    HTTP_STATE_CLIENT_READ_STATUS       = HTTP_STATE_CLIENT | HTTP_STATE_READ | 0x02,
    HTTP_STATE_CLIENT_READ_STATUS_DESC  = HTTP_STATE_CLIENT | HTTP_STATE_READ | 0x03,
    HTTP_STATE_CLIENT_READ_HEADER       = HTTP_STATE_CLIENT | HTTP_STATE_READ | HTTP_STATE_HEADER,
    HTTP_STATE_CLIENT_READ_BODY         = HTTP_STATE_CLIENT | HTTP_STATE_READ | HTTP_STATE_BODY,
    HTTP_STATE_CLIENT_READ_DONE         = HTTP_STATE_CLIENT | HTTP_STATE_READ | HTTP_STATE_DONE,

    HTTP_STATE_CLIENT_WRITE_BEGIN       = HTTP_STATE_CLIENT | HTTP_STATE_WRITE | HTTP_STATE_IDLE,
    HTTP_STATE_CLIENT_WRITE_HEADER      = HTTP_STATE_CLIENT | HTTP_STATE_WRITE | HTTP_STATE_HEADER,
    HTTP_STATE_CLIENT_WRITE_BODY        = HTTP_STATE_CLIENT | HTTP_STATE_WRITE | HTTP_STATE_BODY,
    HTTP_STATE_CLIENT_WRITE_DONE        = HTTP_STATE_CLIENT | HTTP_STATE_WRITE | HTTP_STATE_DONE,
};

enum http_method
{
    HTTP_METHOD_UNKNOWN = 0,
    HTTP_METHOD_GET = 1,
    HTTP_METHOD_POST = 2,
    HTTP_METHOD_UNSUPPORTED = 3,
};

enum http_status
{
    HTTP_STATUS_OK                    = 200,
    HTTP_STATUS_BAD_REQUEST           = 400,
    HTTP_STATUS_NOT_FOUND             = 404,
    HTTP_STATUS_METHOD_NOT_ALLOWED    = 405,
    HTTP_STATUS_VERSION_NOT_SUPPORTED = 505,
};

enum http_flags
{
    HTTP_FLAG_ACCEPT_GZIP = 0x01,
    HTTP_FLAG_CHUNKED     = 0x02,
    HTTP_FLAG_CLIENT      = 0x04,
};

enum http_cgi_state
{
    HTTP_CGI_DONE,
    HTTP_CGI_MORE,
    HTTP_CGI_NOT_FOUND,
};

struct http_request;

typedef enum http_cgi_state (*http_url_handler_func)(struct http_request*);

struct http_request
{
    uint8_t state;
    uint8_t flags;

    char *line;
    int line_index;
    int line_len;

    int content_length;
    int chunk_length;

    int poke;

    char *path;
    char *host;

    int fd;

    // This is only used by the client for outgoing requests
    uint16_t port;


    // Theses are only used by the server for incoming requests
    uint8_t method;
    int status;
    int error;

    char *query;
    char **query_list;

    http_url_handler_func handler;
    void *cgi_arg;
    void *cgi_data;
};

struct http_url_handler {
    const char *url;
    http_url_handler_func handler;
    const void *cgi_arg;
};

extern struct http_url_handler *http_url_tab;

int http_getc(struct http_request *request);
int http_peek(struct http_request *request);

const char *http_get_query_arg(struct http_request *request, const char *name);

int http_write_string(struct http_request *request, const char *str);

void http_write_header(struct http_request *request, const char *name, const char *value);
void http_end_header(struct http_request *request);

void http_set_content_length(struct http_request *request, int length);

int http_close(struct http_request *request);

int http_get_request(struct http_request *request);

#endif
