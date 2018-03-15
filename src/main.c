#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include "http.h"
#include "http-private.h"
#include "log.h"

int http_server_read(struct http_request *request)
{
    if(request->state == HTTP_STATE_READ_BODY) {
        return 0;
    } else if(request->state == HTTP_STATE_READ_EXPECT_EOF) {
        char c;
        int n = read(request->fd, &c, 1);

        if(n < 0) {
            perror("read");
        } else if(n == 0) {
            LOG("Connection %d done", request->fd);

            request->state = HTTP_STATE_DONE;
            http_close(request);
        } else {
            LOG("Expected EOF but got %c", c);
        }
    } else {
        if(request->state == HTTP_STATE_READ_REQ_BEGIN) {
            http_response_init(request);

            const int line_len = HTTP_LINE_LEN;
            request->line = malloc(line_len);
            request->line_len = line_len;
            request->line_index = 0;
        }

        char c;
        int n = read(request->fd, &c, 1);
        if(n < 0) {
            perror("read");
            request->state = HTTP_STATE_ERROR;

            free(request->line);
            request->line = 0;
            request->line_len = 0;

            http_close(request);
            return -1;
        } else if(n==0) {
            LOG("Unexpected EOF");
            request->state = HTTP_STATE_ERROR;

            free(request->line);
            request->line = 0;
            request->line_len = 0;

            http_close(request);
            return -1;
        } else {
            http_parse_header(request, c);
            if(request->state == HTTP_STATE_DONE) {
                if(request->method == HTTP_METHOD_POST) {
                    request->state = HTTP_STATE_READ_BODY;
                } else {
                    request->state = HTTP_STATE_WRITE;
                }
            }
        }
    }
    return 0;
}

const char *http_status_string(enum http_status status)
{
    switch(status) {
    case HTTP_STATUS_OK:
        return "OK";

    case HTTP_STATUS_BAD_REQUEST:
        return "Bad Request";

    case HTTP_STATUS_NOT_FOUND:
        return "Not Found";

    case HTTP_STATUS_METHOD_NOT_ALLOWED:
        return "Method Not Allowed";

    case HTTP_STATUS_VERSION_NOT_SUPPORTED:
        return "HTTP Version Not Supported";
    }

    return "Status Unkown";
}

int http_begin_response(struct http_request *request, int status, const char *content_type)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "HTTP/1.1 %d %s\r\n", status, http_status_string(status));
    http_write_string(request, buf);
    http_write_header(request, "Connection", "close");
    http_write_header(request, "Content-Type", content_type);

    return 0;
}

int http_end_body(struct http_request *request)
{
    if(request->flags & HTTP_FLAG_CHUNKED) {
        http_write_string(request, "");
    }
    return 0;
}

enum http_cgi_state cgi_not_found(struct http_request* request);

int http_server_write(struct http_request *request)
{
    int i = 0;

    for(;;) {

        if(request->handler) {
            enum http_cgi_state state = request->handler(request);

            if(state == HTTP_CGI_DONE) {
                request->state = HTTP_STATE_READ_EXPECT_EOF;
                break;
            } else if(state == HTTP_CGI_MORE) {
                break;
            }
        }

        if(http_url_tab[i].url == NULL) {
            request->handler = cgi_not_found;
            continue;
        } else if(http_server_match_url(http_url_tab[i].url, request->path)) {
            request->handler = http_url_tab[i].handler;
        }

        i++;
    }

    return 0;
}

int http_server_main_loop(struct http_server *server)
{
    fd_set set_read, set_write;
    int maxfd;

    int num_open = http_create_select_sets(server, &set_read, &set_write, &maxfd);

    int n;
    if(num_open != 0) {
        struct timeval t;
        t.tv_sec = 20;
        t.tv_usec = 0;

        n = select(maxfd+1, &set_read, &set_write, 0, &t);
    } else {
        n = select(maxfd+1, &set_read, &set_write, 0, 0);
    }

    if(n < 0) {
        perror("select");
    } else if(n == 0) {
        for(int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
            if(server->request[i].fd >= 0) {
                LOG("Socket %d timed out. Closing.", server->request[i].fd);
                http_close(&server->request[i]);
            }
        }
    } else {
        if(FD_ISSET(server->fd, &set_read)) {
            http_accept_new_connection(server);
        }

        for(int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
            if(server->request[i].fd >= 0) {
                if(FD_ISSET(server->request[i].fd, &set_read)) {
                    http_server_read(&server->request[i]);
                } else if(FD_ISSET(server->request[i].fd, &set_write)) {
                    http_server_write(&server->request[i]);
                }
            }
        }
    }

    return 0;
}

