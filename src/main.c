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

#include <setjmp.h>
#include <cmocka.h>

#include "http.h"
#include "http-private.h"
#include "log.h"


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

enum http_cgi_state cgi_exit(struct http_request* request)
{
    const char *response = "Exiting\r\n";

    http_begin_response(request, 200, "text/plain");
    http_set_content_length(request, strlen(response));
    http_end_header(request);

    http_write_string(request, response);
    http_end_body(request);

    usleep(10 * 1000);

    char c;
    read(request->fd, &c, 1);

    free(request->line);
    http_close(request);

    LOG("Exiting");

    exit(0);
}

struct http_url_handler http_url_tab_[] = {
    {"/simple", cgi_simple, NULL},
    {"/stream", cgi_stream, NULL},
    {"/query", cgi_query, NULL},
    {"/wildcard/*", cgi_simple, NULL},
    {"/exit", cgi_exit, NULL},
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

static int http_port;

static int read_all(struct http_request *request, char *buf)
{
    int i = 0;
    int c;
    while((c = http_getc(request)) > 0) {
        putchar(c);
        buf[i++] = c;
    }
    buf[i] = 0;
    return c;
}

static void test_simple_request(void **states)
{
    struct http_request request = {
        .host = "localhost",
        .path = "/simple",
        .port = http_port,
    };

    int ret;

    ret = http_get_request(&request);
    assert_true(ret > 0);
    assert_int_equal(200, request.status);

    char buf[256];
    ret = read_all(&request, buf);
    assert_int_equal(ret, 0);

    assert_non_null(strstr(buf, "'cgi_simple'"));

    http_close(&request);
}

static void test_stream_request(void **states)
{
    struct http_request request = {
        .host = "localhost",
        .path = "/stream",
        .port = http_port,
    };

    int ret;
    ret = http_get_request(&request);
    assert_true(ret > 0);
    assert_int_equal(200, request.status);

    char buf[256];
    ret = read_all(&request, buf);
    assert_int_equal(ret, 0);

    assert_non_null(strstr(buf, "'cgi_stream'"));

    http_close(&request);
}

static void test_query_request_no_arg(void **states)
{
    struct http_request request = {
        .host = "localhost",
        .path = "/query",
        .port = http_port,
    };

    int ret;

    ret = http_get_request(&request);
    assert_true(ret > 0);
    assert_int_equal(200, request.status);

    char buf[256];
    ret = read_all(&request, buf);
    assert_int_equal(ret, 0);

    assert_non_null(strstr(buf, "'cgi_query'"));
    assert_null(strstr(buf, "="));

    http_close(&request);
}

static void test_query_request_with_arg(void **states)
{
    struct http_request request = {
        .host = "localhost",
        .path = "/query?a=1&b=2%203&c=4",
        .port = http_port,
    };

    int ret;

    ret = http_get_request(&request);
    assert_true(ret > 0);
    assert_int_equal(200, request.status);

    char buf[256];
    ret = read_all(&request, buf);
    assert_int_equal(ret, 0);

    assert_non_null(strstr(buf, "'cgi_query'"));
    assert_non_null(strstr(buf, "a = 1"));
    assert_non_null(strstr(buf, "b = 2 3"));
    assert_null(strstr(buf, "c ="));

    http_close(&request);
}

static void test_wildcard_request(void **states)
{
    struct http_request request = {
        .host = "localhost",
        .path = "/wildcard/xyz?abc=123",
        .port = http_port,
    };

    int ret;

    ret = http_get_request(&request);
    assert_true(ret > 0);
    assert_int_equal(200, request.status);

    char buf[256];
    ret = read_all(&request, buf);
    assert_int_equal(ret, 0);

    assert_non_null(strstr(buf, "'cgi_simple'"));

    http_close(&request);
}

static void test_not_found_request(void **states)
{
    struct http_request request = {
        .host = "localhost",
        .path = "/not_found",
        .port = http_port,
    };

    int ret;

    ret = http_get_request(&request);
    assert_true(ret > 0);
    assert_int_equal(404, request.status);

    char buf[256];
    ret = read_all(&request, buf);
    assert_int_equal(ret, 0);

    assert_non_null(strstr(buf, "Not found"));

    http_close(&request);
}

static void test_that_server_can_handle_a_timeout(void **states)
{
    struct http_request request = {
        .host = "localhost",
        .path = "/not_found",
        .port = http_port,
    };

    int ret;

    ret = http_open_request_socket(&request);
    assert_true(ret > 0);

    usleep(HTTP_SERVER_TIMEOUT_SECS * 1000 * 1000 + HTTP_SERVER_TIMEOUT_USECS + 250 * 1000);
}

const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_simple_request),
    cmocka_unit_test(test_stream_request),
    cmocka_unit_test(test_query_request_no_arg),
    cmocka_unit_test(test_query_request_with_arg),
    cmocka_unit_test(test_wildcard_request),
    cmocka_unit_test(test_not_found_request),
    cmocka_unit_test(test_that_server_can_handle_a_timeout),
};

int main(int argc, char *argv[])
{
    if(argc > 1) {
        if(strcmp(argv[1], "-s") == 0) {
            http_server_main(8080);
        }
    } else {
        pid_t child = fork();

        srand(time(0));
        http_port = 1024 + (rand() % 1024);

        if(child < 0) {
            perror("fork");
            return 1;
        } else if(child > 0) {
            log_system = "server";
            LOG("Starting server");
            http_server_main(http_port);

            LOG("server_main returned!");
            for(;;) {
            }
        } else {
            log_system = "client";
            LOG("Server pid: %d", child);

            int fails = cmocka_run_group_tests(tests, NULL, NULL);

            struct http_request request = {
                .host = "localhost",
                .path = "/exit",
                .port = http_port,
            };

            http_get_request(&request);
            char buf[256];
            read_all(&request, buf);

            http_close(&request);

            LOG("Waiting for child to teminate");

            int status;
            waitpid(child, &status, 0);

            return fails;
        }
    }

    return 0;
}
