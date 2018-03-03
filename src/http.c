#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "http.h"

static char *cmp_str_prefix(char *str, const char *prefix)
{
    int len = strlen(prefix);
    if(strncmp(str, prefix, len) == 0) {
        return str + len;
    } else {
        return 0;
    }
}

static void http_parse_header_next_state(struct http_request *request, int state)
{
    request->state = state;
    request->line_index = 0;
}

void http_parse_header(struct http_request *request, char c)
{
    if(request->state & HTTP_STATE_READ_NL) {
        if(c == '\n') {
            request->state &= ~HTTP_STATE_READ_NL;
        } else {
            printf("Expected '\\n' but got '%c'\n", c);
            request->error = HTTP_STATUS_BAD_REQUEST;
            request->state = HTTP_STATE_ERROR;
        }

        return;
    }

    switch(request->state) {
    case HTTP_STATE_READ_REQ_METHOD:
        if(c == ' ') {
            request->line[request->line_index] = 0;

            if(strcmp(request->line, "GET") == 0) {
                request->method = HTTP_METHOD_GET;
                http_parse_header_next_state(request, HTTP_STATE_READ_REQ_PATH);
            } else if(strcmp(request->line, "POST") == 0) {
                request->method = HTTP_METHOD_POST;
                http_parse_header_next_state(request, HTTP_STATE_READ_REQ_PATH);
            } else {
                printf("Unsupported HTTP method \"%s\"\n", request->line);
                request->method = HTTP_METHOD_UNSUPPORTED;
                request->error = HTTP_STATUS_METHOD_NOT_ALLOWED;
                http_parse_header_next_state(request, HTTP_STATE_ERROR);
            }
            return;
        }
        break;

    case HTTP_STATE_READ_REQ_PATH:
        if(c == ' ' || c == '?') {
            request->line[request->line_index] = 0;
            request->path = malloc(strlen(request->line)+1);
            strcpy(request->path, request->line);

            if(c == '?') {
                http_parse_header_next_state(request, HTTP_STATE_READ_REQ_QUERY);
            } else {
                http_parse_header_next_state(request, HTTP_STATE_READ_REQ_VERSION);
            }
            return;
        }
        break;

    case HTTP_STATE_READ_REQ_QUERY:
        if(c == ' ') {
            request->line[request->line_index] = 0;
            request->query = malloc(strlen(request->line)+1);
            strcpy(request->query, request->line);

            http_parse_header_next_state(request, HTTP_STATE_READ_REQ_VERSION);

            return;
        }
        break;

    case HTTP_STATE_READ_REQ_VERSION:
        if(c == '\r') {
            request->line[request->line_index] = 0;
            if(strcmp(request->line, "HTTP/1.1") == 0) {
                http_parse_header_next_state(request, HTTP_STATE_READ_HEADER | HTTP_STATE_READ_NL);
            } else if(strcmp(request->line, "HTTP/1.0") == 0) {
                printf("HTTP/1.0 not supported\n");
                request->error = HTTP_STATUS_VERSION_NOT_SUPPORTED;
                http_parse_header_next_state(request, HTTP_STATE_ERROR);
            } else {
                printf("Unsupported HTTP version \"%s\"\n", request->line);
                request->error = HTTP_STATUS_BAD_REQUEST;
                http_parse_header_next_state(request, HTTP_STATE_ERROR);
            }

            return;
        }
        break;

    case HTTP_STATE_READ_HEADER:
        if(c == '\r') {
            request->line[request->line_index] = 0;
            printf("line = '%s'\n", request->line);

            if(request->line_index == 0) {
                http_parse_header_next_state(request, HTTP_STATE_DONE | HTTP_STATE_READ_NL);
            } else {

                char *val;

                if((val = cmp_str_prefix(request->line, "Host: ")) != 0) {
                    request->host = malloc(strlen(val) + 1);
                    strcpy(request->host, val);
                } else if((val = cmp_str_prefix(request->line, "Accept-Encoding: ")) != 0) {
                    if(strstr(val, "gzip") != 0) {
                        request->flags |= HTTP_FLAG_ACCEPT_GZIP;
                    }
                } else if((val = cmp_str_prefix(request->line, "Transfer-Encoding: ")) != 0) {
                    if(strstr(val, "chunked") != 0) {
                        request->flags |= HTTP_FLAG_CHUNKED;
                    }
                } else if((val = cmp_str_prefix(request->line, "Content-Length: ")) != 0) {
                    char *p;
                    request->content_length = strtol(val, &p, 10);
                    if(!p || *p) {
                        http_parse_header_next_state(request, HTTP_STATE_ERROR);
                        request->error = HTTP_STATUS_BAD_REQUEST;
                        printf("Error parsing content length \"%s\"\n", val);
                        return;
                    }
                }

                http_parse_header_next_state(request, HTTP_STATE_READ_HEADER | HTTP_STATE_READ_NL);
            }

            return;
        }
        break;

    case HTTP_STATE_READ_RESP_VERSION:
        if(c == ' ') {
            request->line[request->line_index] = 0;
            if(strcmp(request->line, "HTTP/1.1") != 0) {
                printf("Unexpected HTTP version \"%s\"\n", request->line);
            }

            http_parse_header_next_state(request, HTTP_STATE_READ_RESP_STATUS);

            return;
        }
        break;

    case HTTP_STATE_READ_RESP_STATUS:
        if(c == ' ') {
            char *p;
            request->line[request->line_index] = 0;
            request->status = strtol(request->line, &p, 10);
            if(!p || *p) {
                printf("Error reading response code \"%s\" (%s)\n", request->line, p);
            }

            http_parse_header_next_state(request, HTTP_STATE_READ_RESP_STATUS_DESC);

            return;
        }
        break;

    case HTTP_STATE_READ_RESP_STATUS_DESC:
        if(c == '\r') {
            request->line[request->line_index] = 0;
            printf("Status %d: %s\n", request->status, request->line);
            http_parse_header_next_state(request, HTTP_STATE_READ_HEADER | HTTP_STATE_READ_NL);
        }
        break;

    case HTTP_STATE_ERROR:
        return;

    default:
        printf("Unhandled state 0x%02X\n", request->state);
        break;
    }

    if(request->line_index < request->line_len - 1) {
        request->line[request->line_index++] = c;
    }
}
