#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>

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


static void test__http_base64_encode__can_encode_strings_with_zero_byte_padding(void **state)
{
    const char *s;
    char dest[64];
    unsigned res;

    s = "abc";
    res = http_base64_encode(dest, s, strlen(s));
    dest[res] = 0;
    assert_string_equal(dest, "YWJj");

    s = "abcdef";
    res = http_base64_encode(dest, s, strlen(s));
    dest[res] = 0;
    assert_string_equal(dest, "YWJjZGVm");
}

static void test__http_base64_encode__can_encode_strings_with_one_byte_padding(void **state)
{
    const char *s;
    char dest[64];
    unsigned res;

    s = "ab";
    res = http_base64_encode(dest, s, strlen(s));
    dest[res] = 0;
    assert_string_equal(dest, "YWI=");

    s = "abcab";
    res = http_base64_encode(dest, s, strlen(s));
    dest[res] = 0;
    assert_string_equal(dest, "YWJjYWI=");
}

static void test__http_base64_encode__can_encode_strings_with_two_bytes_padding(void **state)
{
    const char *s;
    char dest[64];
    unsigned res;

    s = "a";
    res = http_base64_encode(dest, s, strlen(s));
    dest[res] = 0;
    assert_string_equal(dest, "YQ==");

    s = "abca";
    res = http_base64_encode(dest, s, strlen(s));
    dest[res] = 0;
    assert_string_equal(dest, "YWJjYQ==");
}

static void test__http_base64_encode__returns_the_length_of_the_resulting_string(void **state)
{
    char dest[64] = {0};

    const char *s;

    s = "";
    assert_int_equal(http_base64_encode(dest, s, strlen(s)), http_base64_encode_length(strlen(s)));

    s = "0";
    assert_int_equal(http_base64_encode(dest, s, strlen(s)), http_base64_encode_length(strlen(s)));

    s = "01";
    assert_int_equal(http_base64_encode(dest, s, strlen(s)), http_base64_encode_length(strlen(s)));

    s = "012";
    assert_int_equal(http_base64_encode(dest, s, strlen(s)), http_base64_encode_length(strlen(s)));

    s = "0123";
    assert_int_equal(http_base64_encode(dest, s, strlen(s)), http_base64_encode_length(strlen(s)));

    s = "01234";
    assert_int_equal(http_base64_encode(dest, s, strlen(s)), http_base64_encode_length(strlen(s)));

    s = "012345";
    assert_int_equal(http_base64_encode(dest, s, strlen(s)), http_base64_encode_length(strlen(s)));

    s = "012346";
    assert_int_equal(http_base64_encode(dest, s, strlen(s)), http_base64_encode_length(strlen(s)));
}

static void test__http_base64_encode__returns_zero_if_src_is_null(void **state)
{
    char dest[64];
    assert_int_equal(http_base64_encode(dest, NULL, 3) , 0);
}

static void test__http_base64_encode__returns_zero_if_dest_is_null(void **state)
{
    assert_int_equal(http_base64_encode(NULL, "012", 3) , 0);
}

static void test__http_base64_encode__does_not_write_outside_buffer(void **state)
{

    char dest[64] = { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x78, 0 };

    http_base64_encode(dest+1, "a", 1);
    assert_string_equal(dest, ((const char[]) { 0x12, 'Y', 'Q', '=', '=', 0xBC, 0xDE, 0xF0, 0x78, 0 }));

    http_base64_encode(dest+1, "bc", 2);
    assert_string_equal(dest, ((const char[]) { 0x12, 'Y', 'm', 'M', '=', 0xBC, 0xDE, 0xF0, 0x78, 0 }));

    http_base64_encode(dest+1, "def", 3);
    assert_string_equal(dest, ((const char[]) { 0x12, 'Z', 'G', 'V', 'm', 0xBC, 0xDE, 0xF0, 0x78, 0 }));

    http_base64_encode(dest+1, "ghij", 4);
    assert_string_equal(dest, ((const char[]) { 0x12, 'Z', '2', 'h', 'p', 'a', 'g', '=', '=', 0 }));
}

static void test__http_base64_encode__can_encode_all_values(void **state)
{
    char src[256];
    char dest[350];
    const char *expect[] = {
        "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4"
        "OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3Bx"
        "cnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmq"
        "q6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj"
        "5OXm5+jp6uvs7e7v8PHy8/T19vf4+fr7/P3+/w==",
        "AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyAhIiMkJSYnKCkqKywtLi8wMTIzNDU2Nzg5"
        "Ojs8PT4/QEFCQ0RFRkdISUpLTE1OT1BRUlNUVVZXWFlaW1xdXl9gYWJjZGVmZ2hpamtsbW5vcHFy"
        "c3R1dnd4eXp7fH1+f4CBgoOEhYaHiImKi4yNjo+QkZKTlJWWl5iZmpucnZ6foKGio6Slpqeoqaqr"
        "rK2ur7CxsrO0tba3uLm6u7y9vr/AwcLDxMXGx8jJysvMzc7P0NHS09TV1tfY2drb3N3e3+Dh4uPk"
        "5ebn6Onq6+zt7u/w8fLz9PX29/j5+vv8/f7/AA==",
        "AgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6"
        "Ozw9Pj9AQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpbXF1eX2BhYmNkZWZnaGlqa2xtbm9wcXJz"
        "dHV2d3h5ent8fX5/gIGCg4SFhoeIiYqLjI2Oj5CRkpOUlZaXmJmam5ydnp+goaKjpKWmp6ipqqus"
        "ra6vsLGys7S1tre4ubq7vL2+v8DBwsPExcbHyMnKy8zNzs/Q0dLT1NXW19jZ2tvc3d7f4OHi4+Tl"
        "5ufo6err7O3u7/Dx8vP09fb3+Pn6+/z9/v8AAQ=="
    };

    for(int n = 0; n < 3; n++) {
        for(int i = 0; i < 256; i++) {
            src[i] = i+n;
        }

        unsigned ret = http_base64_encode(dest, src, 256);
        dest[ret] = 0;
        assert_string_equal(dest,expect[n]);
    }
}




