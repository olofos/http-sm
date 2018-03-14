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
