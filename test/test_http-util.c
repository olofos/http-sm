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

static void test__http_base64_encode_length__returns_correct_length_if_input_is_zero_mod_three(void **state)
{
    assert_int_equal(http_base64_encode_length(0), 0);
    assert_int_equal(http_base64_encode_length(3), 4);
    assert_int_equal(http_base64_encode_length(6), 8);
    assert_int_equal(http_base64_encode_length(9), 12);
}

static void test__http_base64_encode_length__returns_correct_length_if_input_is_one_mod_three(void **state)
{
    assert_int_equal(http_base64_encode_length(1), 4);
    assert_int_equal(http_base64_encode_length(4), 8);
    assert_int_equal(http_base64_encode_length(7), 12);
    assert_int_equal(http_base64_encode_length(10), 16);
}

static void test__http_base64_encode_length__returns_correct_length_if_input_is_two_mod_three(void **state)
{
    assert_int_equal(http_base64_encode_length(2), 4);
    assert_int_equal(http_base64_encode_length(5), 8);
    assert_int_equal(http_base64_encode_length(8), 12);
    assert_int_equal(http_base64_encode_length(11), 16);
}

static void test__http_base64_decode_length__returns_zero_if_length_is_zero(void **state)
{
    assert_int_equal(http_base64_decode_length("", 0), 0);
}

static void test__http_base64_decode_length__returns_zero_if_buf_is_zero(void **state)
{
    assert_int_equal(http_base64_decode_length(NULL, 4), 0);
}

static void test__http_base64_decode_length__returns_correct_length_if_there_is_no_padding(void **state)
{
    assert_int_equal(http_base64_decode_length("0123", 4), 3);
    assert_int_equal(http_base64_decode_length("01230123", 8), 6);
    assert_int_equal(http_base64_decode_length("012301230123", 12), 9);
}

static void test__http_base64_decode_length__returns_correct_length_if_there_is_one_byte_padding(void **state)
{
    assert_int_equal(http_base64_decode_length("012=", 4), 2);
    assert_int_equal(http_base64_decode_length("0123012=", 8), 5);
    assert_int_equal(http_base64_decode_length("01230123012=", 12), 8);
}

static void test__http_base64_decode_length__returns_correct_length_if_there_is_two_bytes_padding(void **state)
{
    assert_int_equal(http_base64_decode_length("01==", 4), 1);
    assert_int_equal(http_base64_decode_length("012301==", 8), 4);
    assert_int_equal(http_base64_decode_length("0123012301==", 12), 7);
}

static void test__http_base64_decode_length__returns_zero_if_there_length_is_not_a_multiple_of_four(void **state)
{
    assert_int_equal(http_base64_decode_length("0", 1), 0);
    assert_int_equal(http_base64_decode_length("01", 2), 0);
    assert_int_equal(http_base64_decode_length("012", 3), 0);

    assert_int_equal(http_base64_decode_length("01230", 5), 0);
    assert_int_equal(http_base64_decode_length("012301", 6), 0);
    assert_int_equal(http_base64_decode_length("0123012", 7), 0);

}

const struct CMUnitTest tests_for_http_util[] = {
    cmocka_unit_test(test__http_hex_to_int__can_convert_hex_digits),
    cmocka_unit_test(test__http_hex_to_int__returns_zero_if_argument_is_not_a_digit),

    cmocka_unit_test(test__http_base64_encode_length__returns_correct_length_if_input_is_zero_mod_three),
    cmocka_unit_test(test__http_base64_encode_length__returns_correct_length_if_input_is_one_mod_three),
    cmocka_unit_test(test__http_base64_encode_length__returns_correct_length_if_input_is_two_mod_three),

    cmocka_unit_test(test__http_base64_decode_length__returns_zero_if_length_is_zero),
    cmocka_unit_test(test__http_base64_decode_length__returns_zero_if_buf_is_zero),
    cmocka_unit_test(test__http_base64_decode_length__returns_correct_length_if_there_is_no_padding),
    cmocka_unit_test(test__http_base64_decode_length__returns_correct_length_if_there_is_one_byte_padding),
    cmocka_unit_test(test__http_base64_decode_length__returns_correct_length_if_there_is_two_bytes_padding),
    cmocka_unit_test(test__http_base64_decode_length__returns_zero_if_there_length_is_not_a_multiple_of_four),
};

int main(void)
{
    int fails = 0;
    fails += cmocka_run_group_tests(tests_for_http_util, NULL, NULL);

    return fails;
}
