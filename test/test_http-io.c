#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <stdarg.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <signal.h>

#include <cmocka.h>

#include "http.h"
#include "http-private.h"


static pid_t child_pid;

static void init_request(struct http_request *request, int fd);

static int open_tmp_file(void);
static int write_tmp_file(const char *s);
static int write_tmp_file_n(const char *s[]);
static int write_socket(const char *s);
static int write_socket_n(const char *s[]);
static void close_socket(int fd);

static const char *get_file_content(int fd);

#define assert_string_prefix_equal(pre, s) do { assert_true(strncmp(pre,s,strlen(pre)) == 0); } while(0)

#define assert_string_contains_substring(sub, s) do { assert_non_null(strstr(s, sub)); } while(0)

void LOG(const char *fmt, ...)
{
    // va_list va;
    // va_start(va, fmt);
    // vprintf(fmt, va);
    // va_end(va);
    // printf("\n");
}


// Tests ///////////////////////////////////////////////////////////////////////

static void test__http_getc__can_read_correctly_with_te_identity(void **states)
{
    char *str = "0123";

    int fd = write_tmp_file(str);
    assert_true(0 <= fd);

    struct http_request request;
    init_request(&request, fd);
    request.content_length = strlen(str);

    for(int i = 0; i < strlen(str); i++) {
        assert_int_equal(str[i], http_getc(&request));
    }

    close(fd);
}

static void test__http_getc__doesnt_read_more_than_content_length_with_te_identity(void **states)
{
    char *str = "01";

    int fd = write_tmp_file(str);
    assert_true(0 <= fd);

    struct http_request request;
    init_request(&request, fd);
    request.content_length = 1;

    assert_int_equal('0', http_getc(&request));
    assert_int_equal(0, http_getc(&request));

    close(fd);
}

static void test__http_getc__can_read_correctly_with_te_chunked(void **states)
{
    char *str = "0123";
    const char *s[] = {
        "4\r\n",
        str,
        "\r\n",
        "4\r\n",
        str,
        "\r\n",
        "0\r\n",
        0
    };

    int fd = write_tmp_file_n(s);
    assert_true(0 <= fd);

    struct http_request request;
    init_request(&request, fd);
    request.flags |= HTTP_FLAG_CHUNKED;

    request.content_length = strlen(str);

    for(int i = 0; i < strlen(str); i++) {
        int c = http_getc(&request);
        assert_int_equal(str[i], c);
    }

    for(int i = 0; i < strlen(str); i++) {
        int c = http_getc(&request);
        assert_int_equal(str[i], c);
    }

    close(fd);
}

static void test__http_getc__can_read_non_ascii_characters_with_te_identity(void **states)
{
    char *str = "\xBA\xAD\xF0\x0D";

    int fd = write_tmp_file(str);
    assert_true(0 <= fd);

    struct http_request request;
    init_request(&request, fd);
    request.content_length = strlen(str);

    assert_int_equal(0xBA, http_getc(&request));
    assert_int_equal(0xAD, http_getc(&request));
    assert_int_equal(0xF0, http_getc(&request));
    assert_int_equal(0x0D, http_getc(&request));

    close(fd);
}

static void test__http_getc__can_read_non_ascii_characters_with_te_chunked(void **states)
{
    char *str = "\xBA\xAD\xF0\x0D";

    const char *s[] = {
        "4\r\n",
        str,
        "\r\n0\r\n",
        0
    };

    int fd = write_tmp_file_n(s);
    assert_true(0 <= fd);

    struct http_request request;
    init_request(&request, fd);
    request.flags |= HTTP_FLAG_CHUNKED;

    assert_int_equal(0xBA, http_getc(&request));
    assert_int_equal(0xAD, http_getc(&request));
    assert_int_equal(0xF0, http_getc(&request));
    assert_int_equal(0x0D, http_getc(&request));

    close(fd);
}

static void test__http_getc__returns_zero_when_reading_eof_with_te_identity(void **states)
{
    char *str = "X";
    int fd = write_socket(str);
    assert_true(0 <= fd);

    struct http_request request;
    init_request(&request, fd);
    request.content_length = strlen(str);

    assert_int_equal('X', http_getc(&request));
    assert_int_equal(0, http_getc(&request));
    assert_int_equal(0, http_getc(&request));

    close_socket(fd);
}

