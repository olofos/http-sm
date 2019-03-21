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

#include "http-sm/http.h"
#include "http-private.h"

#include "test-util.h"

static void init_server_request(struct http_request *request, int fd);
static void init_server_request_write_chunked(struct http_request *request, int fd);
static void init_client_request(struct http_request *request, int fd);

// Mocks ///////////////////////////////////////////////////////////////////////

void websocket_flush(struct websocket_connection *conn)
{
}

// Helpers /////////////////////////////////////////////////////////////////////

static void parse_header_helper(struct websocket_connection *conn, const uint8_t *s, int n)
{
    for(int i = 0; i < n; i++) {
        websocket_parse_frame_header(conn, s[i]);
    }
}

void parse_header_fd_helper(struct websocket_connection *conn)
{
    while(conn->state != WEBSOCKET_STATE_BODY) {
        char c;
        int ret = read(conn->fd, &c, 1);
        if(ret <= 0) {
            break;
        }
        websocket_parse_frame_header(conn, c);
    }
}

// Tests ///////////////////////////////////////////////////////////////////////

static void test__http_getc__can_read_correctly_with_te_identity(void **states)
{
    char *str = "0123";

    int fd = write_tmp_file(str);
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.read_content_length = strlen(str);

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
    init_server_request(&request, fd);
    request.read_content_length = 1;

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
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_READ_CHUNKED;

    request.read_content_length = strlen(str);

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

static void test__http_getc__can_read_correctly_te_chunked_and_chunk_extension(void **states)
{
    char *str = "1234";

    const char *s[] = {
        "4;a=b\r\n",
        str,
        "\r\n0;x=y\r\n",
        0
    };

    int fd = write_tmp_file_n(s);
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_READ_CHUNKED;

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
    init_server_request(&request, fd);
    request.read_content_length = strlen(str);

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
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_READ_CHUNKED;

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
    init_server_request(&request, fd);
    request.read_content_length = strlen(str);

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
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_READ_CHUNKED;

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
    init_server_request(&request, fd);
    request.read_content_length = strlen(str);

    enum http_state all_other_states[] = {
        HTTP_STATE_IDLE,
        HTTP_STATE_ERROR,
        HTTP_STATE_SERVER_READ_METHOD,
        HTTP_STATE_SERVER_READ_PATH,
        HTTP_STATE_SERVER_READ_QUERY,
        HTTP_STATE_SERVER_READ_VERSION,
        HTTP_STATE_SERVER_READ_HEADER,
        HTTP_STATE_CLIENT_READ_VERSION,
        HTTP_STATE_CLIENT_READ_STATUS,
        HTTP_STATE_CLIENT_READ_STATUS_DESC,
        HTTP_STATE_CLIENT_READ_HEADER,
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
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_READ_CHUNKED;

    enum http_state all_other_states[] = {
        HTTP_STATE_IDLE,
        HTTP_STATE_ERROR,
        HTTP_STATE_SERVER_READ_METHOD,
        HTTP_STATE_SERVER_READ_PATH,
        HTTP_STATE_SERVER_READ_QUERY,
        HTTP_STATE_SERVER_READ_VERSION,
        HTTP_STATE_SERVER_READ_HEADER,
        HTTP_STATE_CLIENT_READ_VERSION,
        HTTP_STATE_CLIENT_READ_STATUS,
        HTTP_STATE_CLIENT_READ_STATUS_DESC,
        HTTP_STATE_CLIENT_READ_HEADER,
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
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_READ_CHUNKED;

    int c;
    while((c = http_getc(&request)) > 0) {
    }
    assert_int_equal(0, c);

    off_t pos = lseek(request.fd, 0, SEEK_CUR);
    off_t end = lseek(request.fd, 0, SEEK_END);

    assert_int_equal(end, pos);

    close(fd);
}

static void test__http_getc__returns_zero_if_eof_is_found_when_content_length_is_not_zero(void **states)
{
    const char *s = "012";

    int fd = write_tmp_file(s);
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.read_content_length = strlen(s) + 1;

    int c;
    while((c = http_getc(&request)) > 0) {
    }
    assert_int_equal(0, c);

    off_t pos = lseek(request.fd, 0, SEEK_CUR);
    off_t end = lseek(request.fd, 0, SEEK_END);

    assert_int_equal(end, pos);

    close(fd);
}

static void test__http_getc__returns_zero_if_eof_is_found_in_chunk_header(void **states)
{
    const char *s =
        "4\r\n"
        "0123"
        "\r\n"
        "4";

    int fd = write_tmp_file(s);
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_READ_CHUNKED;

    int c;
    while((c = http_getc(&request)) > 0) {
    }
    assert_int_equal(0, c);

    off_t pos = lseek(request.fd, 0, SEEK_CUR);
    off_t end = lseek(request.fd, 0, SEEK_END);

    assert_int_equal(end, pos);

    close(fd);
}

static void test__http_getc__returns_zero_if_eof_is_found_after_chunk_header(void **states)
{
    const char *s =
        "4\r\n"
        "0123"
        "\r\n"
        "4\r";

    int fd = write_tmp_file(s);
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_READ_CHUNKED;

    int c;
    while((c = http_getc(&request)) > 0) {
    }
    assert_int_equal(0, c);

    off_t pos = lseek(request.fd, 0, SEEK_CUR);
    off_t end = lseek(request.fd, 0, SEEK_END);

    assert_int_equal(end, pos);

    close(fd);
}

static void test__http_getc__returns_zero_if_eof_is_found_in_chunk_footer(void **states)
{
    const char *s =
        "4\r\n"
        "0123"
        "\r\n"
        "0";


    int fd = write_tmp_file(s);
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_READ_CHUNKED;

    int c;
    while((c = http_getc(&request)) > 0) {
    }
    assert_int_equal(0, c);

    off_t pos = lseek(request.fd, 0, SEEK_CUR);
    off_t end = lseek(request.fd, 0, SEEK_END);

    assert_int_equal(end, pos);

    close(fd);
}

static void test__http_getc__returns_zero_if_missing_chunk_footer(void **states)
{
    const char *s =
        "4\r\n"
        "0123";


    int fd = write_tmp_file(s);
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_READ_CHUNKED;

    int c;
    while((c = http_getc(&request)) > 0) {
    }
    assert_int_equal(0, c);

    off_t pos = lseek(request.fd, 0, SEEK_CUR);
    off_t end = lseek(request.fd, 0, SEEK_END);

    assert_int_equal(end, pos);

    close(fd);
}

static void test__http_getc__returns_zero_if_extra_characters_are_found_in_chunk_footer(void **states)
{
    const char *s =
        "4\r\n"
        "0123"
        "\r\n"
        "0\r\n"
        "XX";


    int fd = write_tmp_file(s);
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_READ_CHUNKED;

    int c;
    while((c = http_getc(&request)) > 0) {
    }
    assert_int_equal(0, c);

    off_t pos = lseek(request.fd, 0, SEEK_CUR);
    off_t end = lseek(request.fd, 0, SEEK_END);

    assert_int_equal(end, pos);

    close(fd);
}

static void test__http_peek__returns_the_next_character_with_te_identity(void **states)
{
    char *str = "0123";

    int fd = write_tmp_file(str);
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.read_content_length = strlen(str);

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
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_READ_CHUNKED;

    assert_int_equal('0', http_peek(&request));
    assert_int_equal('0', http_peek(&request));
    assert_int_equal('0', http_getc(&request));
    assert_int_equal('1', http_getc(&request));
    assert_int_equal('2', http_peek(&request));
    assert_int_equal('2', http_peek(&request));
    assert_int_equal('2', http_getc(&request));

    close(fd);
}



static void test__http_read__can_read_correctly_with_te_identity(void **states)
{
    char *str = "0123";

    int fd = write_tmp_file(str);
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.read_content_length = strlen(str);

    char buf[5] = "XXXX";

    int n = http_read(&request, buf, 4);

    assert_string_equal(str, buf);
    assert_int_equal(4, n);

    close(fd);
}

static void test__http_read__can_read_correctly_with_te_chunked(void **states)
{
    char *str = "0123";
    const char *s[] = {
        "4\r\n",
        str,
        "\r\n",
        "0\r\n",
        0
    };

    int fd = write_tmp_file_n(s);
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_READ_CHUNKED;

    char buf[5] = "XXXX";

    int n = http_read(&request, buf, 4);
    assert_int_equal(4, n);
    assert_string_equal(str, buf);

    close(fd);
}

static void test__http_read__stops_reading_at_end_of_file_with_te_identity(void **states)
{
    const char *s = "0123";

    int fd = write_tmp_file(s);
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.read_content_length = strlen(s);

    char buf[6] = "XXXX";

    int n = http_read(&request, buf, 5);

    assert_int_equal(4, n);
    assert_string_equal(s, buf);

    off_t pos = lseek(request.fd, 0, SEEK_CUR);
    off_t end = lseek(request.fd, 0, SEEK_END);

    assert_int_equal(end, pos);

    close(fd);
}

static void test__http_read__stops_reading_at_end_of_file_with_te_chunked(void **states)
{
    char *str = "0123";
    const char *s[] = {
        "4\r\n",
        str,
        "\r\n",
        "0\r\n",
        0
    };

    int fd = write_tmp_file_n(s);
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_READ_CHUNKED;

    char buf[6] = "XXXX";

    int n = http_read(&request, buf, 5);

    assert_int_equal(4, n);
    assert_string_equal(str, buf);

    off_t pos = lseek(request.fd, 0, SEEK_CUR);
    off_t end = lseek(request.fd, 0, SEEK_END);

    assert_int_equal(end, pos);

    close(fd);
}

static void test__http_read__returns_zero_at_end_of_file_with_te_identity(void **states)
{
    const char *s = "0123";

    int fd = write_tmp_file(s);
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.read_content_length = strlen(s);

    char buf[5] = "XXXX";

    int n = http_read(&request, buf, 4);

    assert_int_equal(4, n);
    assert_string_equal(s, buf);

    n = http_read(&request, buf, 4);
    assert_int_equal(0, n);

    off_t pos = lseek(request.fd, 0, SEEK_CUR);
    off_t end = lseek(request.fd, 0, SEEK_END);

    assert_int_equal(end, pos);

    close(fd);
}

static void test__http_read__returns_zero_at_end_of_file_with_te_chunked(void **states)
{
    const char *str = "0123";

    const char *s[] = {
        "4\r\n",
        str,
        "\r\n",
        "0\r\n",
        0
    };


    int fd = write_tmp_file_n(s);
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_READ_CHUNKED;

    char buf[5] = "XXXX";

    int n = http_read(&request, buf, 4);

    assert_int_equal(4, n);
    assert_string_equal(str, buf);

    n = http_read(&request, buf, 4);
    assert_int_equal(0, n);

    off_t pos = lseek(request.fd, 0, SEEK_CUR);
    off_t end = lseek(request.fd, 0, SEEK_END);

    assert_int_equal(end, pos);

    close(fd);
}

static void test__http_read__doesnt_read_more_than_content_length_te_identity(void **states)
{
    char *str = "0123";

    int fd = write_tmp_file(str);
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.read_content_length = strlen(str)-1;

    char buf[5] = "XXXX";

    int n = http_read(&request, buf, 4);

    assert_int_equal(str[0], buf[0]);
    assert_int_equal(str[1], buf[1]);
    assert_int_equal(str[2], buf[2]);
    assert_int_equal(3, n);

    close(fd);
}


static void test__http_write_header__writes_the_header(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);

    http_write_header(&request, "Connection", "close");
    assert_string_equal("Connection: close\r\n", get_file_content(fd));

    close(fd);
}

static void test__http_write_header__writes_nothing_when_name_is_null(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);

    http_write_header(&request, NULL, "close");
    assert_string_equal("", get_file_content(fd));

    close(fd);
}

static void test__http_write_header__writes_nothing_when_value_is_null(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);

    http_write_header(&request, "Test", NULL);
    assert_string_equal("", get_file_content(fd));

    close(fd);
}

