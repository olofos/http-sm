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
    if(request->state != HTTP_STATE_READ_BODY) {
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
                request->state = HTTP_STATE_DONE;
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

static void write_string(int fd, const char *str)
{
    int len = strlen(str);
    while(len > 0) {
        int n = write(fd, str, len);
        if(n < 0) {
            perror("write");
            return;
        }
        str += n;
        len -= n;
    }
}

void http_write_header(struct http_request *request, const char *name, const char *value)
{
    if(name && value) {
        write_string(request->fd, name);
        write_string(request->fd, ": ");
        write_string(request->fd, value);
        write_string(request->fd, "\r\n");
    }
}

int http_begin_request(struct http_request *request)
{
    if(request->method == HTTP_METHOD_POST) {
        write_string(request->fd, "POST ");
    } else {
        write_string(request->fd, "GET ");
    }
    write_string(request->fd, request->path);
    if(request->query && request->query[0]) {
        write_string(request->fd, "?");
        write_string(request->fd, request->query);
    }
    write_string(request->fd, " HTTP/1.1\r\n");

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
    write_string(request->fd, "\r\n");
}

void http_set_content_length(struct http_request *request, int length)
{
    request->content_length = length;

    char buf[12];
    snprintf(buf, sizeof(buf), "%d", length);

    http_write_header(request, "Content-Length", buf);
}
