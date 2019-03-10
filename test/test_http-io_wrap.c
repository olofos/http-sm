#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <unistd.h>
#include <string.h>

#include <cmocka.h>

#include "http-sm/http.h"
#include "http-private.h"

#include "test-util.h"

// Mocks ///////////////////////////////////////////////////////////////////////

static int enable_malloc_mock = 0;
static int enable_write_mock = 0;
static int enable_read_mock = 0;

static char *wrap_read_buf;


void *__real_malloc(size_t size);

void *__wrap_malloc(size_t size)
{
    if(enable_malloc_mock) {
        check_expected(size);
        return (void *)mock();
    } else {
        return __real_malloc(size);
    }
}

ssize_t __real_write(int fd, const void *buf, size_t count);

ssize_t __wrap_write(int fd, const void *buf, size_t count)
{
    if(enable_write_mock) {
        check_expected(fd);
        check_expected(buf);
        check_expected(count);

        return mock();
    } else {
        return __real_write(fd, buf, count);
    }
}

ssize_t __real_read(int fd, void *buf, size_t count);

ssize_t __wrap_read(int fd, void *buf, size_t count)
{
    printf("__wrap_read\n");
    if(enable_read_mock) {
        check_expected(fd);
        check_expected(buf);
        check_expected(count);
        int n = mock();
        char *cbuf = buf;

        for(int i = 0; i < n; i++) {
            cbuf[i] = *wrap_read_buf++;
        }

        return n;
    } else {
        return __real_read(fd, buf, count);
    }
}


// Tests ///////////////////////////////////////////////////////////////////////

static void test__http_begin_request__returns_zero_if_out_of_memory(void **states)
{
    int fd = open_tmp_file();
    assert_true(0 <= fd);

    struct http_request request = {
        .fd = fd,
        .method = HTTP_METHOD_GET,
        .host = "www.example.com",
        .path = "/",
        .query = 0,
        .port = 8080,
    };

    expect_any(__wrap_malloc, size);
    will_return(__wrap_malloc, NULL);

    int ret = http_begin_request(&request);
    assert_int_equal(0, ret);

    close(fd);
}

static void test__http_getc__returns_minus_one_if_read_fails_with_te_identity(void **states)
{
    struct http_request request = {
        .fd = 3,
        .poke = -1,
        .state = HTTP_STATE_CLIENT_READ_BODY,
        .flags = 0,
        .read_content_length = 4,
    };

    expect_value(__wrap_read, fd, 3);
    expect_any(__wrap_read, buf);
    expect_any(__wrap_read, count);
    will_return(__wrap_read, -1);

    assert_int_equal(-1, http_getc(&request));
}

static void test__http_getc__returns_minus_one_if_read_fails_with_te_chunked(void **states)
{
    struct http_request request = {
        .fd = 3,
        .poke = -1,
        .state = HTTP_STATE_CLIENT_READ_BODY,
        .flags = HTTP_FLAG_READ_CHUNKED,
        .chunk_length = 4,
    };

    expect_value(__wrap_read, fd, 3);
    expect_any(__wrap_read, buf);
    expect_any(__wrap_read, count);
    will_return(__wrap_read, -1);

    assert_int_equal(-1, http_getc(&request));
}

static void test__http_getc__returns_minus_one_if_read_fails_in_beginning_of_chunk_header(void **states)
{
    struct http_request request = {
        .fd = 3,
        .poke = -1,
        .state = HTTP_STATE_CLIENT_READ_BODY,
        .flags = HTTP_FLAG_READ_CHUNKED,
        .chunk_length = 0,
    };

    expect_value(__wrap_read, fd, 3);
    expect_any(__wrap_read, buf);
    expect_any(__wrap_read, count);
    will_return(__wrap_read, -1);

    assert_int_equal(-1, http_getc(&request));
}

