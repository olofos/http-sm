#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "http-sm/http.h"
#include "http-private.h"
#include "log.h"

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
    request->state = state | (request->state & HTTP_STATE_CLIENT);
    request->line_index = 0;
}

void http_parse_header(struct http_request *request, char c)
{
    if(request->state & HTTP_STATE_READ_NL) {
        if(c == '\n') {
            request->state &= ~HTTP_STATE_READ_NL;
        } else {
            LOG("Expected '\\n' but got '%c'", c);
            request->error = HTTP_STATUS_BAD_REQUEST;
            http_parse_header_next_state(request, HTTP_STATE_ERROR);
        }

        return;
    }

    switch(request->state) {
    case HTTP_STATE_SERVER_READ_METHOD:
        if(c == ' ') {
            request->line[request->line_index] = 0;

            if(strcmp(request->line, "GET") == 0) {
                request->method = HTTP_METHOD_GET;
                http_parse_header_next_state(request, HTTP_STATE_SERVER_READ_PATH);
            } else if(strcmp(request->line, "POST") == 0) {
                request->method = HTTP_METHOD_POST;
                http_parse_header_next_state(request, HTTP_STATE_SERVER_READ_PATH);
            } else if(strcmp(request->line, "DELETE") == 0) {
                request->method = HTTP_METHOD_DELETE;
                http_parse_header_next_state(request, HTTP_STATE_SERVER_READ_PATH);
            } else {
                LOG("Unsupported HTTP method '%s'", request->line);
                request->method = HTTP_METHOD_UNSUPPORTED;
                request->error = HTTP_STATUS_METHOD_NOT_ALLOWED;
                http_parse_header_next_state(request, HTTP_STATE_ERROR);
            }
            return;
        }
        break;

    case HTTP_STATE_SERVER_READ_PATH:
        if(c == ' ' || c == '?') {
            request->line[request->line_index] = 0;
            request->path = malloc(strlen(request->line)+1);
            strcpy(request->path, request->line);

            if(c == '?') {
                http_parse_header_next_state(request, HTTP_STATE_SERVER_READ_QUERY);
            } else {
                http_parse_header_next_state(request, HTTP_STATE_SERVER_READ_VERSION);
            }
            return;
        }
        break;

    case HTTP_STATE_SERVER_READ_QUERY:
        if(c == ' ') {
            request->line[request->line_index] = 0;
            request->query = malloc(strlen(request->line)+1);
            strcpy(request->query, request->line);

            http_parse_header_next_state(request, HTTP_STATE_SERVER_READ_VERSION);

            return;
        }
        break;

    case HTTP_STATE_SERVER_READ_VERSION:
        if(c == '\r') {
            request->line[request->line_index] = 0;
            if(strcmp(request->line, "HTTP/1.1") == 0) {
#ifdef LOG_VERBOSE
                const char *method = 0;
                if(request->method == HTTP_METHOD_GET) {
                    method = "GET";
                } else if(request->method == HTTP_METHOD_POST) {
                    method = "POST";
                } else if(request->method == HTTP_METHOD_DELETE) {
                    method = "DELETE";
                }
                if(method) {
                    LOG("%s %s HTTP/1.1", method, request->path);
                }
#endif
                http_parse_header_next_state(request, HTTP_STATE_SERVER_READ_HEADER | HTTP_STATE_READ_NL);
            } else if(strcmp(request->line, "HTTP/1.0") == 0) {
                LOG("HTTP/1.0 not supported");
                request->error = HTTP_STATUS_VERSION_NOT_SUPPORTED;
                http_parse_header_next_state(request, HTTP_STATE_ERROR);
            } else {
                LOG("Unsupported HTTP version '%s'", request->line);
                request->error = HTTP_STATUS_BAD_REQUEST;
                http_parse_header_next_state(request, HTTP_STATE_ERROR);
            }

            return;
        }
        break;

    case HTTP_STATE_SERVER_READ_HEADER:
    case HTTP_STATE_CLIENT_READ_HEADER:
        if(c == '\r') {
            request->line[request->line_index] = 0;
#ifdef LOG_VERBOSE
            LOG("%s", request->line);
#endif
            if(request->line_index == 0) {
                http_parse_header_next_state(request, HTTP_STATE_IDLE | HTTP_STATE_READ_NL);
            } else {

                char *val;

                if(http_is_server(request)) {
                    if((val = cmp_str_prefix(request->line, "Host: ")) != 0) {
                        request->host = malloc(strlen(val) + 1);
                        strcpy(request->host, val);
                    } else if((val = cmp_str_prefix(request->line, "Accept-Encoding: ")) != 0) {
                        if(strstr(val, "gzip") != 0) {
                            request->flags |= HTTP_FLAG_ACCEPT_GZIP;
                        }
                    } else if((val = cmp_str_prefix(request->line, "Upgrade: ")) != 0) {
                        if(strstr(val, "websocket") != 0) {
                            request->flags |= HTTP_FLAG_WEBSOCKET;
                        }
                    } else if((val = cmp_str_prefix(request->line, "Sec-WebSocket-Key: ")) != 0) {
                        request->websocket_key = malloc(strlen(val) + 1);
                        strcpy(request->websocket_key, val);
                    }
                } else {
                    if((val = cmp_str_prefix(request->line, "Content-Type: ")) != 0) {
                        request->content_type = malloc(strlen(val) + 1);
                        strcpy(request->content_type, val);
                    }
                }

                if((val = cmp_str_prefix(request->line, "Transfer-Encoding: ")) != 0) {
                    if(strstr(val, "chunked") != 0) {
                        request->flags |= HTTP_FLAG_READ_CHUNKED;
                    }
                } else if((val = cmp_str_prefix(request->line, "Content-Length: ")) != 0) {
                    char *p;
                    request->read_content_length = strtol(val, &p, 10);
                    if(!p || *p) {
                        http_parse_header_next_state(request, HTTP_STATE_ERROR);
                        request->error = HTTP_STATUS_BAD_REQUEST;
                        LOG("Error parsing content length '%s'", val);
                        return;
                    }
                }

                http_parse_header_next_state(request, HTTP_STATE_READ| HTTP_STATE_HEADER | HTTP_STATE_READ_NL);
            }

            return;
        }
        break;

    case HTTP_STATE_CLIENT_READ_VERSION:
        if(c == ' ') {
            request->line[request->line_index] = 0;
            if(strcmp(request->line, "HTTP/1.1") != 0) {
                LOG("Unexpected HTTP version '%s'", request->line);
            }

            http_parse_header_next_state(request, HTTP_STATE_CLIENT_READ_STATUS);

            return;
        }
        break;

    case HTTP_STATE_CLIENT_READ_STATUS:
        if(c == ' ') {
            char *p;
            request->line[request->line_index] = 0;
            request->status = strtol(request->line, &p, 10);
            if(!p || *p) {
                LOG("Error reading response code '%s' (%s)", request->line, p);
            }

            http_parse_header_next_state(request, HTTP_STATE_CLIENT_READ_STATUS_DESC);

            return;
        }
        break;

    case HTTP_STATE_CLIENT_READ_STATUS_DESC:
        if(c == '\r') {
            request->line[request->line_index] = 0;
            http_parse_header_next_state(request, HTTP_STATE_CLIENT_READ_HEADER | HTTP_STATE_READ_NL);

            return;
        }
        break;

    case HTTP_STATE_ERROR:
        return;

    default:
        LOG("Unhandled state 0x%02X", request->state);
        request->error = HTTP_STATUS_BAD_REQUEST;
        http_parse_header_next_state(request, HTTP_STATE_ERROR);
        return;
    }

    if(request->line_index < request->line_length - 1) {
        request->line[request->line_index++] = c;
    }
}

