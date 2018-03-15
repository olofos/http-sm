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
    HTTP_STATE_DONE                  = 0x00,
    HTTP_STATE_ERROR                 = 0x01,
    //
    HTTP_STATE_READ                  = 0x10 | 0x80,
    //
    HTTP_STATE_READ_REQ_BEGIN        = 0x11,
    HTTP_STATE_READ_RESP_BEGIN       = 0x12,
    //
    HTTP_STATE_READ_REQ_METHOD       = 0x13,
    HTTP_STATE_READ_REQ_PATH         = 0x14,
    HTTP_STATE_READ_REQ_QUERY        = 0x15,
    HTTP_STATE_READ_REQ_VERSION      = 0x16,
    //
    HTTP_STATE_READ_RESP_VERSION     = 0x17,
    HTTP_STATE_READ_RESP_STATUS      = 0x18,
    HTTP_STATE_READ_RESP_STATUS_DESC = 0x19,
    //
    HTTP_STATE_READ_HEADER           = 0x1A,
    //
    HTTP_STATE_READ_BODY             = 0x1B,
    //
    HTTP_STATE_READ_EXPECT_EOF       = 0x1F,
    //
    HTTP_STATE_READ_NL               = 0x80,
    //
    HTTP_STATE_WRITE                 = 0x20,
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
    HTTP_FLAG_REQUEST     = 0x04,
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
    uint8_t method;
    uint8_t flags;

    int status;
    int error;

    char *line;
    int line_index;
    int line_len;

    int content_length;
    int chunk_length;

    int poke;

    char *path;
    char *query;
    char *host;

    char **query_list;

    int fd;
    uint16_t port;

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