static void test__http_begin_request__writes_the_request_line_without_query(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_client_request(&request, fd);
    request.path = "/";
    request.query = 0;

    http_begin_request(&request);
    assert_string_prefix_equal("GET / HTTP/1.1\r\n", get_file_content(fd));

    close(fd);
}

static void test__http_begin_request__writes_the_request_line_with_query(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_client_request(&request, fd);
    request.path = "/";
    request.query = "a=1";

    http_begin_request(&request);
    assert_string_prefix_equal("GET /?a=1 HTTP/1.1\r\n", get_file_content(fd));

    close(fd);
}

static void test__http_begin_request__writes_the_request_line_with_empty_query(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_client_request(&request, fd);
    request.path = "/";
    request.query = "";

    http_begin_request(&request);
    assert_string_prefix_equal("GET / HTTP/1.1\r\n", get_file_content(fd));

    close(fd);
}

static void test__http_begin_request__sends_host_header(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_client_request(&request, fd);
    request.host = "www.example.com";

    http_begin_request(&request);
    assert_string_contains_substring("Host: www.example.com\r\n", get_file_content(fd));

    close(fd);
}

static void test__http_begin_request__sends_host_header_with_port_if_port_is_not_80(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_client_request(&request, fd);
    request.host = "www.example.com";
    request.port = 8080;

    http_begin_request(&request);
    assert_string_contains_substring("Host: www.example.com:8080\r\n", get_file_content(fd));

    close(fd);
}

