#ifndef HTTP_SM_WEBSOCKET_H_
#define HTTP_SM_WEBSOCKET_H_

enum http_ws_frame_bits
{
    HTTP_WS_FRAME_FIN  = 0x80,
    HTTP_WS_FRAME_MASK = 0x80,
    HTTP_WS_FRAME_LEN  = 0x7F,
    HTTP_WS_FRAME_LEN_16BIT  = 0x7E,
    HTTP_WS_FRAME_LEN_64BIT  = 0x7F,
    HTTP_WS_FRAME_OPCODE = 0x0F,
};

enum http_ws_frame_opcode
{
    HTTP_WS_FRAME_OPCODE_CONT = 0x00,
    HTTP_WS_FRAME_OPCODE_TEXT = 0x01,
    HTTP_WS_FRAME_OPCODE_BIN  = 0x02,

    HTTP_WS_FRAME_OPCODE_CLOSE = 0x08,
    HTTP_WS_FRAME_OPCODE_PING  = 0x09,
    HTTP_WS_FRAME_OPCODE_PONG  = 0x0a,
};

struct http_ws_url_handler;

struct http_ws_connection {
    int fd;

    uint64_t frame_length;
    uint64_t frame_index;
    uint8_t frame_opcode;
    uint8_t frame_mask[4];

    struct http_ws_url_handler *handler;
};

typedef int (*http_ws_url_handler_func_open)(struct http_ws_connection*, struct http_request*);
typedef void (*http_ws_url_handler_func_close)(struct http_ws_connection*);
typedef void (*http_ws_url_handler_func_message)(struct http_ws_connection*);

struct http_ws_url_handler {
    const char *url;
    http_ws_url_handler_func_open open;
    http_ws_url_handler_func_close close;
    http_ws_url_handler_func_message message;
    void *data;
};

extern struct http_ws_url_handler http_ws_url_tab[];

int http_ws_read(struct http_ws_connection *conn, void *buf_, size_t count);
int http_ws_send(struct http_ws_connection *conn, const void *buf_, size_t count, enum http_ws_frame_opcode opcode);

#endif
