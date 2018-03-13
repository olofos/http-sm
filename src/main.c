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
            LOG("Connection on fd %d done", request->fd);

            request->state = HTTP_STATE_DONE;
            http_close(request);
        } else {
            LOG("Expected EOF but got %c", c);
        }
    } else {
        if(request->state == HTTP_STATE_READ_REQ_BEGIN) {
            http_request_init(request);

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
            putchar(c);
            http_parse_header(request, c);
            if(!(request->state & HTTP_STATE_READ)) {
                LOG("state = 0x%02X", request->state);
            }

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

const char *http_status_string(int status)
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

    default:
        return "Status Unkown";
    }
}

int http_begin_response(struct http_request *request)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "HTTP/1.1 %d %s\r\n", request->status, http_status_string(request->status));
    write(request->fd, buf, strlen(buf));
    http_write_header(request, "Connection", "close");

    return 0;
}

int http_end_body(struct http_request *request)
{
    request->state = HTTP_STATE_READ_EXPECT_EOF;

    return 0;
}

int http_server_write(struct http_request *request)
{
    LOG("Got request:");
    LOG("Host: %s", request->host);
    LOG("Path: %s", request->path);

    LOG("Now we should write a response!");

    char *body = "That worked\r\n";

    request->status = HTTP_STATUS_OK;

    http_begin_response(request);
    http_set_content_length(request, strlen(body));
    http_write_header(request, "Content-Type", "text/plain");
    http_end_header(request);

    write(request->fd, body, strlen(body));

    http_end_body(request);

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
        t.tv_sec = 2;
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



int main(void)
{
    pid_t child = fork();

    srand(time(0));
    int port = 1024 + (rand() % 1024);

    if(child < 0) {
        perror("fork");
        return 1;
    } else if(child == 0) {
        LOG("Starting server");
        server_main(port);

        LOG("server_main returned!");
        for(;;) {
        }
    } else {
        LOG("Server running as process %d", child);

        struct http_request request = {
            .host = "localhost",
            .path = "/test",
            .port = port,
        };

        if(http_get_request(&request) > 0) {
            int c;
            while((c = http_getc(&request)) > 0) {
                putchar(c);
            }

            http_close(&request);
        }

        kill(child, SIGINT);

        LOG("Waiting for child to teminate");

        int status;
        waitpid(child, &status, 0);
    }
}
