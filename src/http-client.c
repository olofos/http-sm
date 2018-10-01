#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "http-sm/http.h"
#include "http-private.h"
#include "log.h"

int http_get_request(struct http_request *request)
{
    int err;

    err = http_open_request_socket(request);
    if(err < 0) {
        ERROR("http_open_request_socket failed");
        request->state = HTTP_STATE_CLIENT_ERROR;
        return err;
    }

    err = http_begin_request(request);
    if(err < 0) {
        ERROR("http_begin_request failed");
        request->state = HTTP_STATE_CLIENT_ERROR;
        http_close(request);
        return err;
    }

    http_end_header(request);

    const int line_len = HTTP_LINE_LEN;
    request->line = malloc(line_len);

    if(!request->line) {
        ERROR("Malloc failed while allocating line");
        request->state = HTTP_STATE_CLIENT_ERROR;
        http_close(request);
        return -1;
    }

    request->line_len = line_len;
    request->state = HTTP_STATE_CLIENT_READ_VERSION;

    while(request->state & (HTTP_STATE_READ | HTTP_STATE_READ_NL)) {
        int c;
        int ret = read(request->fd, &c, 1);

        if(ret <= 0) {
            free(request->line);
            request->line = 0;
            request->line_len = 0;
            request->state = HTTP_STATE_CLIENT_ERROR;
            http_close(request);

            return -1;
        } else {
            http_parse_header(request, c);
        }
    }

    free(request->line);
    request->line = 0;
    request->line_len = 0;

    if(http_is_error(request)) {
        http_close(request);
        return -1;
    }

    request->state = HTTP_STATE_CLIENT_READ_BODY;
    return 1;
}
