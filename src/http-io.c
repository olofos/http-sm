#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "http.h"
#include "http-private.h"
#include "log.h"

static int read_chunk_header(struct http_request *request)
{
    char c;
    int ret;
    for(;;) {
        ret = read(request->fd, &c, 1);
        if(ret < 0) {
            perror("read");
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
            perror("read");
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
        perror("read");
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

    if(request->flags & HTTP_FLAG_CHUNKED) {
        int ret;
        unsigned char c;

        if(request->chunk_length == 0) {
           if((ret = read_chunk_header(request)) < 0) {
                return ret;
            }

            if(request->chunk_length == 0) {
                read_chunk_footer(request);
                request->state = HTTP_STATE_IDLE;
                return 0;
            }
        }

        ret = read(request->fd, &c, 1);
        if(ret < 0) {
            perror("read");
            return -1;
        } else if(ret == 0) {
            LOG("Got EOF");
            return 0;
        }

        request->chunk_length--;

        if(request->chunk_length == 0) {
            if((ret = read_chunk_footer(request)) <= 0) {
                return ret;
            }
        }
        return c;
    } else {
        if(request->content_length > 0) {
            unsigned char c;
            int ret = read(request->fd, &c, 1);
            if(ret < 0) {
                perror("read");
                return -1;
            } else if(ret == 0) {
                LOG("Got EOF");
                return 0;
            }
            request->content_length--;
            return c;
        } else {
            return 0;
        }
    }
}

int http_peek(struct http_request *request)
{
    if(request->poke < 0) {
        request->poke = http_getc(request);
    }

    return request->poke;
}

static int write_all(int fd, const char *str, int len)
{
    int num = 0;
    while(num < len) {
        int n = write(fd, str, len);
        if(n < 0) {
            perror("write");
            return -1;
        }
        str += n;
        num += n;
    }
    return num;
}

int http_write_string(struct http_request *request, const char *str)
{
    int len = strlen(str);

    if(request->flags & HTTP_FLAG_CHUNKED) {
        char buf[16];
        int n = snprintf(buf, sizeof(buf), "%X\r\n", len);
        if(write_all(request->fd, buf, n) < 0) {
            return -1;
        }
    }

    int num = write_all(request->fd, str, len);

    if(request->flags & HTTP_FLAG_CHUNKED) {
        write_all(request->fd, "\r\n", 2);
    }

    return num;
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
            perror("malloc");
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
        if(request->content_length < 0) {
            http_write_header(request, "Transfer-Encoding", "chunked");
            request->flags |= HTTP_FLAG_CHUNKED;
        }
    }

    write_all(request->fd, "\r\n", 2);
}

void http_set_content_length(struct http_request *request, int length)
{
    request->content_length = length;

    char buf[12];
    snprintf(buf, sizeof(buf), "%d", length);

    http_write_header(request, "Content-Length", buf);
}
