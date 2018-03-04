#include <stdio.h>
#include <unistd.h>

#include "http.h"

static int hex_to_int(char c)
{
    if('0' <= c && c <= '9') {
        return c - '0';
    } else if('A' <= c && c <= 'F') {
        return 0xA + c - 'A';
    } else  if('a' <= c && c <= 'f') {
        return 0xA + c - 'a';
    }
    return 0;
}

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

        request->chunk_length = (request->chunk_length << 4) | hex_to_int(c);
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

int http_fgetc(struct http_request *request)
{
    if(request->flags & HTTP_FLAG_CHUNKED) {
        int ret;
        char c;

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
        char c;
        int ret = read(request->fd, &c, 1);
        if(ret < 0) {
            perror("read");
            return -1;
        } else if(ret == 0) {
            printf("Got EOF\n");
            return 0;
        }

        return c;
    }
}
