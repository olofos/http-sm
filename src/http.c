#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "http.h"
#include "http-private.h"
#include "log.h"

int http_get_request(struct http_request *request)
{
    int err;

    err = http_open(request);
    if(err < 0) {
        LOG("http_open failed");
        return err;
    }

    LOG("http_open");

    err = http_begin_request(request);
    if(err < 0) {
        LOG("http_begin_request failed");
        return err;
    }

    LOG("http_begin_request");

    http_end_header(request);

    LOG("http_end_header");


    request->poke = -1;
    request->content_length = -1;

    const int line_len = 64;
    request->line = malloc(line_len);
    request->line_len = line_len;

    request->state = HTTP_STATE_READ_RESP_VERSION;

    while(request->state != HTTP_STATE_ERROR && request->state != HTTP_STATE_DONE) {
        int c;
        int ret = read(request->fd, &c, 1);

        if(ret < 0) {
            return -1;
        } else if(ret == 0) {
            return -1;
        } else {
            http_parse_header(request, c);
        }
    }

    request->state = HTTP_STATE_READ_BODY;

    return 1;
}
