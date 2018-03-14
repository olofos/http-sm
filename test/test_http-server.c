#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <stdarg.h>

#include <cmocka.h>

#include "http.h"
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


// Main ////////////////////////////////////////////////////////////////////////

const struct CMUnitTest tests_for_http_server[] = {
    cmocka_unit_test(test__http_server_match_url__returns_true_if_the_url_exactly_matches),
    cmocka_unit_test(test__http_server_match_url__returns_false_for_partial_match),
    cmocka_unit_test(test__http_server_match_url__returns_false_if_the_url_does_not_match),
    cmocka_unit_test(test__http_server_match_url__returns_true_if_wildcard_matches),
    cmocka_unit_test(test__http_server_match_url__returns_false_if_wildcard_does_not_matches),
};

int main(void)
{
    return cmocka_run_group_tests(tests_for_http_server, NULL, NULL);
}