static void test__http_begin_request__has_GET_as_default_method(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_client_request(&request, fd);
    request.path = "/";
    request.method = 0;

    http_begin_request(&request);
    assert_string_prefix_equal("GET / HTTP/1.1\r\n", get_file_content(fd));

    close(fd);
}

static void test__http_begin_request__writes_the_POST_request_line(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_client_request(&request, fd);
    request.path = "/";
    request.method = HTTP_METHOD_POST;

    http_begin_request(&request);
    assert_string_prefix_equal("POST / HTTP/1.1\r\n", get_file_content(fd));

    close(fd);
}

static void test__http_begin_request__sends_user_agent_header(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_client_request(&request, fd);

    http_begin_request(&request);
    assert_string_contains_substring("User-Agent: ", get_file_content(fd));

    close(fd);
}


static void test__http_end_headers__sends_new_line_if_client(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_client_request(&request, fd);

    http_end_header(&request);
    assert_string_equal("\r\n", get_file_content(fd));
    assert_false(request.flags & HTTP_FLAG_WRITE_CHUNKED);

    close(fd);
}

static void test__http_end_headers__does_not_change_chunked_flag_if_client(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_client_request(&request, fd);
    request.flags |= HTTP_FLAG_WRITE_CHUNKED;

    http_end_header(&request);
    assert_string_equal("\r\n", get_file_content(fd));
    assert_true(request.flags & HTTP_FLAG_WRITE_CHUNKED);

    close(fd);
}

static void test__http_end_headers__server_sets_chunked_flag_if_no_content_length(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.write_content_length = -1;

    http_end_header(&request);
    assert_true(request.flags & HTTP_FLAG_WRITE_CHUNKED);

    const char *s = get_file_content(fd);
    assert_true(strlen(s) > 2);
    assert_string_equal("\r\n", &s[strlen(s)-2]);
    assert_non_null(strstr(s, "Transfer-Encoding: chunked\r\n"));

    http_end_body(&request);

    close(fd);
}

static void test__http_end_headers__server_does_not_set_chunked_flag_if_content_length_is_nonzero(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.write_content_length = 1;

    http_end_header(&request);
    assert_string_equal("\r\n", get_file_content(fd));
    assert_false(request.flags & HTTP_FLAG_WRITE_CHUNKED);

    close(fd);
}

