#ifndef HTTP_SM_WEBSOCKET_H_
#define HTTP_SM_WEBSOCKET_H_

enum websocket_frame_bits
{
    WEBSOCKET_FRAME_FIN  = 0x80,
    WEBSOCKET_FRAME_MASK = 0x80,
    WEBSOCKET_FRAME_LEN  = 0x7F,
    WEBSOCKET_FRAME_LEN_16BIT  = 0x7E,
    WEBSOCKET_FRAME_LEN_64BIT  = 0x7F,
    WEBSOCKET_FRAME_OPCODE = 0x0F,
};

enum websocket_frame_opcode
{
    WEBSOCKET_FRAME_OPCODE_CONT = 0x00,
    WEBSOCKET_FRAME_OPCODE_TEXT = 0x01,
    WEBSOCKET_FRAME_OPCODE_BIN  = 0x02,

    WEBSOCKET_FRAME_OPCODE_CLOSE = 0x08,
    WEBSOCKET_FRAME_OPCODE_PING  = 0x09,
    WEBSOCKET_FRAME_OPCODE_PONG  = 0x0a,
};

struct websocket_url_handler;

struct websocket_connection {
    int fd;

    uint64_t frame_length;
    uint64_t frame_index;
    uint8_t frame_opcode;
    uint8_t frame_mask[4];

    struct websocket_url_handler *handler;
};

typedef int (*websocket_url_handler_func_open)(struct websocket_connection*, struct http_request*);
typedef void (*websocket_url_handler_func_close)(struct websocket_connection*);
typedef void (*websocket_url_handler_func_message)(struct websocket_connection*);

struct websocket_url_handler {
    const char *url;
    websocket_url_handler_func_open open;
    websocket_url_handler_func_close close;
    websocket_url_handler_func_message message;
    void *data;
};

extern struct websocket_url_handler websocket_url_tab[];

int websocket_read(struct websocket_connection *conn, void *buf_, size_t count);
int websocket_send(struct websocket_connection *conn, const void *buf_, size_t count, enum websocket_frame_opcode opcode);

#endif
