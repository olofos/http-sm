#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

#include <cmocka.h>

#include "http-private.h"

static void test__http_hex_to_int__can_convert_hex_digits(void **state)
{
    assert_int_equal(0x0, http_hex_to_int('0'));
    assert_int_equal(0x1, http_hex_to_int('1'));
    assert_int_equal(0x2, http_hex_to_int('2'));
    assert_int_equal(0x3, http_hex_to_int('3'));
    assert_int_equal(0x4, http_hex_to_int('4'));
    assert_int_equal(0x5, http_hex_to_int('5'));
    assert_int_equal(0x6, http_hex_to_int('6'));
    assert_int_equal(0x7, http_hex_to_int('7'));
    assert_int_equal(0x8, http_hex_to_int('8'));
    assert_int_equal(0x9, http_hex_to_int('9'));

    assert_int_equal(0xA, http_hex_to_int('A'));
    assert_int_equal(0xB, http_hex_to_int('B'));
    assert_int_equal(0xC, http_hex_to_int('C'));
    assert_int_equal(0xD, http_hex_to_int('D'));
    assert_int_equal(0xE, http_hex_to_int('E'));
    assert_int_equal(0xF, http_hex_to_int('F'));

    assert_int_equal(0xA, http_hex_to_int('a'));
    assert_int_equal(0xB, http_hex_to_int('b'));
    assert_int_equal(0xC, http_hex_to_int('c'));
    assert_int_equal(0xD, http_hex_to_int('d'));
    assert_int_equal(0xE, http_hex_to_int('e'));
    assert_int_equal(0xF, http_hex_to_int('f'));

}

static void test__http_hex_to_int__returns_zero_if_argument_is_not_a_digit(void **state)
{
    for(int i = 0; i <= 0xFF; i++) {
        if(!(('0' <= i && i <= '9') || ('A' <= i && i <= 'F') || ('a' <= i && i <= 'f'))) {
            assert_int_equal(0x0, http_hex_to_int(i));
        }
    }
}

const struct CMUnitTest tests_for_http_util[] = {
    cmocka_unit_test(test__http_hex_to_int__can_convert_hex_digits),
    cmocka_unit_test(test__http_hex_to_int__returns_zero_if_argument_is_not_a_digit),
};

int main(void)
{
    return cmocka_run_group_tests(tests_for_http_util, NULL, NULL);
}
