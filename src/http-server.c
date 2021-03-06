#include <string.h>

#include "http-private.h"

int http_server_match_url(const char *server_url, const char *request_url)
{
    int n = strlen(server_url);

    if(server_url[n-1] == '*') {
        return strncmp(server_url, request_url, n-1) == 0;
    } else {
        return strcmp(server_url, request_url) == 0;
    }
}

const char *http_status_string(enum http_status status)
{
    switch(status) {
    case HTTP_STATUS_OK:
        return "OK";

    case HTTP_STATUS_NO_CONTENT:
        return "No Content";

    case HTTP_STATUS_NOT_MODIFIED:
        return "Not Modified";

    case HTTP_STATUS_BAD_REQUEST:
        return "Bad Request";

    case HTTP_STATUS_NOT_FOUND:
        return "Not Found";

    case HTTP_STATUS_METHOD_NOT_ALLOWED:
        return "Method Not Allowed";

    case HTTP_STATUS_URI_TOO_LONG:
        return "URI Too Long";

    case HTTP_STATUS_INTERNAL_SERVER_ERROR:
        return "Internal Server Error";

    case HTTP_STATUS_SERVICE_UNAVAILABLE:
        return "Service Unavailable";

    case HTTP_STATUS_VERSION_NOT_SUPPORTED:
        return "HTTP Version Not Supported";

    case HTTP_STATUS_ERROR:
        return NULL;
    }

    return "";
}
