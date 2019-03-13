#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "http-sm/http.h"
#include "http-sm/http-sha1.h"
#include "http-private.h"
#include "log.h"

static int read_chunk_header(struct http_request *request)
{
    char c;
    int ret;
    for(;;) {
        ret = read(request->fd, &c, 1);
        if(ret < 0) {
            ERROR("Read failed in chunk header");
            return -1;
        } else if(ret == 0) {
            LOG("Got EOF");
            return 0;
        }

        if(c == ';' || c == '\r') {
            break;
        }

        request->chunk_length = (request->chunk_length << 4) | http_hex_to_int(c);
    }

    for(;;) {
        ret = read(request->fd, &c, 1);
        if(ret < 0) {
            ERROR("Read failed before newline");
            return -1;
        } else if(ret == 0) {
            LOG("Got EOF");
            return 0;
        }
        if(c == '\n') {
            break;
        }
    }
    return request->chunk_length;
}

static int read_chunk_footer(struct http_request *request)
{
    char buf[2];
    int ret = read(request->fd, buf, 2);
    if(ret < 0) {
        ERROR("Read failed while reading chunk footer");
        return -1;
    } else if(ret == 0) {
        LOG("Got EOF");
        return 0;
    }

    if(buf[0] != '\r' || buf[1] != '\n') {
        return -1;
    }
    return 1;
}

int http_read(struct http_request *request, void *buf_, size_t count)
{
    uint8_t *buf = buf_;

    size_t num = 0;

    if(request->flags & HTTP_FLAG_READ_CHUNKED) {
        while(count > 0) {
            if(request->chunk_length == 0) {
                int ret = read_chunk_header(request);

                if(ret < 0) {
                    return ret;
                }

                if(request->chunk_length == 0) {
                    read_chunk_footer(request);
                    request->state = HTTP_STATE_IDLE | (request->state & HTTP_STATE_CLIENT);
                    break;
                }
            }

            int num_to_read = (count < request->chunk_length) ? count : request->chunk_length;
            int n = read(request->fd, buf, num_to_read);

            if(n < 0) {
                ERROR("Read failed in body (chunked)");
                return -1;
            } else if(n == 0) {
                break;
            } else {
                num += n;
                count -= n;
                buf += n;
                request->chunk_length -= n;

                if(request->chunk_length == 0) {
                    int ret = read_chunk_footer(request);
                    if(ret < 0) {
                        return ret;
                    }
                }
            }
        }
    } else if(request->read_content_length == 0) {
        request->state = HTTP_STATE_IDLE | (request->state & HTTP_STATE_CLIENT);
    } else {
        if((count > 0) && (request->read_content_length > 0)) {
            int num_to_read = (count < request->read_content_length) ? count : request->read_content_length;
            int n = http_read_all(request->fd, buf, num_to_read);

            if(n < 0) {
                ERROR("Read failed in reading body");
                return -1;
            } else if(n == 0) {
                request->state = HTTP_STATE_IDLE | (request->state & HTTP_STATE_CLIENT);
                return 0;
            } else {
                request->read_content_length -= n;
                return n;
            }
        }
    }

    return num;
}

int http_getc(struct http_request *request)
{
    if(request->state != HTTP_STATE_SERVER_READ_BODY && request->state != HTTP_STATE_CLIENT_READ_BODY) {
        return 0;
    }

    if(request->poke >= 0) {
        int c = request->poke;
        request->poke = -1;
        return c;
    }

    unsigned char c;
    int n = http_read(request, &c, 1);
    if(n < 0) {
        return -1;
    } else if(n == 0) {
        return 0;
    } else {
        return c;
    }
}

int http_peek(struct http_request *request)
{
    if(request->poke < 0) {
        request->poke = http_getc(request);
    }

    return request->poke;
}

int http_read_all(int fd, void *buf_, size_t count)
{
    char *buf = buf_;
    size_t num = 0;
    while(num < count) {
        size_t to_read = count - num;
        ssize_t ret = read(fd, buf, to_read);
        if(ret < 0) {
            return -1;
        } else if(ret == 0) {
            break;
        }
        num += ret;
        buf += ret;
    }
    return num;
}

int http_write_all(int fd, const char *str, int len)
{
    int num = 0;
    while(num < len) {
        int n = write(fd, str, len);
        if(n < 0) {
            return -1;
        }
        str += n;
        num += n;
    }
    return num;
}

static int write_chunk(int fd, const char *data, int len)
{
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%X\r\n", len);

    if(http_write_all(fd, buf, n) < 0) {
        return -1;
    }

    int num = http_write_all(fd, data, len);

    http_write_all(fd, "\r\n", 2);

    return num;
}