static void test__http_end_headers__server_does_not_set_chunked_flag_if_content_length_is_zero(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.write_content_length = 0;

    http_end_header(&request);
    assert_string_equal("\r\n", get_file_content(fd));
    assert_false(request.flags & HTTP_FLAG_WRITE_CHUNKED);

    close(fd);
}

static void test__http_set_content_length__sets_variable_and_sends_header(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.write_content_length = -1;

    http_set_content_length(&request, 10);
    assert_int_equal(request.write_content_length, 10);
    assert_string_equal("Content-Length: 10\r\n", get_file_content(fd));

    close(fd);
}

static void test__http_set_content_length__does_not_send_header_if_zero(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.write_content_length = -1;

    http_set_content_length(&request, 0);
    assert_int_equal(request.write_content_length, 0);
    assert_string_equal("", get_file_content(fd));

    close(fd);
}

static void test__http_write_string__writes_the_string_and_returns_its_length_with_te_identity(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);

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

    struct http_request request;
    init_server_request_write_chunked(&request, fd);

    const char *s = "test";

    int ret = http_write_string(&request, s);
    http_end_body(&request);

    assert_int_equal(strlen(s), ret);
    assert_non_null(get_file_content_chunked(fd));
    assert_string_equal(s, get_file_content_chunked(fd));


    close(fd);
}

static void test__http_write_string__writes_the_string_and_returns_its_length_with_long_string_and_te_chunked(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request_write_chunked(&request, fd);

    char s[256];

    for(int i = 0; i < sizeof(s)-1; i++) {
        s[i] = 'A' + (i % 26);
    }
    s[sizeof(s) - 1] = 0;

    int ret = http_write_string(&request, s);
    http_end_body(&request);

    assert_int_equal(strlen(s), ret);
    assert_non_null(get_file_content_chunked(fd));
    assert_string_equal(s, get_file_content_chunked(fd));

    close(fd);
}

static void test__http_write_string__writes_the_string_and_returns_its_length_with_multiple_calls_and_te_chunked(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request_write_chunked(&request, fd);

    const char *s = "test";

    int ret1 = http_write_string(&request, "te");
    int ret2 = http_write_string(&request, "st");
    http_end_body(&request);

    assert_int_equal(2, ret1);
    assert_int_equal(2, ret2);
    assert_non_null(get_file_content_chunked(fd));
    assert_string_equal(s, get_file_content_chunked(fd));


    close(fd);
}

static void test__http_write_string__writes_nothing_for_empty_string_with_te_chunked(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request_write_chunked(&request, fd);

    int ret = http_write_string(&request, "");

    assert_int_equal(0, ret);
    assert_non_null(get_file_content(fd));
    assert_string_equal("", get_file_content(fd));
    assert_int_equal(0, request.chunk_length);

    http_end_body(&request);

    close(fd);
}

static void test__http_write_bytes__writes_the_data_and_returns_the_length_with_te_identity(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);

    const char s[] = {'t', 'e', 0, 't'};

    int ret = http_write_bytes(&request, s, sizeof(s));
    assert_int_equal(sizeof(s), ret);
    assert_int_equal(sizeof(s), lseek(fd, 0, SEEK_END));
    assert_memory_equal(s, get_file_content(fd), sizeof(s));

    close(fd);
}

static void test__http_write_bytes__writes_the_data_and_returns_the_length_with_te_chunked(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request_write_chunked(&request, fd);
    const char s[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    };

    int ret = http_write_bytes(&request, s, sizeof(s));
    http_end_body(&request);

    assert_int_equal(sizeof(s), ret);
    assert_non_null(get_file_content_chunked(fd));
    assert_memory_equal(s, get_file_content_chunked(fd), sizeof(s));

    close(fd);
}

static void test__http_write_bytes__writes_nothing_for_empty_data_with_te_chunked(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request_write_chunked(&request, fd);
    int ret = http_write_bytes(&request, "", 0);
    http_end_body(&request);

    assert_int_equal(0, ret);
    assert_string_equal("", get_file_content_chunked(fd));

    close(fd);
}

static void test__http_end_body__writes_chunk_end_with_te_chunked(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request_write_chunked(&request, fd);
    http_end_body(&request);

    assert_string_equal("0\r\n\r\n", get_file_content(fd));

    close(fd);
}

static void test__websocket_send_response__writes_response_without_sec_websocket_key(void **states)
{
    const char *expected = ""
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "\r\n";

    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.flags = HTTP_FLAG_WEBSOCKET;
    request.websocket_key = 0;

    websocket_send_response(&request);

    assert_string_equal(expected, get_file_content(fd));

    close(fd);
}

static void test__websocket_send_response__writes_response_with_sec_websocket_key(void **states)
{
    const char *expected = ""
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
        "\r\n";

    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.flags = HTTP_FLAG_WEBSOCKET;
    request.websocket_key = "dGhlIHNhbXBsZSBub25jZQ==";

    websocket_send_response(&request);

    assert_string_equal(expected, get_file_content(fd));

    close(fd);
}


static void test__websocket_read__can_read_without_mask(void **states)
{
    const char frame[] = { 0x81, 0x03, 'a', 'b', 'c' };

    int fd = write_tmp_file_bin(frame, sizeof(frame));

    struct websocket_connection conn = {
        .fd = fd,
    };

    parse_header_fd_helper(&conn);

    char buf[4];
    int n = websocket_read(&conn, buf, 3);
    buf[3] = 0;

    assert_int_equal(n, 3);
    assert_string_equal(buf, "abc");
    assert_int_equal(conn.state, WEBSOCKET_STATE_DONE);

    close(fd);
}

