#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "http-sm/http.h"
#include "log.h"

static const char *WWW_DIR = HTTP_WWW_DIR;
static const char *GZIP_EXT = ".gz";
static const char *HASH_EXT = ".hs";

#define HASH_LEN 40

struct http_fs_response
{
    int fd;
    char buf[128];
};

struct http_mime_map
{
    const char *ext;
    const char *type;
};

static const struct http_mime_map mime_tab[] = {
    {"html", "text/html"},
    {"css", "text/css"},
    {"js", "text/javascript"},
    {"png", "image/png"},
    {"svg", "image/svg+xml"},
    {"json", "application/json"},
    {NULL, "text/plain"},
};

static const char *get_mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');

    const struct http_mime_map *p;

    if(!ext) {
        return "text/plain";
    }

    ext++;

    for(p = mime_tab; p->ext; p++) {
        if(!strcmp(ext, p->ext)) {
            break;
        }
    }
    return p->type;
}

enum http_cgi_state cgi_fs(struct http_request* request)
{
    if(request->method != HTTP_METHOD_GET) {
        return HTTP_CGI_NOT_FOUND;
    }

    if(!request->cgi_data) {
        const char *base_filename;
        const char *www_prefix;

        if(request->cgi_arg) {
            base_filename = request->cgi_arg;
            www_prefix = "";
        } else {
            base_filename = request->path;
            www_prefix = WWW_DIR;
        }

        char *filename;

        filename = malloc(strlen(www_prefix) + strlen(base_filename) + strlen(GZIP_EXT) + 1);

        if(!filename) {
            return HTTP_CGI_NOT_FOUND;
        }

        strcpy(filename, www_prefix);
        strcat(filename, base_filename);

        int filename_len = strlen(filename);

        uint8_t file_flag = 0;
        int fd = -1;

        char *etag = NULL;

        strcat(filename, HASH_EXT);

        fd = open(filename, O_RDONLY);

        if(fd >= 0) {
            etag = malloc(HASH_LEN + 3);
            if(etag) {
                int n = read(fd, etag + 1, HASH_LEN);
                if(n == HASH_LEN) {
                    etag[0] = '"';
                    etag[n+1] = '"';
                    etag[n+2] = 0;
                } else {
                    free(etag);
                    etag = NULL;
                }
            }
        }

        if(etag && request->etag && (strncmp(etag+1, request->etag, HASH_LEN) == 0)) {
            http_begin_response(request, 304, NULL);
            http_write_header(request, "Cache-Control", "max-age=3600, must-revalidate");
            http_set_content_length(request, 0);

            if(etag) {
                http_write_header(request, "ETag", etag);
            }

            http_end_header(request);
            http_end_body(request);

            free(filename);
            free(etag);

            return HTTP_CGI_DONE;
        }

        filename[filename_len] = 0;

        if(!file_flag && (request->flags & HTTP_FLAG_ACCEPT_GZIP)) {
            strcat(filename, GZIP_EXT);

            fd = open(filename, O_RDONLY);

            if(fd >= 0) {
                INFO("Opened file '%s'", filename);

                file_flag |= HTTP_FLAG_ACCEPT_GZIP;
            } else {
                INFO("File '%s' not found", filename);
            }

            filename[strlen(filename) - strlen(GZIP_EXT)] = 0;
        }

        if(!file_flag) {
            fd = open(filename, O_RDONLY);

            if(fd >= 0) {
                INFO("Opened file '%s'", filename);
            } else {
                INFO("File '%s' not found", filename);
            }
        }

        if(fd < 0) {
            free(etag);
            free(filename);
            return HTTP_CGI_NOT_FOUND;
        }

        struct stat s;
        fstat(fd, &s);
        INFO("File size: %d", s.st_size);

        const char *mime_type = get_mime_type(filename);

        free(filename);


        request->cgi_data = malloc(sizeof(struct http_fs_response));

        if(!request->cgi_data) {
            free(etag);
            return HTTP_CGI_NOT_FOUND;
        }

        struct http_fs_response *resp = request->cgi_data;

        resp->fd = fd;

        http_begin_response(request, 200, mime_type);
        http_write_header(request, "Cache-Control", "max-age=3600, must-revalidate");
        http_set_content_length(request, s.st_size);

        if(file_flag & HTTP_FLAG_ACCEPT_GZIP) {
            http_write_header(request, "Content-Encoding", "gzip");
        }

        if(etag) {
            http_write_header(request, "ETag", etag);
        }

        http_end_header(request);

        free(etag);

        return HTTP_CGI_MORE;
    } else {
        struct http_fs_response *resp = request->cgi_data;

        int n = read(resp->fd, resp->buf, sizeof(resp->buf));

        if(n > 0) {
            http_write_bytes(request, resp->buf, n);
        }

        if(n < sizeof(resp->buf)) {
            http_end_body(request);

            close(resp->fd);
            free(request->cgi_data);

            return HTTP_CGI_DONE;
        } else {
            return HTTP_CGI_MORE;
        }
    }
}