static void test__http_getc__returns_zero_when_reading_eof_with_te_chunked(void **states)
{
    char *str = "X";
    const char *s[] = {
        "1\r\n",
        str,
        "\r\n0\r\n",
        0
    };

    int fd = write_socket_n(s);
    assert_true(0 <= fd);

    struct http_request request;
    init_request(&request, fd);
    request.flags |= HTTP_FLAG_CHUNKED;

    assert_int_equal('X', http_getc(&request));
    assert_int_equal(0, http_getc(&request));
    assert_int_equal(0, http_getc(&request));

    close_socket(request.fd);
}

static void test__http_getc__returns_zero_if_state_is_not_http_read_body_with_te_identity(void **states)
{
    char *str = "0123";

    int fd = write_tmp_file(str);
    assert_true(0 <= fd);

    struct http_request request;
    init_request(&request, fd);
    request.content_length = strlen(str);

    enum http_state all_other_states[] = {
        HTTP_STATE_IDLE,
        HTTP_STATE_ERROR,
        HTTP_STATE_READ_SERVER_METHOD,
        HTTP_STATE_READ_SERVER_PATH,
        HTTP_STATE_READ_SERVER_QUERY,
        HTTP_STATE_READ_SERVER_VERSION,
        HTTP_STATE_READ_CLIENT_VERSION,
        HTTP_STATE_READ_CLIENT_STATUS,
        HTTP_STATE_READ_CLIENT_STATUS_DESC,
        HTTP_STATE_READ_HEADER,
    };

    for(int i = 0; i < sizeof(all_other_states)/sizeof(all_other_states[0]); i++) {
        request.state = all_other_states[i];
        assert_int_equal(0, http_getc(&request));
    }

    close(fd);
}

static void test__http_getc__returns_zero_if_state_is_not_http_read_body_with_te_chunked(void **states)
{
    char *str =
        "4\r\n"
        "0123\r\n"
        "0\r\n";

    int fd = write_tmp_file(str);
    assert_true(0 <= fd);

    struct http_request request;
    init_request(&request, fd);
    request.flags |= HTTP_FLAG_CHUNKED;

    enum http_state all_other_states[] = {
        HTTP_STATE_IDLE,
        HTTP_STATE_ERROR,
        HTTP_STATE_READ_SERVER_METHOD,
        HTTP_STATE_READ_SERVER_PATH,
        HTTP_STATE_READ_SERVER_QUERY,
        HTTP_STATE_READ_SERVER_VERSION,
        HTTP_STATE_READ_CLIENT_VERSION,
        HTTP_STATE_READ_CLIENT_STATUS,
        HTTP_STATE_READ_CLIENT_STATUS_DESC,
        HTTP_STATE_READ_HEADER,
    };

    for(int i = 0; i < sizeof(all_other_states)/sizeof(all_other_states[0]); i++) {
        request.state = all_other_states[i];
        assert_int_equal(0, http_getc(&request));
    }

    close(fd);
}

static void test__http_getc__read_to_end_of_file_with_te_chunked(void **states)
{
    const char *s =
        "4\r\n"
        "0123"
        "\r\n"
        "4\r\n"
        "0123"
        "\r\n"
        "0\r\n"
        "\r\n";

    int fd = write_tmp_file(s);
    assert_true(0 <= fd);

    struct http_request request;
    init_request(&request, fd);
    request.flags |= HTTP_FLAG_CHUNKED;

    int c;
    while((c = http_getc(&request)) > 0) {
    }
    assert_int_equal(0, c);

    off_t pos = lseek(request.fd, 0, SEEK_CUR);
    off_t end = lseek(request.fd, 0, SEEK_END);

    printf("seek: %ld %ld\n", pos, end);

    assert_int_equal(end, pos);

    close(fd);
}

static void test__http_peek__returns_the_next_character_with_te_identity(void **states)
{
    char *str = "0123";

    int fd = write_tmp_file(str);
    assert_true(0 <= fd);

    struct http_request request;
    init_request(&request, fd);
    request.content_length = strlen(str);

    assert_int_equal('0', http_peek(&request));
    assert_int_equal('0', http_peek(&request));
    assert_int_equal('0', http_getc(&request));
    assert_int_equal('1', http_getc(&request));
    assert_int_equal('2', http_peek(&request));
    assert_int_equal('2', http_peek(&request));
    assert_int_equal('2', http_getc(&request));

    close(fd);
}

static void test__http_peek__returns_the_next_character_with_te_chunked(void **states)
{
    char *str = "0123";

    const char *s[] = {
        "4\r\n",
        str,
        "\r\n0\r\n",
        0
    };

    int fd = write_tmp_file_n(s);
    assert_true(0 <= fd);

    struct http_request request;
    init_request(&request, fd);
    request.flags |= HTTP_FLAG_CHUNKED;

    assert_int_equal('0', http_peek(&request));
    assert_int_equal('0', http_peek(&request));
    assert_int_equal('0', http_getc(&request));
    assert_int_equal('1', http_getc(&request));
    assert_int_equal('2', http_peek(&request));
    assert_int_equal('2', http_peek(&request));
    assert_int_equal('2', http_getc(&request));

    close(fd);
}