static void test__websocket_read__can_read_with_mask(void **states)
{
    const char frame[] = { 0x81, 0x83, 'a', 'b', 'c', 'd', 'X' ^ 'a', 'Y' ^ 'b', 'Z' ^ 'c' };

    int fd = write_tmp_file_bin(frame, sizeof(frame));

    struct websocket_connection conn = {
        .fd = fd,
    };

    parse_header_fd_helper(&conn);

    char buf[4];
    int n = websocket_read(&conn, buf, 3);
    buf[3] = 0;

    assert_int_equal(n, 3);
    assert_string_equal(buf, "XYZ");
    assert_int_equal(conn.state, WEBSOCKET_STATE_DONE);

    close(fd);
}

static void test__websocket_read__can_read_several_times(void **states)
{
    const char frame[] = { 0x81, 0x88, 'a', 'b', 'c', 'd', 'a' ^ 'a', 'b' ^ 'b', 'c' ^ 'c', 'd' ^ 'd', 'e' ^ 'a', 'f' ^ 'b', 'g' ^ 'c', 'h' ^ 'd' };

    int fd = write_tmp_file_bin(frame, sizeof(frame));

    struct websocket_connection conn = {
        .fd = fd,
    };

    parse_header_fd_helper(&conn);

    char buf[5];
    int n;

    n = websocket_read(&conn, buf, 4);
    buf[4] = 0;

    assert_int_equal(n, 4);
    assert_string_equal(buf, "abcd");
    assert_int_equal(conn.state, WEBSOCKET_STATE_BODY);

    n = websocket_read(&conn, buf, 4);
    buf[4] = 0;

    assert_int_equal(n, 4);
    assert_string_equal(buf, "efgh");
    assert_int_equal(conn.state, WEBSOCKET_STATE_DONE);

    close(fd);
}

static void test__websocket_read__does_not_read_more_than_available(void **states)
{
    const char frame[] = { 0x81, 0x88, 'a', 'b', 'c', 'd', 'a' ^ 'a', 'b' ^ 'b', 'c' ^ 'c', 'd' ^ 'd', 'e' ^ 'a', 'f' ^ 'b', 'g' ^ 'c', 'h' ^ 'd'  };

    int fd = write_tmp_file_bin(frame, sizeof(frame));

    struct websocket_connection conn = {
        .fd = fd,
    };

    parse_header_fd_helper(&conn);

    char buf[9];
    int n;

    n = websocket_read(&conn, buf, 32);
    buf[n] = 0;

    assert_int_equal(n, 8);
    assert_string_equal(buf, "abcdefgh");
    assert_int_equal(conn.state, WEBSOCKET_STATE_DONE);

    close(fd);
}


static void test__websocket_read__does_not_read_more_than_frame_length(void **states)
{
    const char frame[] = { 0x81, 0x88, 'a', 'b', 'c', 'd', 'a' ^ 'a', 'b' ^ 'b', 'c' ^ 'c', 'd' ^ 'd', 'e' ^ 'a', 'f' ^ 'b', 'g' ^ 'c', 'h' ^ 'd', 'X', 'Y', 'Z' };

    int fd = write_tmp_file_bin(frame, sizeof(frame));

    struct websocket_connection conn = {
        .fd = fd,
    };

    parse_header_fd_helper(&conn);

    char buf[9];
    int n;

    n = websocket_read(&conn, buf, 32);
    buf[n] = 0;

    off_t pos = lseek(conn.fd, 0, SEEK_CUR);

    assert_int_equal(n, 8);
    assert_string_equal(buf, "abcdefgh");
    assert_int_equal(conn.state, WEBSOCKET_STATE_DONE);
    assert_int_equal(pos, 8+6);

    n = read(conn.fd, buf, 8);
    buf[n] = 0;
    assert_string_equal(buf, "XYZ");

    close(fd);
}

static void test__websocket_read__returns_minus_one_if_not_reading_body(void **states)
{
    struct websocket_connection conn = {
        .fd = 3,
    };

    char buf[32];

    conn.state = WEBSOCKET_STATE_OPCODE;
    assert_int_equal(websocket_read(&conn, buf, 32), -1);

    conn.state = WEBSOCKET_STATE_LEN8;
    assert_int_equal(websocket_read(&conn, buf, 32), -1);

    conn.state = WEBSOCKET_STATE_MASK_0;
    assert_int_equal(websocket_read(&conn, buf, 32), -1);

    conn.state = WEBSOCKET_STATE_DONE;
    assert_int_equal(websocket_read(&conn, buf, 32), -1);
}


static void test__websocket_send__sends_a_simple_message(void **states)
{
    int fd = open_tmp_file();
    assert_true(fd >= 0);

    struct websocket_connection conn = {
        .fd = fd,
    };

    char str[] = "abcd";

    assert_in_range(strlen(str), 1, 126);

    int n = websocket_send(&conn, str, strlen(str), WEBSOCKET_FRAME_OPCODE_TEXT);
    char expected[] = { 0x81, 0x04, 'a', 'b', 'c', 'd', 0 };

    assert_int_equal(n, strlen(str));
    assert_string_equal(expected, get_file_content(fd));

    close(fd);
}