int http_write_bytes(struct http_request *request, const char *data, int len)
{
    if(len > 0) {
        if(request->flags & HTTP_FLAG_WRITE_CHUNKED) {
            if(len < (request->line_length - request->chunk_length)) {
                memcpy(request->line + request->chunk_length, data, len);
                request->chunk_length += len;
                return len;
            } else {
                if(request->chunk_length > 0) {
                    write_chunk(request->fd, request->line, request->chunk_length);
                }

                if(len < request->line_length) {
                    memcpy(request->line, data, len);
                    request->chunk_length = len;
                    return len;
                } else {
                    int ret = write_chunk(request->fd, data, len);
                    request->chunk_length = 0;
                    return ret;
                }
            }
        } else {
            return http_write_all(request->fd, data, len);
        }
    }
    return 0;
}

int http_write_string(struct http_request *request, const char *str)
{
    return http_write_bytes(request, str, strlen(str));
}

void http_write_header(struct http_request *request, const char *name, const char *value)
{
    if(name && value) {
        http_write_string(request, name);
        http_write_string(request, ": ");
        http_write_string(request, value);
        http_write_string(request, "\r\n");
    }
}

int http_begin_request(struct http_request *request)
{
    if(request->method == HTTP_METHOD_POST) {
        http_write_string(request, "POST ");
    } else {
        http_write_string(request, "GET ");
    }
    http_write_string(request, request->path);
    if(request->query && request->query[0]) {
        http_write_string(request, "?");
        http_write_string(request, request->query);
    }
    http_write_string(request, " HTTP/1.1\r\n");

    if(request->port == 80) {
        http_write_header(request, "Host", request->host);
    } else {
        int len = strlen(request->host) + strlen(":65535");
        char *buf = malloc(len + 1);

        if(!buf) {
            ERROR("Malloc failed while allocation buf");
            return 0;
        }

        snprintf(buf, len, "%s:%d", request->host, request->port);
        http_write_header(request, "Host", buf);
        free(buf);
    }

    http_write_header(request, "User-Agent", HTTP_USER_AGENT);

    return 1;
}

void http_end_header(struct http_request *request)
{
    if(http_is_server(request)) {
        if(request->write_content_length < 0) {
            http_write_header(request, "Transfer-Encoding", "chunked");
            request->flags |= HTTP_FLAG_WRITE_CHUNKED;

            request->line = malloc(HTTP_LINE_LEN);
            request->line_length = HTTP_LINE_LEN;
            request->chunk_length = 0;
        }

        request->state = HTTP_STATE_SERVER_WRITE_BODY;
    }

    http_write_all(request->fd, "\r\n", 2);
}

int http_end_body(struct http_request *request)
{
    if(request->flags & HTTP_FLAG_WRITE_CHUNKED) {

        if(request->chunk_length > 0) {
            write_chunk(request->fd, request->line, request->chunk_length);
        }

        const char *final_chunk = "0\r\n\r\n";
        http_write_all(request->fd, final_chunk, strlen(final_chunk));
        free(request->line);
    }
    return 0;
}


void http_set_content_length(struct http_request *request, int length)
{
    request->write_content_length = length;

    if(length > 0) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%d", length);

        http_write_header(request, "Content-Length", buf);
    }
}

void http_ws_send_response(struct http_request *request)
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

void http_ws_send_error_response(struct http_request *request)
{
    const char *response = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
    http_write_all(request->fd, response, strlen(response));
}

void http_ws_read_frame_header(struct http_ws_connection *conn)
{
    read(conn->fd, &conn->frame_opcode, 1);

    uint8_t mask_len;
    read(conn->fd, &mask_len, 1);

    conn->frame_length = mask_len & HTTP_WS_FRAME_LEN;

    if(conn->frame_length == HTTP_WS_FRAME_LEN_16BIT) {
        uint8_t hi;
        uint8_t lo;
        read(conn->fd, &hi, 1);
        read(conn->fd, &lo, 1);
        conn->frame_length = (((uint16_t)hi << 8)) | lo;
    } else if(conn->frame_length == HTTP_WS_FRAME_LEN_64BIT) {
        conn->frame_length = 0;
        for(int i = 0; i < 8; i++) {
            uint8_t c;
            read(conn->fd, &c, 1);
            conn->frame_length = (conn->frame_length << 8) | c;
        }
    }

    if(mask_len & HTTP_WS_FRAME_MASK) {
        read(conn->fd, conn->frame_mask, 4);
    } else {
        conn->frame_mask[0] = 0;
        conn->frame_mask[1] = 0;
        conn->frame_mask[2] = 0;
        conn->frame_mask[3] = 0;
    }

    conn->frame_index = 0;
}

int http_ws_read(struct http_ws_connection *conn, void *buf_, size_t count)
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

int http_ws_send(struct http_ws_connection *conn, const void *buf, size_t count, enum http_ws_frame_opcode opcode)
{
    uint8_t op = opcode | HTTP_WS_FRAME_FIN;
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

    return http_write_all(conn->fd, buf, count);
}
