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
            request->status = HTTP_STATUS_BAD_REQUEST;
            request->state = HTTP_STATE_ERROR;
        }

        return;
    }

    switch(request->state) {
    case HTTP_STATE_READ_METHOD:
        if(c == ' ') {
            request->line[request->line_index] = 0;

            if(strcmp(request->line, "GET") == 0) {
                request->method = HTTP_METHOD_GET;
                http_parse_header_next_state(request, HTTP_STATE_READ_PATH);
            } else if(strcmp(request->line, "POST") == 0) {
                request->method = HTTP_METHOD_POST;
                http_parse_header_next_state(request, HTTP_STATE_READ_PATH);
            } else {
                printf("Unsupported HTTP method \"%s\"\n", request->line);
                request->method = HTTP_METHOD_UNSUPPORTED;
                request->status = HTTP_STATUS_METHOD_NOT_ALLOWED;
                http_parse_header_next_state(request, HTTP_STATE_ERROR);
            }
            return;
        }
        break;

    case HTTP_STATE_READ_PATH:
        if(c == ' ' || c == '?') {
            request->line[request->line_index] = 0;
            request->path = malloc(strlen(request->line)+1);
            strcpy(request->path, request->line);

            if(c == '?') {
                http_parse_header_next_state(request, HTTP_STATE_READ_QUERY);
            } else {
                http_parse_header_next_state(request, HTTP_STATE_READ_VERSION);
            }
            return;
        }
        break;

    case HTTP_STATE_READ_QUERY:
        if(c == ' ') {
            request->line[request->line_index] = 0;
            request->query = malloc(strlen(request->line)+1);
            strcpy(request->query, request->line);

            http_parse_header_next_state(request, HTTP_STATE_READ_VERSION);

            return;
        }
        break;

    case HTTP_STATE_READ_VERSION:
        if(c == '\r') {
            request->line[request->line_index] = 0;
            if(strcmp(request->line, "HTTP/1.1") == 0) {
                http_parse_header_next_state(request, HTTP_STATE_READ_HEADER | HTTP_STATE_READ_NL);
            } else if(strcmp(request->line, "HTTP/1.0") == 0) {
                printf("HTTP/1.0 not supported\n");
                request->status = HTTP_STATUS_VERSION_NOT_SUPPORTED;
                http_parse_header_next_state(request, HTTP_STATE_ERROR);
            } else {
                printf("Unsupported HTTP version \"%s\"\n", request->line);
                request->status = HTTP_STATUS_BAD_REQUEST;
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
                }

                http_parse_header_next_state(request, HTTP_STATE_READ_HEADER | HTTP_STATE_READ_NL);
            }

            return;
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