int http_server_start(struct http_server *server, int port)
{
    int listen_fd = http_open_listen_socket(port);

    if(listen_fd < 0) {
        return -1;
    }

    server->fd = listen_fd;

    for(int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
        server->request[i].fd = -1;
        server->request[i].state = HTTP_STATE_DONE;
    }

    return 0;
}

int server_main(int port)
{
    struct http_server server;

    if(http_server_start(&server, port) < 0) {
        return -1;
    }

    for(;;) {
        if(http_server_main_loop(&server) < 0) {
            return -1;
        }
    }
}

enum http_cgi_state cgi_not_found(struct http_request* request)
{
    const char *response = "Not found\r\n";

    http_begin_response(request, 404, "text/plain");
    http_set_content_length(request, strlen(response));
    http_end_header(request);
    http_write_string(request, response);
    http_end_body(request);

    return HTTP_CGI_DONE;

}

enum http_cgi_state cgi_simple(struct http_request* request)
{
    if(request->method != HTTP_METHOD_GET) {
        return HTTP_CGI_NOT_FOUND;
    }

    const char *response = "This is a response from \'cgi_simple\'\r\n";

    http_begin_response(request, 200, "text/plain");
    http_set_content_length(request, strlen(response));
    http_end_header(request);

    http_write_string(request, response);

    http_end_body(request);

    return HTTP_CGI_DONE;
}

enum http_cgi_state cgi_stream(struct http_request* request)
{
    if(request->method != HTTP_METHOD_GET) {
        return HTTP_CGI_NOT_FOUND;
    }

    if(!request->cgi_data) {
        http_begin_response(request, 200, "text/plain");
        http_end_header(request);

        request->cgi_data = malloc(1);

        return HTTP_CGI_MORE;
    } else {
        const char response[] = "This is a response from \'cgi_stream\'\r\n";

        http_write_string(request, response);
        http_end_body(request);

        free(request->cgi_data);

        return HTTP_CGI_DONE;
    }
}

enum http_cgi_state cgi_query(struct http_request* request)
{
    if(request->method != HTTP_METHOD_GET) {
        return HTTP_CGI_NOT_FOUND;
    }

    http_begin_response(request, 200, "text/plain");
    http_end_header(request);

    http_write_string(request, "This is a response from \'cgi_query\'\r\n");
    http_write_string(request, "The parameters were:\r\n");

    const char *sa = http_get_query_arg(request, "a");
    const char *sb = http_get_query_arg(request, "b");

    if(sa) {
        http_write_string(request, "a = ");
        http_write_string(request, sa);
        http_write_string(request, "\r\n");
    }

    if(sb) {
        http_write_string(request, "b = ");
        http_write_string(request, sb);
        http_write_string(request, "\r\n");
    }

    http_end_body(request);

    return HTTP_CGI_DONE;
}

struct http_url_handler http_url_tab_[] = {
    {"/simple", cgi_simple, NULL},
    {"/stream", cgi_stream, NULL},
    {"/query", cgi_query, NULL},
    {"/wildcard/*", cgi_simple, NULL},
    {NULL, NULL, NULL}
};

struct http_url_handler *http_url_tab = http_url_tab_;

extern const char *log_system;

void test_request(int port, char *path)
{
    struct http_request request = {
        .host = "localhost",
        .path = path,
        .port = port,
    };

    if(http_get_request(&request) > 0) {
        putchar('\n');
        printf("Status: %d\n\n", request.status);
        int c;
        while((c = http_getc(&request)) > 0) {
            putchar(c);
        }

        putchar('\n');

        http_close(&request);
    }
}

int main(int argc, char *argv[])
{
    if(argc > 1) {
        if(strcmp(argv[1], "-s") == 0) {
            server_main(8080);
        }
    } else {
        pid_t child = fork();

        srand(time(0));
        int port = 1024 + (rand() % 1024);

        if(child < 0) {
            perror("fork");
            return 1;
        } else if(child == 0) {
            log_system = "server";
            LOG("Starting server");
            server_main(port);

            LOG("server_main returned!");
            for(;;) {
            }
        } else {
            log_system = "client";
            LOG("Server pid: %d", child);

            test_request(port, "/simple");
            test_request(port, "/stream");
            test_request(port, "/stream");
            test_request(port, "/query");
            test_request(port, "/query?a=1");
            test_request(port, "/query?a=1&b=2+3&c=4");
            test_request(port, "/wildcard/xyz?a=1&b=2+3&c=4");
            test_request(port, "/missing");

            usleep(1000);
            kill(child, SIGINT);

            LOG("Waiting for child to teminate");

            int status;
            waitpid(child, &status, 0);
        }
    }

    return 0;
}
