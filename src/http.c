#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "http.h"

static void http_parse_header_next_state(struct http_request *request, int state)
{
    request->state = state;
    request->line_index = 0;
}

void http_parse_header(struct http_request *request, char c)
{
    printf("state = 0x%02X\n", request->state);
    if(request->state & HTTP_STATE_READ_NL) {
        if(c == '\n') {
            request->state &= ~HTTP_STATE_READ_NL;
        } else {
            printf("Expected '\\n' but got '%c'", c);
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
        } else {
            if(request->line_index < request->line_len - 1) {
                request->line[request->line_index++] = c;
            }
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
        } else {
            if(request->line_index < request->line_len - 1) {
                request->line[request->line_index++] = c;
            }
        }
        break;

    case HTTP_STATE_READ_QUERY:
        if(c == ' ') {
            request->line[request->line_index] = 0;
            request->query = malloc(strlen(request->line)+1);
            strcpy(request->query, request->line);

            http_parse_header_next_state(request, HTTP_STATE_READ_VERSION);
        } else {
            if(request->line_index < request->line_len - 1) {
                request->line[request->line_index++] = c;
            }
        }
        break;

    case HTTP_STATE_READ_VERSION:
        if(c == '\r') {
            request->line[request->line_index] = 0;
            if(strcmp(request->line, "HTTP/1.1") == 0) {
                http_parse_header_next_state(request, HTTP_STATE_READ_HEADER | HTTP_STATE_READ_NL);
            } else if(strcmp(request->line, "HTTP/1.0") == 0) {
                printf("HTTP/1.0 not supported");
                request->status = HTTP_STATUS_VERSION_NOT_SUPPORTED;
                http_parse_header_next_state(request, HTTP_STATE_ERROR);
            } else {
                printf("Unsupported HTTP version \"%s\"\n", request->line);
                request->status = HTTP_STATUS_BAD_REQUEST;
                http_parse_header_next_state(request, HTTP_STATE_ERROR);
            }
        } else {
            if(request->line_index < request->line_len - 1) {
                request->line[request->line_index++] = c;
            }
        }
        break;

    default:
        break;
    }
}