static void test__http_write_header__writes_the_header(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_request(&request, fd);

    http_write_header(&request, "Connection", "close");
    assert_string_equal("Connection: close\r\n", get_file_content(fd));

    close(fd);
}

static void test__http_write_header__writes_nothing_when_name_is_null(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_request(&request, fd);

    http_write_header(&request, NULL, "close");
    assert_string_equal("", get_file_content(fd));

    close(fd);
}

static void test__http_write_header__writes_nothing_when_value_is_null(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_request(&request, fd);

    http_write_header(&request, "Test", NULL);
    assert_string_equal("", get_file_content(fd));

    close(fd);
}

static void test__http_begin_request__writes_the_request_line_without_query(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request = {
        .fd = fd,
        .method = HTTP_METHOD_GET,
        .host = "www.example.com",
        .path = "/",
        .query = 0,
        .port = 80,
    };

    http_begin_request(&request);
    assert_string_prefix_equal("GET / HTTP/1.1\r\n", get_file_content(fd));

    close(fd);
}

static void test__http_begin_request__writes_the_request_line_with_query(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request = {
        .fd = fd,
        .method = HTTP_METHOD_GET,
        .host = "www.example.com",
        .path = "/",
        .query = "a=1",
        .port = 80,
    };

    http_begin_request(&request);
    assert_string_prefix_equal("GET /?a=1 HTTP/1.1\r\n", get_file_content(fd));

    close(fd);
}

static void test__http_begin_request__writes_the_request_line_with_empty_query(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request = {
        .fd = fd,
        .method = HTTP_METHOD_GET,
        .host = "www.example.com",
        .path = "/",
        .query = "",
        .port = 80,
    };

    http_begin_request(&request);
    assert_string_prefix_equal("GET / HTTP/1.1\r\n", get_file_content(fd));

    close(fd);
}

static void test__http_begin_request__sends_host_header(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request = {
        .fd = fd,
        .method = HTTP_METHOD_GET,
        .host = "www.example.com",
        .path = "/",
        .query = "a=1",
        .port = 80,
    };

    http_begin_request(&request);
    assert_string_contains_substring("Host: www.example.com\r\n", get_file_content(fd));

    close(fd);
}

static void test__http_begin_request__sends_host_header_with_port_if_port_is_not_80(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request = {
        .fd = fd,
        .method = HTTP_METHOD_GET,
        .host = "www.example.com",
        .path = "/",
        .query = "a=1",
        .port = 8080,
    };

    http_begin_request(&request);
    assert_string_contains_substring("Host: www.example.com:8080\r\n", get_file_content(fd));

    close(fd);
}

static void test__http_begin_request__has_GET_as_default_method(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request = {
        .fd = fd,
        .host = "www.example.com",
        .path = "/",
        .query = 0,
        .port = 80,
    };

    http_begin_request(&request);
    assert_string_prefix_equal("GET / HTTP/1.1\r\n", get_file_content(fd));

    close(fd);
}

static void test__http_begin_request__writes_the_POST_request_line(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request = {
        .fd = fd,
        .method = HTTP_METHOD_POST,
        .host = "www.example.com",
        .path = "/",
        .query = 0,
        .port = 80,
    };

    http_begin_request(&request);
    assert_string_prefix_equal("POST / HTTP/1.1\r\n", get_file_content(fd));

    close(fd);
}

static void test__http_begin_request__sends_user_agent_header(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request = {
        .fd = fd,
        .method = HTTP_METHOD_GET,
        .host = "www.example.com",
        .path = "/",
        .query = "a=1",
        .port = 80,
    };

    http_begin_request(&request);
    assert_string_contains_substring("User-Agent: " HTTP_USER_AGENT, get_file_content(fd));

    close(fd);
}


static void test__http_end_headers__sends_new_line_in_request(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request = {
        .fd = fd,
        .method = HTTP_METHOD_GET,
        .host = "www.example.com",
        .path = "/",
        .query = "a=1",
        .port = 80,
        .flags = HTTP_FLAG_CLIENT,
    };

    http_end_header(&request);
    assert_string_equal("\r\n", get_file_content(fd));
    assert_false(request.flags & HTTP_FLAG_CHUNKED);

    close(fd);
}