static void test__http_getc__returns_minus_one_if_read_fails_at_end_of_chunk_header(void **states)
{
    struct http_request request = {
        .fd = 3,
        .poke = -1,
        .state = HTTP_STATE_CLIENT_READ_BODY,
        .flags = HTTP_FLAG_READ_CHUNKED,
        .chunk_length = 0,
    };

    wrap_read_buf = "\r";
    expect_value(__wrap_read, fd, 3);
    expect_any(__wrap_read, buf);
    expect_any(__wrap_read, count);
    will_return(__wrap_read, 1);

    expect_value(__wrap_read, fd, 3);
    expect_any(__wrap_read, buf);
    expect_any(__wrap_read, count);
    will_return(__wrap_read, -1);

    assert_int_equal(-1, http_getc(&request));
}

static void test__http_write_string__returns_minus_one_if_write_fails_with_te_identity(void **states)
{
    struct http_request request = {
        .fd = 3,
        .flags = ~HTTP_FLAG_WRITE_CHUNKED,
    };

    char *str = "test";

    expect_value(__wrap_write, fd, 3);
    expect_value(__wrap_write, buf, str);
    expect_value(__wrap_write, count, strlen(str));
    will_return(__wrap_write, -1);

    int ret = http_write_string(&request, str);

    assert_int_equal(-1, ret);
}

static void test__http_ws_read__reads_everything(void **states)
{
    struct http_ws_connection conn = {
        .fd = 3,
        .frame_length = 3,
        .frame_index = 0,
    };

    wrap_read_buf = "abc";

    char buf[4];

    expect_value(__wrap_read, fd, 3);
    expect_value(__wrap_read, buf, buf);
    expect_value(__wrap_read, count, 3);
    will_return(__wrap_read, 2);

    expect_value(__wrap_read, fd, 3);
    expect_value(__wrap_read, buf, buf+2);
    expect_value(__wrap_read, count, 1);
    will_return(__wrap_read, 1);

    int ret = http_ws_read(&conn, buf, 3);
    buf[ret] = 0;

    assert_int_equal(3, ret);
    assert_string_equal(buf, "abc");
}

// Setup & Teardown ////////////////////////////////////////////////////////////

static int gr_setup_malloc_mock(void **state)
{
    enable_malloc_mock = 1;
    return 0;
}

static int gr_teardown_malloc_mock(void **state)
{
    enable_malloc_mock = 0;
    return 0;
}

static int gr_setup_read_mock(void **state)
{
    enable_read_mock = 1;
    return 0;
}

static int gr_teardown_read_mock(void **state)
{
    enable_read_mock = 0;
    return 0;
}

static int gr_setup_write_mock(void **state)
{
    enable_write_mock = 1;
    return 0;
}

static int gr_teardown_write_mock(void **state)
{
    enable_write_mock = 0;
    return 0;
}


// Main ////////////////////////////////////////////////////////////////////////

const struct CMUnitTest tests_for_http_io_malloc_mock[] = {
    cmocka_unit_test(test__http_begin_request__returns_zero_if_out_of_memory),
};

const struct CMUnitTest tests_for_http_io_read_mock[] = {
    cmocka_unit_test(test__http_getc__returns_minus_one_if_read_fails_with_te_identity),
    cmocka_unit_test(test__http_getc__returns_minus_one_if_read_fails_with_te_chunked),
    cmocka_unit_test(test__http_getc__returns_minus_one_if_read_fails_in_beginning_of_chunk_header),
    cmocka_unit_test(test__http_getc__returns_minus_one_if_read_fails_at_end_of_chunk_header),
    cmocka_unit_test(test__http_ws_read__reads_everything),
};

const struct CMUnitTest tests_for_http_io_write_mock[] = {
    cmocka_unit_test(test__http_write_string__returns_minus_one_if_write_fails_with_te_identity),
};

int main(void)
{
    int fails = 0;

    fails += cmocka_run_group_tests(tests_for_http_io_malloc_mock, gr_setup_malloc_mock, gr_teardown_malloc_mock);
    fails += cmocka_run_group_tests(tests_for_http_io_read_mock, gr_setup_read_mock, gr_teardown_read_mock);
    fails += cmocka_run_group_tests(tests_for_http_io_write_mock, gr_setup_write_mock, gr_teardown_write_mock);

    return fails;
}