int http_urldecode(char *dest, const char* src, int max_len)
{
    int len = 0;
    int esc = 0;
    int c;

    while(*src) {
        if(esc == 0) {
            if(*src == '%') {
                esc = 1;
                c = 0;
            } else {
                if(dest) {
                    if(len < max_len) {
                        if(*src == '+') {
                            dest[len++] = ' ';
                        } else {
                            dest[len++] = *src;
                        }
                    } else {
                        break;
                    }
                } else {
                    len++;
                }
            }
        } else if(esc == 1) {
            c = http_hex_to_int(*src);
            esc = 2;
        } else if(esc == 2) {
            if(dest) {
                if(len < max_len) {
                    dest[len++] = (c << 4) | http_hex_to_int(*src);
                } else {
                    break;
                }
            } else {
                len++;
            }
            esc = 0;
        }

        src++;
    }

    if(dest && len < max_len) {
        dest[len] = 0;
    }

    return len;
}



static void parse_query_string(struct http_request *request)
{
    int n = 0;
    if(request->query && *request->query) {
        n++;
    }

    for(char *s = request->query; *s; s++) {
        if(*s == '&') {
            n++;
        }
    }

    request->query_list = malloc(sizeof(char*) * (n+1));

    char *name = request->query;

    int i = 0;
    for(;;) {
        if(name) {
            request->query_list[i++] = name;
        }

        char *delim = strchr(name, '&');

        if(delim) {
            *delim = 0;
        }

        char *value = strchr(name, '=');
        if(value) {
            value++;
            http_urldecode(value, value, strlen(value));
        } else {
            LOG("Query parameter '%s' has no value", name);
        }

        if(!delim) {
            request->query_list[i] = 0;
            break;
        }

        name = delim + 1;
    }

}

int http_urlencode(char *dest, const char* src, int max_len)
{
    int len = 0;

    while(*src) {
        char c = *src++;
        if(('0' <= c && c <= '9') || ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z')
           || c == '-' || c == '.' || c == '_' || c == '~') {
            if(dest) {
                if(len > max_len) {
                    break;
                }

                dest[len] = c;
            }

            len++;
        } else {
            if(dest) {
                if(len > max_len - 2) {
                    break;
                }

                uint8_t hi = c >> 4;
                uint8_t lo = c & 0x0F;

                dest[len] = '%';
                dest[len+1] = (hi <= 9) ? (hi + '0') : (hi - 0x0A + 'A');
                dest[len+2] = (lo <= 9) ? (lo + '0') : (lo - 0x0A + 'A');
            }

            len += 3;
        }
    }

    if(dest && len < max_len) {
        dest[len] = 0;
    }

    return len;
}


const char *http_get_query_arg(struct http_request *request, const char *name)
{
    if(name && request->query) {
        if(!request->query_list) {
            parse_query_string(request);
        }

        int name_len = strlen(name);

        for(char **query_ptr = request->query_list; *query_ptr; query_ptr++)
        {
            if(strncmp(name, *query_ptr, name_len) == 0) {
                if(*(*query_ptr + name_len) == '=') {
                    return *query_ptr + name_len + 1;
                }
            }
        }
    }

    return 0;
}