static void test__http_base64_decode__can_decode_strings_with_zero_byte_padding(void **state)
{
    const char *s;
    char dest[64];
    unsigned res;

    s = "YWJj";
    res = http_base64_decode(dest, s, strlen(s));
    dest[res] = 0;
    assert_string_equal(dest, "abc");

    s = "YWJjZGVm";
    res = http_base64_decode(dest, s, strlen(s));
    dest[res] = 0;
    assert_string_equal(dest, "abcdef");

    s = "YWJjYWJjZGVm";
    res = http_base64_decode(dest, s, strlen(s));
    dest[res] = 0;
    assert_string_equal(dest, "abcabcdef");
}

static void test__http_base64_decode__can_decode_strings_with_one_byte_padding(void **state)
{
    const char *s;
    char dest[64];
    unsigned res;

    s = "YWI=";
    res = http_base64_decode(dest, s, strlen(s));
    dest[res] = 0;
    assert_string_equal(dest, "ab");

    s = "YWJjYWI=";
    res = http_base64_decode(dest, s, strlen(s));
    dest[res] = 0;
    assert_string_equal(dest, "abcab");
}

static void test__http_base64_decode__can_decode_strings_with_two_bytes_padding(void **state)
{
    const char *s;
    char dest[64];
    unsigned res;

    s = "YQ==";
    res = http_base64_decode(dest, s, strlen(s));
    dest[res] = 0;
    assert_string_equal(dest, "a");

    s = "YWJjYQ==";
    res = http_base64_decode(dest, s, strlen(s));
    dest[res] = 0;
    assert_string_equal(dest, "abca");
}

static void test__http_base64_decode__returns_the_length_of_the_resulting_string(void **state)
{
    char dest[64];

    assert_int_equal(http_base64_decode(dest, "YQ==", 4), 1);
    assert_int_equal(http_base64_decode(dest, "YWI=", 4), 2);
    assert_int_equal(http_base64_decode(dest, "YWJj", 4), 3);
    assert_int_equal(http_base64_decode(dest, "YWJjYQ==", 8), 4);
    assert_int_equal(http_base64_decode(dest, "YWJjYWI=", 8), 5);
    assert_int_equal(http_base64_decode(dest, "YWJjYWJj", 8), 6);
}

static void test__http_base64_decode__returns_zero_if_dest_is_null(void **state)
{
    assert_int_equal(http_base64_decode(NULL, "YWJj", 4), 0);
}

static void test__http_base64_decode__returns_zero_if_src_is_null(void **state)
{
    char dest[64];
    assert_int_equal(http_base64_decode(dest, NULL, 4), 0);
}

static void test__http_base64_decode__does_not_write_outside_buffer(void **state)
{

    char dest[64] = { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0 };

    http_base64_decode(dest+1, "YQ==", 4);
    assert_string_equal(dest, ((const char[]) { 0x12, 'a', 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0 }));

    http_base64_decode(dest+1, "YmM=", 4);
    assert_string_equal(dest, ((const char[]) { 0x12, 'b', 'c', 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0 }));

    http_base64_decode(dest+1, "ZGVm", 4);
    assert_string_equal(dest, ((const char[]) { 0x12, 'd', 'e', 'f', 0x9A, 0xBC, 0xDE, 0xF0, 0 }));

    http_base64_decode(dest+1, "Z2hpag==", 8);
    assert_string_equal(dest, ((const char[]) { 0x12, 'g', 'h', 'i', 'j', 0xBC, 0xDE, 0xF0, 0 }));
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

    cmocka_unit_test(test__http_base64_encode__can_encode_strings_with_zero_byte_padding),
    cmocka_unit_test(test__http_base64_encode__can_encode_strings_with_one_byte_padding),
    cmocka_unit_test(test__http_base64_encode__can_encode_strings_with_two_bytes_padding),
    cmocka_unit_test(test__http_base64_encode__returns_the_length_of_the_resulting_string),
    cmocka_unit_test(test__http_base64_encode__returns_zero_if_src_is_null),
    cmocka_unit_test(test__http_base64_encode__returns_zero_if_dest_is_null),
    cmocka_unit_test(test__http_base64_encode__does_not_write_outside_buffer),
    cmocka_unit_test(test__http_base64_encode__can_encode_all_values),

    cmocka_unit_test(test__http_base64_decode__can_decode_strings_with_zero_byte_padding),
    cmocka_unit_test(test__http_base64_decode__can_decode_strings_with_one_byte_padding),
    cmocka_unit_test(test__http_base64_decode__can_decode_strings_with_two_bytes_padding),
    cmocka_unit_test(test__http_base64_decode__returns_the_length_of_the_resulting_string),
    cmocka_unit_test(test__http_base64_decode__returns_zero_if_src_is_null),
    cmocka_unit_test(test__http_base64_decode__returns_zero_if_dest_is_null),
    cmocka_unit_test(test__http_base64_decode__does_not_write_outside_buffer),
};

int main(void)
{
    int fails = 0;
    fails += cmocka_run_group_tests(tests_for_http_util, NULL, NULL);

    return fails;
}