static void test__websocket_send__sends_a_16bit_message(void **states)
{
    int fd = open_tmp_file();
    assert_true(fd >= 0);

    struct websocket_connection conn = {
        .fd = fd,
    };

    char str[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

    assert_in_range(strlen(str), 127, 65535);

    int n = websocket_send(&conn, str, strlen(str), WEBSOCKET_FRAME_OPCODE_TEXT);
    assert_int_equal(n, strlen(str));

    lseek(conn.fd, 0, SEEK_SET);

    parse_header_fd_helper(&conn);

    assert_int_equal(conn.frame_opcode, WEBSOCKET_FRAME_OPCODE_TEXT | WEBSOCKET_FRAME_FIN);
    assert_int_equal(conn.frame_length, strlen(str));

    char *buf = malloc(conn.frame_length+1);
    buf[conn.frame_length] = 0;

    websocket_read(&conn, buf, conn.frame_length);

    assert_string_equal(buf, str);

    free(buf);
    close(fd);
}

static void test__websocket_send__sends_a_64bit_message(void **states)
{
    int fd = open_tmp_file();
    assert_true(fd >= 0);

    struct websocket_connection conn = {
        .fd = fd,
    };

    const int size = 131052;

    char *str = malloc(size + 1);

    for(int i = 0; i < size; i++) {
        str[i] = 'A' + (i % 26);
    }
    str[size] = 0;

    assert_true(strlen(str) > 65535);

    int n = websocket_send(&conn, str, strlen(str), WEBSOCKET_FRAME_OPCODE_TEXT);
    assert_int_equal(n, strlen(str));

    lseek(conn.fd, 0, SEEK_SET);

    parse_header_fd_helper(&conn);

    assert_int_equal(conn.frame_opcode, WEBSOCKET_FRAME_OPCODE_TEXT | WEBSOCKET_FRAME_FIN);
    assert_int_equal(conn.frame_length, strlen(str));

    char *buf = malloc(conn.frame_length+1);
    buf[conn.frame_length] = 0;

    websocket_read(&conn, buf, conn.frame_length);

    assert_string_equal(buf, str);

    free(str);
    free(buf);
    close(fd);
}

static void test__websocket_parse_frame_header__can_read_opcode(void **states)
{
    struct websocket_connection conn = {
        .state = WEBSOCKET_STATE_OPCODE,
    };

    websocket_parse_frame_header(&conn, 0x81);

    assert_int_equal(conn.frame_opcode, 0x81);
    assert_int_equal(conn.state, WEBSOCKET_STATE_LEN8);
}

static void test__websocket_parse_frame_header__can_read_len8_no_mask(void **states)
{
    struct websocket_connection conn = {
        .state = WEBSOCKET_STATE_OPCODE,
        .frame_mask = {0xBA, 0xDC, 0x0F, 0xFE},
        .frame_length = 0xDEADBEEFBADC0FFE,
    };

    const uint8_t input[] = {0x81, 0x05};

    parse_header_helper(&conn, input, sizeof(input));

    assert_int_equal(conn.frame_length, 5);
    assert_int_equal(conn.state, WEBSOCKET_STATE_BODY);
    assert_int_equal(conn.frame_mask[0], 0x00);
    assert_int_equal(conn.frame_mask[1], 0x00);
    assert_int_equal(conn.frame_mask[2], 0x00);
    assert_int_equal(conn.frame_mask[3], 0x00);
}

static void test__websocket_parse_frame_header__can_read_len8_mask(void **states)
{
    struct websocket_connection conn = {
        .state = WEBSOCKET_STATE_OPCODE,
        .frame_mask = {0xBA, 0xDC, 0x0F, 0xFE},
        .frame_length = 0xDEADBEEFBADC0FFE,
    };

    const uint8_t input[] = {0x81, 0x85, 0x01, 0x02, 0x03, 0x04};
    parse_header_helper(&conn, input, sizeof(input));

    assert_int_equal(conn.frame_length, 5);
    assert_int_equal(conn.state, WEBSOCKET_STATE_BODY);
    assert_int_equal(conn.frame_mask[0], 0x01);
    assert_int_equal(conn.frame_mask[1], 0x02);
    assert_int_equal(conn.frame_mask[2], 0x03);
    assert_int_equal(conn.frame_mask[3], 0x04);
}

static void test__websocket_parse_frame_header__can_read_len16_no_mask(void **states)
{
    struct websocket_connection conn = {
        .state = WEBSOCKET_STATE_OPCODE,
        .frame_mask = {0xBA, 0xDC, 0x0F, 0xFE},
        .frame_length = 0xDEADBEEFBADC0FFE,
    };

    const uint8_t input[] = {0x81, 0x7E, 0x12, 0x34};
    parse_header_helper(&conn, input, sizeof(input));

    assert_int_equal(conn.frame_length, 0x1234);
    assert_int_equal(conn.state, WEBSOCKET_STATE_BODY);
    assert_int_equal(conn.frame_mask[0], 0x00);
    assert_int_equal(conn.frame_mask[1], 0x00);
    assert_int_equal(conn.frame_mask[2], 0x00);
    assert_int_equal(conn.frame_mask[3], 0x00);
}

static void test__websocket_parse_frame_header__can_read_len16_mask(void **states)
{
    struct websocket_connection conn = {
        .state = WEBSOCKET_STATE_OPCODE,
        .frame_mask = {0xBA, 0xDC, 0x0F, 0xFE},
        .frame_length = 0xDEADBEEFBADC0FFE,
    };

    const uint8_t input[] = {0x81, 0xFE, 0x12, 0x34, 0x01, 0x02, 0x03, 0x04};
    parse_header_helper(&conn, input, sizeof(input));

    assert_int_equal(conn.frame_length, 0x1234);
    assert_int_equal(conn.state, WEBSOCKET_STATE_BODY);
    assert_int_equal(conn.frame_mask[0], 0x01);
    assert_int_equal(conn.frame_mask[1], 0x02);
    assert_int_equal(conn.frame_mask[2], 0x03);
    assert_int_equal(conn.frame_mask[3], 0x04);
}

static void test__websocket_parse_frame_header__can_read_len64_no_mask(void **states)
{
    struct websocket_connection conn = {
        .state = WEBSOCKET_STATE_OPCODE,
        .frame_mask = {0xBA, 0xDC, 0x0F, 0xFE},
        .frame_length = 0xDEADBEEFBADC0FFE,
    };

    const uint8_t input[] = {0x81, 0x7F, 0x12, 0x34, 0x56, 0x78, 0xAB, 0xCD, 0xEF, 0x01};
    parse_header_helper(&conn, input, sizeof(input));

    assert_int_equal(conn.frame_length, 0x12345678ABCDEF01);
    assert_int_equal(conn.state, WEBSOCKET_STATE_BODY);
    assert_int_equal(conn.frame_mask[0], 0x00);
    assert_int_equal(conn.frame_mask[1], 0x00);
    assert_int_equal(conn.frame_mask[2], 0x00);
    assert_int_equal(conn.frame_mask[3], 0x00);
}

static void test__websocket_parse_frame_header__can_read_len64_mask(void **states)
{
    struct websocket_connection conn = {
        .state = WEBSOCKET_STATE_OPCODE,
        .frame_mask = {0xBA, 0xDC, 0x0F, 0xFE},
        .frame_length = 0xDEADBEEFBADC0FFE,
    };

    const uint8_t input[] = {0x81, 0xFF, 0x12, 0x34, 0x56, 0x78, 0xAB, 0xCD, 0xEF, 0x01, 0x01, 0x02, 0x03, 0x04};
    parse_header_helper(&conn, input, sizeof(input));

    assert_int_equal(conn.frame_length, 0x12345678ABCDEF01);
    assert_int_equal(conn.state, WEBSOCKET_STATE_BODY);
    assert_int_equal(conn.frame_mask[0], 0x01);
    assert_int_equal(conn.frame_mask[1], 0x02);
    assert_int_equal(conn.frame_mask[2], 0x03);
    assert_int_equal(conn.frame_mask[3], 0x04);
}

static void test__websocket_parse_frame_header__sets_error_for_unknown_opcode(void **states)
{
    struct websocket_connection conn;

    const uint8_t opcode[] = {
        0x03, 0x04, 0x05, 0x06, 0x07, 0x0b, 0x0c, 0x0d,
        0x0e, 0x0f, 0x11, 0x12, 0x21, 0x30, 0x41, 0x52,
        0x61, 0x77, 0x84, 0x8f, 0x90, 0xa2, 0xb3, 0xc1,
        0xd0, 0xe4, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };

    for(int i = 0; i < sizeof(opcode); i++) {
        conn.state = WEBSOCKET_STATE_OPCODE;
        websocket_parse_frame_header(&conn, opcode[i]);
        assert_int_equal(conn.state, WEBSOCKET_STATE_ERROR);
    }
}


// Main ////////////////////////////////////////////////////////////////////////

const struct CMUnitTest tests_for_http_io[] = {
    cmocka_unit_test(test__http_getc__can_read_correctly_with_te_identity),
    cmocka_unit_test(test__http_getc__can_read_correctly_with_te_chunked),
    cmocka_unit_test(test__http_getc__can_read_correctly_te_chunked_and_chunk_extension),
    cmocka_unit_test(test__http_getc__doesnt_read_more_than_content_length_with_te_identity),

    cmocka_unit_test(test__http_getc__can_read_non_ascii_characters_with_te_identity),
    cmocka_unit_test(test__http_getc__can_read_non_ascii_characters_with_te_chunked),

    cmocka_unit_test(test__http_getc__returns_zero_when_reading_eof_with_te_identity),
    cmocka_unit_test(test__http_getc__returns_zero_when_reading_eof_with_te_chunked),

    cmocka_unit_test(test__http_getc__returns_zero_if_state_is_not_http_read_body_with_te_identity),
    cmocka_unit_test(test__http_getc__returns_zero_if_state_is_not_http_read_body_with_te_chunked),

    cmocka_unit_test(test__http_getc__read_to_end_of_file_with_te_chunked),
    cmocka_unit_test(test__http_getc__returns_zero_if_eof_is_found_when_content_length_is_not_zero),
    cmocka_unit_test(test__http_getc__returns_zero_if_eof_is_found_in_chunk_header),
    cmocka_unit_test(test__http_getc__returns_zero_if_eof_is_found_after_chunk_header),
    cmocka_unit_test(test__http_getc__returns_zero_if_eof_is_found_in_chunk_footer),
    cmocka_unit_test(test__http_getc__returns_zero_if_missing_chunk_footer),
    cmocka_unit_test(test__http_getc__returns_zero_if_extra_characters_are_found_in_chunk_footer),

    cmocka_unit_test(test__http_peek__returns_the_next_character_with_te_identity),
    cmocka_unit_test(test__http_peek__returns_the_next_character_with_te_chunked),

    cmocka_unit_test(test__http_read__can_read_correctly_with_te_identity),
    cmocka_unit_test(test__http_read__can_read_correctly_with_te_chunked),
    cmocka_unit_test(test__http_read__stops_reading_at_end_of_file_with_te_identity),
    cmocka_unit_test(test__http_read__stops_reading_at_end_of_file_with_te_chunked),
    cmocka_unit_test(test__http_read__returns_zero_at_end_of_file_with_te_identity),
    cmocka_unit_test(test__http_read__returns_zero_at_end_of_file_with_te_chunked),
    cmocka_unit_test(test__http_read__doesnt_read_more_than_content_length_te_identity),

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

    cmocka_unit_test(test__http_end_headers__sends_new_line_if_client),
    cmocka_unit_test(test__http_end_headers__does_not_change_chunked_flag_if_client),
    cmocka_unit_test(test__http_end_headers__server_sets_chunked_flag_if_no_content_length),
    cmocka_unit_test(test__http_end_headers__server_does_not_set_chunked_flag_if_content_length_is_nonzero),
    cmocka_unit_test(test__http_end_headers__server_does_not_set_chunked_flag_if_content_length_is_zero),

    cmocka_unit_test(test__http_set_content_length__sets_variable_and_sends_header),
    cmocka_unit_test(test__http_set_content_length__does_not_send_header_if_zero),

    cmocka_unit_test(test__http_write_string__writes_the_string_and_returns_its_length_with_te_identity),
    cmocka_unit_test(test__http_write_string__writes_the_string_and_returns_its_length_with_te_chunked),
    cmocka_unit_test(test__http_write_string__writes_the_string_and_returns_its_length_with_long_string_and_te_chunked),
    cmocka_unit_test(test__http_write_string__writes_the_string_and_returns_its_length_with_multiple_calls_and_te_chunked),
    cmocka_unit_test(test__http_write_string__writes_nothing_for_empty_string_with_te_chunked),

    cmocka_unit_test(test__http_write_bytes__writes_the_data_and_returns_the_length_with_te_identity),
    cmocka_unit_test(test__http_write_bytes__writes_the_data_and_returns_the_length_with_te_chunked),
    cmocka_unit_test(test__http_write_bytes__writes_nothing_for_empty_data_with_te_chunked),

    cmocka_unit_test(test__http_end_body__writes_chunk_end_with_te_chunked),

    cmocka_unit_test(test__websocket_send_response__writes_response_without_sec_websocket_key),
    cmocka_unit_test(test__websocket_send_response__writes_response_with_sec_websocket_key),

    cmocka_unit_test(test__websocket_read__can_read_without_mask),
    cmocka_unit_test(test__websocket_read__can_read_with_mask),
    cmocka_unit_test(test__websocket_read__can_read_several_times),
    cmocka_unit_test(test__websocket_read__does_not_read_more_than_available),
    cmocka_unit_test(test__websocket_read__does_not_read_more_than_frame_length),
    cmocka_unit_test(test__websocket_read__returns_minus_one_if_not_reading_body),

    cmocka_unit_test(test__websocket_send__sends_a_simple_message),
    cmocka_unit_test(test__websocket_send__sends_a_16bit_message),
    cmocka_unit_test(test__websocket_send__sends_a_64bit_message),

    cmocka_unit_test(test__websocket_parse_frame_header__can_read_opcode),
    cmocka_unit_test(test__websocket_parse_frame_header__can_read_len8_no_mask),
    cmocka_unit_test(test__websocket_parse_frame_header__can_read_len8_mask),
    cmocka_unit_test(test__websocket_parse_frame_header__can_read_len16_no_mask),
    cmocka_unit_test(test__websocket_parse_frame_header__can_read_len16_mask),
    cmocka_unit_test(test__websocket_parse_frame_header__can_read_len64_no_mask),
    cmocka_unit_test(test__websocket_parse_frame_header__can_read_len64_mask),
    cmocka_unit_test(test__websocket_parse_frame_header__sets_error_for_unknown_opcode),
};

int main(void)
{
    int fails = 0;
    fails += cmocka_run_group_tests(tests_for_http_io, NULL, NULL);

    return fails;
}

// Support functions for test setup ////////////////////////////////////////////

static void init_server_request(struct http_request *request, int fd)
{
    memset(request, 0, sizeof(*request));
    request->fd = fd;
    request->poke = -1;
    request->host = "www.example.com";
    request->path = "/";
    request->method = HTTP_METHOD_GET;
    request->state = HTTP_STATE_SERVER_READ_BODY;
}

static void init_server_request_write_chunked(struct http_request *request, int fd)
{
    init_server_request(request, fd);

    request->write_content_length = -1;

    http_end_header(request);

    lseek(request->fd, 0, SEEK_SET);
    ftruncate(request->fd, 0);
}

static void init_client_request(struct http_request *request, int fd)
{
    memset(request, 0, sizeof(*request));
    request->fd = fd;
    request->method = HTTP_METHOD_GET;
    request->host = "www.example.com";
    request->path = "/";
    request->query = 0;
    request->port = 80;
    request->state = HTTP_STATE_CLIENT_READ_BODY;
}