static void test__http_end_headers__does_not_change_chunked_flag_in_request(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request = {
        .fd = fd,
        .method = HTTP_METHOD_GET,
        .host = "www.example.com",
        .path = "/",
        .query = "a=1",
        .port = 80,
        .flags = HTTP_FLAG_CLIENT | HTTP_FLAG_CHUNKED,
    };

    http_end_header(&request);
    assert_string_equal("\r\n", get_file_content(fd));
    assert_true(request.flags & HTTP_FLAG_CHUNKED);

    close(fd);
}

static void test__http_end_headers__sets_chunked_flag_in_response_if_no_content_length(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request = {
        .fd = fd,
        .method = HTTP_METHOD_GET,
        .host = "www.example.com",
        .path = "/",
        .query = "a=1",
        .port = 80,
        .flags = 0,
        .content_length = -1,
    };

    http_end_header(&request);
    assert_true(request.flags & HTTP_FLAG_CHUNKED);

    const char *s = get_file_content(fd);
    assert_true(strlen(s) > 2);
    assert_string_equal("\r\n", &s[strlen(s)-2]);
    assert_non_null(strstr(s, "Transfer-Encoding: chunked\r\n"));

    close(fd);
}

static void test__http_end_headers__does_not_set_chunked_flag_in_response_if_content_length_is_nonzero(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request = {
        .fd = fd,
        .method = HTTP_METHOD_GET,
        .host = "www.example.com",
        .path = "/",
        .query = "a=1",
        .port = 80,
        .flags = 0,
        .content_length = 1,
    };

    http_end_header(&request);
    assert_string_equal("\r\n", get_file_content(fd));
    assert_false(request.flags & HTTP_FLAG_CHUNKED);

    close(fd);
}

static void test__http_set_content_length__sets_variable_and_sends_header(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request = {
        .fd = fd,
        .content_length = -1,
    };

    http_set_content_length(&request, 10);
    assert_int_equal(request.content_length, 10);
    assert_string_equal("Content-Length: 10\r\n", get_file_content(fd));

    close(fd);
}

static void test__http_write_string__writes_the_string_and_returns_its_length_with_te_identity(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request = {
        .fd = fd,
    };

    const char *s = "test";

    int ret = http_write_string(&request, s);
    assert_int_equal(strlen(s), ret);
    assert_string_equal(s, get_file_content(fd));

    close(fd);
}

static void test__http_write_string__writes_the_string_and_returns_its_length_with_te_chunked(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request = {
        .fd = fd,
        .flags = HTTP_FLAG_CHUNKED,
    };

    const char *s = "test";

    int ret = http_write_string(&request, s);
    assert_int_equal(strlen(s), ret);


    char expected[64];
    snprintf(expected, sizeof(expected), "%lX\r\n%s\r\n", strlen(s), s);
    assert_string_equal(expected, get_file_content(fd));

    close(fd);
}


// Main ////////////////////////////////////////////////////////////////////////

const struct CMUnitTest tests_for_http_io[] = {
    cmocka_unit_test(test__http_getc__can_read_correctly_with_te_identity),
    cmocka_unit_test(test__http_getc__can_read_correctly_with_te_chunked),
    cmocka_unit_test(test__http_getc__doesnt_read_more_than_content_length_with_te_identity),

    cmocka_unit_test(test__http_getc__can_read_non_ascii_characters_with_te_identity),
    cmocka_unit_test(test__http_getc__can_read_non_ascii_characters_with_te_chunked),

    cmocka_unit_test(test__http_getc__returns_zero_when_reading_eof_with_te_identity),
    cmocka_unit_test(test__http_getc__returns_zero_when_reading_eof_with_te_chunked),

    cmocka_unit_test(test__http_getc__returns_zero_if_state_is_not_http_read_body_with_te_identity),
    cmocka_unit_test(test__http_getc__returns_zero_if_state_is_not_http_read_body_with_te_chunked),

    cmocka_unit_test(test__http_getc__read_to_end_of_file_with_te_chunked),

    cmocka_unit_test(test__http_peek__returns_the_next_character_with_te_identity),
    cmocka_unit_test(test__http_peek__returns_the_next_character_with_te_chunked),

    cmocka_unit_test(test__http_write_header__writes_the_header),
    cmocka_unit_test(test__http_write_header__writes_nothing_when_name_is_null),
    cmocka_unit_test(test__http_write_header__writes_nothing_when_value_is_null),

    cmocka_unit_test(test__http_begin_request__writes_the_request_line_without_query),
    cmocka_unit_test(test__http_begin_request__writes_the_request_line_with_query),
    cmocka_unit_test(test__http_begin_request__writes_the_request_line_with_empty_query),
    cmocka_unit_test(test__http_begin_request__sends_host_header),
    cmocka_unit_test(test__http_begin_request__sends_host_header_with_port_if_port_is_not_80),
    cmocka_unit_test(test__http_begin_request__has_GET_as_default_method),
    cmocka_unit_test(test__http_begin_request__writes_the_POST_request_line),
    cmocka_unit_test(test__http_begin_request__sends_user_agent_header),

    cmocka_unit_test(test__http_end_headers__sends_new_line_in_request),
    cmocka_unit_test(test__http_end_headers__does_not_change_chunked_flag_in_request),
    cmocka_unit_test(test__http_end_headers__sets_chunked_flag_in_response_if_no_content_length),
    cmocka_unit_test(test__http_end_headers__does_not_set_chunked_flag_in_response_if_content_length_is_nonzero),

    cmocka_unit_test(test__http_set_content_length__sets_variable_and_sends_header),

    cmocka_unit_test(test__http_write_string__writes_the_string_and_returns_its_length_with_te_identity),
    cmocka_unit_test(test__http_write_string__writes_the_string_and_returns_its_length_with_te_chunked),
};

