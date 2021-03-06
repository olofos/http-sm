#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <stdarg.h>

#include <cmocka.h>

#include "http-sm/http.h"
#include "http-private.h"

// Tests ///////////////////////////////////////////////////////////////////////

static void test__http_server_match_url__returns_true_if_the_url_exactly_matches(void **states)
{
    assert_true(http_server_match_url("/", "/"));
}

static void test__http_server_match_url__returns_false_for_partial_match(void **states)
{
    assert_false(http_server_match_url("a", "abc"));
    assert_false(http_server_match_url("abc", "a"));
}

static void test__http_server_match_url__returns_false_if_the_url_does_not_match(void **states)
{
    assert_false(http_server_match_url("a", "b"));
}

static void test__http_server_match_url__returns_true_if_wildcard_matches(void **states)
{
    assert_true(http_server_match_url("a*", "abc"));
}

static void test__http_server_match_url__returns_false_if_wildcard_does_not_matches(void **states)
{
    assert_false(http_server_match_url("a*", "bcd"));
}

static void assert_non_empty_string(const char *s)
{
    assert_non_null(s);
    assert_true(strlen(s) > 0);
}

static void test__http_status_string__returns_non_empty_strings_for_all_known_status(void **states)
{
    assert_non_empty_string(http_status_string(HTTP_STATUS_OK));
    assert_non_empty_string(http_status_string(HTTP_STATUS_NO_CONTENT));
    assert_non_empty_string(http_status_string(HTTP_STATUS_NOT_MODIFIED));
    assert_non_empty_string(http_status_string(HTTP_STATUS_BAD_REQUEST));
    assert_non_empty_string(http_status_string(HTTP_STATUS_NOT_FOUND));
    assert_non_empty_string(http_status_string(HTTP_STATUS_METHOD_NOT_ALLOWED));
    assert_non_empty_string(http_status_string(HTTP_STATUS_URI_TOO_LONG));
    assert_non_empty_string(http_status_string(HTTP_STATUS_INTERNAL_SERVER_ERROR));
    assert_non_empty_string(http_status_string(HTTP_STATUS_SERVICE_UNAVAILABLE));
    assert_non_empty_string(http_status_string(HTTP_STATUS_VERSION_NOT_SUPPORTED));

    assert_null(http_status_string(HTTP_STATUS_ERROR));
}

static void test__http_status_string__returns_an_empty_string_for_an_unkown_known_status(void **states)
{
    assert_non_null(http_status_string(0));
    assert_int_equal(strlen(http_status_string(0)), 0);
}


// Main ////////////////////////////////////////////////////////////////////////

const struct CMUnitTest tests_for_http_server[] = {
    cmocka_unit_test(test__http_server_match_url__returns_true_if_the_url_exactly_matches),
    cmocka_unit_test(test__http_server_match_url__returns_false_for_partial_match),
    cmocka_unit_test(test__http_server_match_url__returns_false_if_the_url_does_not_match),
    cmocka_unit_test(test__http_server_match_url__returns_true_if_wildcard_matches),
    cmocka_unit_test(test__http_server_match_url__returns_false_if_wildcard_does_not_matches),
};

const struct CMUnitTest tests_for_http_status_string[] = {
    cmocka_unit_test(test__http_status_string__returns_non_empty_strings_for_all_known_status),
    cmocka_unit_test(test__http_status_string__returns_an_empty_string_for_an_unkown_known_status),
};

int main(void)
{
    int fails = 0;
    fails += cmocka_run_group_tests(tests_for_http_server, NULL, NULL);
    fails += cmocka_run_group_tests(tests_for_http_status_string, NULL, NULL);
}
