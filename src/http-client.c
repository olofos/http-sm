#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "http.h"
#include "http-private.h"
#include "log.h"

int http_get_request(struct http_request *request)
{
    request->flags |= HTTP_FLAG_CLIENT;

    int err;

    err = http_open_request_socket(request);
    if(err < 0) {
        LOG("http_open_request_socket failed");
        request->state = HTTP_STATE_ERROR;
        return err;
    }

    err = http_begin_request(request);
    if(err < 0) {
        LOG("http_begin_request failed");
        request->state = HTTP_STATE_ERROR;
        http_close(request);
        return err;
    }

    http_end_header(request);

    request->poke = -1;
    request->content_length = -1;

    const int line_len = HTTP_LINE_LEN;
    request->line = malloc(line_len);
    request->line_len = line_len;

    request->state = HTTP_STATE_READ_CLIENT_VERSION;

    while(request->state & HTTP_STATE_READ) {
        int c;
        int ret = read(request->fd, &c, 1);

        if(ret <= 0) {
            free(request->line);
            request->line = 0;
            request->line_len = 0;
            request->state = HTTP_STATE_ERROR;
            http_close(request);

            return -1;
        } else {
            http_parse_header(request, c);
        }
    }

    free(request->line);
    request->line = 0;
    request->line_len = 0;

    if(request->state == HTTP_STATE_ERROR) {
        http_close(request);
        return -1;
    }

    request->state = HTTP_STATE_READ_BODY;
    return 1;
}
