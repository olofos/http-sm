#include <string.h>
#include <unistd.h>

#include "http-sm/http.h"
#include "http-sm/sha1.h"
#include "http-private.h"
#include "log.h"

void websocket_send_response(struct http_request *request)
{
    const char *response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n";
    http_write_all(request->fd, response, strlen(response));

    if(request->websocket_key) {
        const char *guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

        struct http_sha1_ctx ctx;
        uint8_t hash[21];
        http_sha1_init(&ctx);
        http_sha1_update(&ctx, (const uint8_t*)request->websocket_key, strlen(request->websocket_key));
        http_sha1_update(&ctx, (const uint8_t*)guid, strlen(guid));
        http_sha1_final(hash, &ctx);
        char hash_base64[29];
        http_base64_encode(hash_base64, (char *)hash, 20);
        hash_base64[28] = 0;

        const char *header = "Sec-WebSocket-Accept: ";
        http_write_all(request->fd, header, strlen(header));
        http_write_all(request->fd, hash_base64, strlen(hash_base64));
        http_write_all(request->fd, "\r\n", 2);
    }

    http_write_all(request->fd, "\r\n", 2);
}

#define websocket_next_state(conn,newstate) ((conn)->state = (newstate) | ((conn)->state & WEBSOCKET_STATE_MASK))

void websocket_parse_frame_header(struct websocket_connection *conn, uint8_t c)
{
    switch(conn->state & ~WEBSOCKET_STATE_MASK) {
    case WEBSOCKET_STATE_OPCODE:
        conn->frame_index = 0;
        if((c & ~(WEBSOCKET_FRAME_OPCODE | WEBSOCKET_FRAME_MASK)) || ((c & 0x07) > 2)) {
            LOG("Unkown websocket opcode: %02X", c);
            conn->state = WEBSOCKET_STATE_ERROR;
        } else {
            conn->frame_opcode = c;
            conn->state = WEBSOCKET_STATE_LEN8;
        }
        break;

    case WEBSOCKET_STATE_LEN8:
    {
        if(c & WEBSOCKET_FRAME_MASK) {
            conn->state |= WEBSOCKET_STATE_MASK;
        }
        conn->frame_length = 0;

        c &= ~WEBSOCKET_FRAME_MASK;

        if(c == WEBSOCKET_FRAME_LEN_16BIT) {
            websocket_next_state(conn, WEBSOCKET_STATE_LEN16_0);
            break;
        } else if(c == WEBSOCKET_FRAME_LEN_64BIT) {
            websocket_next_state(conn, WEBSOCKET_STATE_LEN64_0);
            break;
        }
        // Intentional fall through to read 8 bit length
    }

    case WEBSOCKET_STATE_LEN16_1:
    case WEBSOCKET_STATE_LEN64_7:
    {
        conn->frame_length |= c;
        if(conn->state & WEBSOCKET_STATE_MASK) {
            conn->state = WEBSOCKET_STATE_MASK_0;
        } else {
            memset(conn->frame_mask, 0, 4);
            conn->state = WEBSOCKET_STATE_BODY;
        }
        break;
    }

    case WEBSOCKET_STATE_LEN16_0:
    case WEBSOCKET_STATE_LEN64_0:
    case WEBSOCKET_STATE_LEN64_1:
    case WEBSOCKET_STATE_LEN64_2:
    case WEBSOCKET_STATE_LEN64_3:
    case WEBSOCKET_STATE_LEN64_4:
    case WEBSOCKET_STATE_LEN64_5:
    case WEBSOCKET_STATE_LEN64_6:
    {
        conn->frame_length = (conn->frame_length | c) << 8;
        conn->state++;
        break;
    }


    case WEBSOCKET_STATE_MASK_0:
    {
        conn->frame_mask[0] = c;
        conn->state++;
        break;
    }

    case WEBSOCKET_STATE_MASK_1:
    {
        conn->frame_mask[1] = c;
        conn->state++;
        break;
    }

    case WEBSOCKET_STATE_MASK_2:
    {
        conn->frame_mask[2] = c;
        conn->state++;
        break;
    }

    case WEBSOCKET_STATE_MASK_3:
    {
        conn->frame_mask[3] = c;
        conn->state = WEBSOCKET_STATE_BODY;
        break;
    }

    case WEBSOCKET_STATE_BODY:
    case WEBSOCKET_STATE_DONE:
    case WEBSOCKET_STATE_ERROR:
        LOG("Unexpected call to websocket_parse_frame_header in state %02F", conn->state);
        break;
    }
}

int websocket_read(struct websocket_connection *conn, void *buf_, size_t count)
{
    if(conn->state != WEBSOCKET_STATE_BODY) {
        return -1;
    }

    char *buf = buf_;

    if(count > conn->frame_length - conn->frame_index) {
        count = conn->frame_length - conn->frame_index;
    }

    int n = http_read_all(conn->fd, buf, count);

    for(int i = 0; i < n; i++) {
        buf[i] ^= conn->frame_mask[(conn->frame_index++) % 4];
    }

    if(conn->frame_length == conn->frame_index) {
        conn->state = WEBSOCKET_STATE_DONE;
    }

    return n;
}

int websocket_send(struct websocket_connection *conn, const void *buf, size_t count, enum websocket_frame_opcode opcode)
{
    uint8_t op = opcode | WEBSOCKET_FRAME_FIN;
    write(conn->fd, &op, 1);

    if(count < 0x7e) {
        uint8_t c = count;
        write(conn->fd, &c, sizeof(c));
    } else if(count < 0x10000) {
        uint8_t c[] = { 0x7e, (count >> 8) & 0xFF, count & 0xFF };
        write(conn->fd, &c, sizeof(c));
    } else {
        uint8_t c[] = { 0x7f, (count >> 56) & 0xFF, (count >> 48) & 0xFF, (count >> 40) & 0xFF, (count >> 32) & 0xFF, (count >> 24) & 0xFF, (count >> 16) & 0xFF, (count >> 8) & 0xFF, count & 0xFF };
        write(conn->fd, &c, sizeof(c));
    }

    int ret = http_write_all(conn->fd, buf, count);

    websocket_flush(conn);

    return ret;
}