int main(void)
{
    return cmocka_run_group_tests(tests_for_http_io, NULL, NULL);
}

// Support functions for testing reads /////////////////////////////////////////

static int open_tmp_file(void)
{
    char filename[] = "/tmp/http-test-XXXXXX";
    int fd = mkstemp(filename);

    if(fd < 0) {
        perror("mkstemp");
    } else {
        unlink(filename);
    }
    return fd;
}

static int write_string(int fd, const char *s)
{
    int num = 0;
    int len = strlen(s);

    while(num < len) {
        int n = write(fd, s, len);
        if(n < 0) {
            perror("write");
            break;
        }
        num += n;
        s += n;
    }
    return num;
}

static int open_socket(pid_t *pid)
{
    int fds[2];
    if(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
        perror("socketpair");
        return -1;
    }

    *pid = fork();

    if(*pid < 0) {
        perror("fork");
        return -1;
    }

    if(*pid > 0) {
        close(fds[1]);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 1000;
        setsockopt(fds[0], SOL_SOCKET, SO_RCVTIMEO, (struct timeval *)&tv, sizeof(struct timeval));
        return fds[0];

    } else {
        close(fds[0]);
        return fds[1];
    }
}

static void close_socket(int fd)
{
    kill(child_pid, SIGINT);

    int status;
    if(waitpid(child_pid, &status, 0) < 0) {
        perror("wait");
    }

    close(fd);
}

static int write_socket(const char *str)
{
    int fd = open_socket(&child_pid);

    if(!child_pid) {
        write_string(fd, str);
        for(;;) {
        }
    }

    return fd;
}

static int write_socket_n(const char *s[])
{
    int fd = open_socket(&child_pid);

    if(!child_pid) {
        while(*s) {
            if(write_string(fd, *s++) < 0)
            {
                break;
            }
        }

        for(;;) {
        }
    }

    return fd;
}

static int write_tmp_file(const char *s)
{
    int fd = open_tmp_file();
    if(fd < 0) {
        return fd;
    }

    if(write_string(fd, s) < 0) {
        return -1;
    }

    if(lseek(fd, SEEK_SET, 0) != 0) {
        return -1;
    }

    return fd;
}

static int write_tmp_file_n(const char *s[])
{
    int fd = open_tmp_file();
    if(fd < 0) {
        return fd;
    }

    while(*s) {
        if(write_string(fd, *s++) < 0)
        {
            return -1;
        }
    }

    if(lseek(fd, SEEK_SET, 0) != 0) {
        return -1;
    }

    return fd;
}

// Support functions for testing writes ////////////////////////////////////////

static const char *get_file_content(int fd)
{
    static char buf[4096];

    off_t off;
    if((off = lseek(fd, 0, SEEK_SET)) < 0) {
        perror("lseek");
        return 0;
    }

    int n;

    if((n = read(fd, buf, sizeof(buf) - 1)) < 0) {
        perror("read");
        return 0;
    }

    buf[n] = 0;

    if(lseek(fd, off, SEEK_SET) < 0) {
        perror("lseek");
        return 0;
    }

    return buf;
}

// Support functions for test setup ////////////////////////////////////////////

static void init_request(struct http_request *request, int fd)
{
    memset(request, 0, sizeof(*request));
    request->fd = fd;
    request->poke = -1;
    request->state = HTTP_STATE_READ_BODY;
}
