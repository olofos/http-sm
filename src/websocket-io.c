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

void websocket_read_frame_header(struct websocket_connection *conn)
{
    read(conn->fd, &conn->frame_opcode, 1);

    uint8_t mask_len;
    read(conn->fd, &mask_len, 1);

    conn->frame_length = mask_len & WEBSOCKET_FRAME_LEN;

    if(conn->frame_length == WEBSOCKET_FRAME_LEN_16BIT) {
        uint8_t hi;
        uint8_t lo;
        read(conn->fd, &hi, 1);
        read(conn->fd, &lo, 1);
        conn->frame_length = (((uint16_t)hi << 8)) | lo;
    } else if(conn->frame_length == WEBSOCKET_FRAME_LEN_64BIT) {
        conn->frame_length = 0;
        for(int i = 0; i < 8; i++) {
            uint8_t c;
            read(conn->fd, &c, 1);
            conn->frame_length = (conn->frame_length << 8) | c;
        }
    }

    if(mask_len & WEBSOCKET_FRAME_MASK) {
        read(conn->fd, conn->frame_mask, 4);
    } else {
        conn->frame_mask[0] = 0;
        conn->frame_mask[1] = 0;
        conn->frame_mask[2] = 0;
        conn->frame_mask[3] = 0;
    }

    conn->frame_index = 0;
}

int websocket_read(struct websocket_connection *conn, void *buf_, size_t count)
{
    char *buf = buf_;

    if(count > conn->frame_length - conn->frame_index) {
        count = conn->frame_length - conn->frame_index;
    }

    int n = http_read_all(conn->fd, buf, count);

    for(int i = 0; i < n; i++) {
        buf[i] ^= conn->frame_mask[(conn->frame_index++) % 4];
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