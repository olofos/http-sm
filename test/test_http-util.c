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

static void test__http_base64_decode__can_decode_all_values(void **state)
{
    const char *src[] = {
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
        "/ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+",
        "+/ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
        "9+/ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz012345678",
    };
    char dest[256];
    const char expect[][64] = {
        {
            0x00, 0x10, 0x83, 0x10, 0x51, 0x87, 0x20, 0x92, 0x8b, 0x30, 0xd3, 0x8f, 0x41, 0x14, 0x93, 0x51,
            0x55, 0x97, 0x61, 0x96, 0x9b, 0x71, 0xd7, 0x9f, 0x82, 0x18, 0xa3, 0x92, 0x59, 0xa7, 0xa2, 0x9a,
            0xab, 0xb2, 0xdb, 0xaf, 0xc3, 0x1c, 0xb3, 0xd3, 0x5d, 0xb7, 0xe3, 0x9e, 0xbb, 0xf3, 0xdf, 0xbf,
        },
        {
            0xfc, 0x00, 0x42, 0x0c, 0x41, 0x46, 0x1c, 0x82, 0x4a, 0x2c, 0xc3, 0x4e, 0x3d, 0x04, 0x52, 0x4d,
            0x45, 0x56, 0x5d, 0x86, 0x5a, 0x6d, 0xc7, 0x5e, 0x7e, 0x08, 0x62, 0x8e, 0x49, 0x66, 0x9e, 0x8a,
            0x6a, 0xae, 0xcb, 0x6e, 0xbf, 0x0c, 0x72, 0xcf, 0x4d, 0x76, 0xdf, 0x8e, 0x7a, 0xef, 0xcf, 0x7e,
        },
        {
            0xfb, 0xf0, 0x01, 0x08, 0x31, 0x05, 0x18, 0x72, 0x09, 0x28, 0xb3, 0x0d, 0x38, 0xf4, 0x11, 0x49,
            0x35, 0x15, 0x59, 0x76, 0x19, 0x69, 0xb7, 0x1d, 0x79, 0xf8, 0x21, 0x8a, 0x39, 0x25, 0x9a, 0x7a,
            0x29, 0xaa, 0xbb, 0x2d, 0xba, 0xfc, 0x31, 0xcb, 0x3d, 0x35, 0xdb, 0x7e, 0x39, 0xeb, 0xbf, 0x3d,
        },
        {
            0xf7, 0xef, 0xc0, 0x04, 0x20, 0xc4, 0x14, 0x61, 0xc8, 0x24, 0xa2, 0xcc, 0x34, 0xe3, 0xd0, 0x45,
            0x24, 0xd4, 0x55, 0x65, 0xd8, 0x65, 0xa6, 0xdc, 0x75, 0xe7, 0xe0, 0x86, 0x28, 0xe4, 0x96, 0x69,
            0xe8, 0xa6, 0xaa, 0xec, 0xb6, 0xeb, 0xf0, 0xc7, 0x2c, 0xf4, 0xd7, 0x6d, 0xf8, 0xe7, 0xae, 0xfc,
        },
    };

    for(int n = 1; n < sizeof(expect)/sizeof(expect[0]); n++) {
        http_base64_decode(dest, src[n], strlen(src[n]));
        for(int i = 0; i < sizeof(expect[n]); i++) {
            assert_int_equal(dest[i], expect[n][i]);
        }
    }
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
    cmocka_unit_test(test__http_base64_decode__can_decode_all_values),
};

int main(void)
{
    int fails = 0;
    fails += cmocka_run_group_tests(tests_for_http_util, NULL, NULL);

    return fails;
}
