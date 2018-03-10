#ifndef HTTP_H_
#define HTTP_H_

#include <stdlib.h>
#include <stdint.h>

#ifndef HTTP_USER_AGENT
#define HTTP_USER_AGENT "esp8266-http/0.1"
#endif

enum http_state
{
    HTTP_STATE_DONE                  = 0x00,
    HTTP_STATE_ERROR                 = 0x01,
    //
    HTTP_STATE_READ                  = 0x10,
    //
    HTTP_STATE_READ_REQ_METHOD       = 0x11,
    HTTP_STATE_READ_REQ_PATH         = 0x12,
    HTTP_STATE_READ_REQ_QUERY        = 0x13,
    HTTP_STATE_READ_REQ_VERSION      = 0x14,
    //
    HTTP_STATE_READ_RESP_VERSION     = 0x15,
    HTTP_STATE_READ_RESP_STATUS      = 0x16,
    HTTP_STATE_READ_RESP_STATUS_DESC = 0x17,
    //
    HTTP_STATE_READ_HEADER           = 0x18,
    //
    HTTP_STATE_READ_BODY             = 0x19,
    //
    HTTP_STATE_WRITE                 = 0x20,
    //
    HTTP_STATE_READ_NL               = 0x80,
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
};

int http_getc(struct http_request *request);
int http_peek(struct http_request *request);

const char *http_get_query_arg(struct http_request *request, const char *name);

void http_write_header(struct http_request *request, const char *name, const char *value);
void http_end_header(struct http_request *request);

int http_open(struct http_request *request);
int http_close(struct http_request *request);

int http_get_request(struct http_request *request);

#endif
