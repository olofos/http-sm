#include <stdio.h>
#include <unistd.h>

#include "http.h"
#include "http-private.h"

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
            printf("Got EOF\n");
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
            printf("Got EOF\n");
            return 0;
        }
        if(c == '\n') {
            break;
        }
    }
    return 1;
}

static int read_chunk_footer(struct http_request *request)
{
    char buf[2];
    int ret = read(request->fd, buf, 2);
    if(ret < 0) {
        perror("read");
        return -1;
    } else if(ret == 0) {
        printf("Got EOF\n");
        return 0;
    }

    if(buf[0] != '\r' || buf[1] != '\n') {
        return -1;
    }
    return 1;
}

int http_getc(struct http_request *request)
{
    if(request->poke >= 0) {
        int c = request->poke;
        request->poke = -1;
        return c;
    }

    if(request->flags & HTTP_FLAG_CHUNKED) {
        int ret;
        unsigned char c;

        if(request->chunk_length == 0) {
            if((ret = read_chunk_header(request)) <= 0) {
                return ret;
            }
        }

        ret = read(request->fd, &c, 1);
        if(ret < 0) {
            perror("read");
            return -1;
        } else if(ret == 0) {
            printf("Got EOF\n");
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
                printf("Got EOF\n");
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
