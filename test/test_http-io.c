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
static void init_client_request(struct http_request *request, int fd);

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
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_WRITE_CHUNKED;

    const char *s = "test";

    int ret = http_write_string(&request, s);
    assert_int_equal(strlen(s), ret);


    char expected[64];
    snprintf(expected, sizeof(expected), "%lX\r\n%s\r\n", strlen(s), s);
    assert_string_equal(expected, get_file_content(fd));

    close(fd);
}

static void test__http_write_string__writes_nothing_for_empty_string_with_te_chunked(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_WRITE_CHUNKED;

    int ret = http_write_string(&request, "");
    assert_int_equal(0, ret);

    assert_string_equal("", get_file_content(fd));

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
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_WRITE_CHUNKED;

    const char s[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    };

    const char expected[] = {
        '1', '0', '\r', '\n',
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        '\r', '\n'};

    int ret = http_write_bytes(&request, s, sizeof(s));
    assert_int_equal(sizeof(s), ret);
    assert_int_equal(sizeof(expected), lseek(fd, 0, SEEK_END));
    assert_memory_equal(expected, get_file_content(fd), sizeof(expected));

    close(fd);
}

static void test__http_write_bytes__writes_nothing_for_empty_data_with_te_chunked(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_WRITE_CHUNKED;

    int ret = http_write_bytes(&request, "", 0);
    assert_int_equal(0, ret);

    assert_string_equal("", get_file_content(fd));

    close(fd);
}

static void test__http_end_body__writes_chunk_end_with_te_chunked(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request;
    init_server_request(&request, fd);
    request.flags |= HTTP_FLAG_WRITE_CHUNKED;

    http_end_body(&request);

    assert_string_equal("0\r\n\r\n", get_file_content(fd));

    close(fd);
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
    cmocka_unit_test(test__http_write_string__writes_nothing_for_empty_string_with_te_chunked),

    cmocka_unit_test(test__http_write_bytes__writes_the_data_and_returns_the_length_with_te_identity),
    cmocka_unit_test(test__http_write_bytes__writes_the_data_and_returns_the_length_with_te_chunked),
    cmocka_unit_test(test__http_write_bytes__writes_nothing_for_empty_data_with_te_chunked),

    cmocka_unit_test(test__http_end_body__writes_chunk_end_with_te_chunked),
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
