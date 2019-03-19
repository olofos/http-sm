#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>

#include <cmocka.h>

#include "http-sm/sha1.h"

// Tests from https://www.di-mgt.com.au/sha_testvectors.html

static char *hash_to_ascii(const uint8_t *hash)
{
    static char buf[45];

    char *s = buf;

    for(int i = 0; i < 20; i++) {
        if(i > 0 && (i % 4 == 0)) {
            *s++ = ' ';
        }

        sprintf(s, "%02x", hash[i]);
        s += 2;
    }
    *s = 0;

    return buf;
}

static void test__http_sha1__can_calculate_hash1(void **state)
{
    const char *input = "abc";
    uint8_t hash[21];
    const char *expected = "a9993e36 4706816a ba3e2571 7850c26c 9cd0d89d";

    http_sha1(hash, input, strlen(input));

    assert_string_equal(hash_to_ascii(hash), expected);
}

static void test__http_sha1__can_calculate_hash2(void **state)
{
    const char *input = "";
    uint8_t hash[21];
    const char *expected = "da39a3ee 5e6b4b0d 3255bfef 95601890 afd80709";

    http_sha1(hash, input, strlen(input));

    assert_string_equal(hash_to_ascii(hash), expected);
}

static void test__http_sha1__can_calculate_hash3(void **state)
{
    const char *input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    uint8_t hash[21];
    const char *expected = "84983e44 1c3bd26e baae4aa1 f95129e5 e54670f1";

    http_sha1(hash, input, strlen(input));

    assert_string_equal(hash_to_ascii(hash), expected);
}

static void test__http_sha1__can_calculate_hash4(void **state)
{
    const char *input = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    uint8_t hash[21];
    const char *expected = "a49b2446 a02c645b f419f995 b6709125 3a04a259";

    http_sha1(hash, input, strlen(input));

    assert_string_equal(hash_to_ascii(hash), expected);
}


static void test__http_sha1__can_calculate_hash4_in_steps(void **state)
{
    const char *input1 = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn";
    const char *input2 = "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    uint8_t hash[21];
    const char *expected = "a49b2446 a02c645b f419f995 b6709125 3a04a259";

    struct http_sha1_ctx ctx;

    http_sha1_init(&ctx);
    http_sha1_update(&ctx, (const uint8_t*)input1, strlen(input1));
    http_sha1_update(&ctx, (const uint8_t*)input2, strlen(input2));
    http_sha1_final(hash, &ctx);

    assert_string_equal(hash_to_ascii(hash), expected);
}

static void test__http_sha1__can_calculate_hash5_in_small_steps(void **state)
{
    uint8_t hash[21];
    const char *expected = "34aa973c d4c4daa4 f61eeb2b dbad2731 6534016f";
    const int total_len = 1000000;

    const int str_len = 1;
    uint8_t *str = malloc(str_len + 1);
    for(int i = 0; i < str_len; i++) {
        str[i] = 'a';
    }
    str[str_len] = 0;

    assert_int_equal(total_len % str_len, 0);

    struct http_sha1_ctx ctx;

    http_sha1_init(&ctx);
    for(int i = 0; i < total_len / str_len; i++) {
        http_sha1_update(&ctx, str, str_len);
    }
    http_sha1_final(hash, &ctx);

    assert_string_equal(hash_to_ascii(hash), expected);

    free(str);
}

static void test__http_sha1__can_calculate_hash5_in_large_steps(void **state)
{
    uint8_t hash[21];
    const char *expected = "34aa973c d4c4daa4 f61eeb2b dbad2731 6534016f";
    const int total_len = 1000000;

    const int str_len = 10000;
    uint8_t *str = malloc(str_len + 1);
    for(int i = 0; i < str_len; i++) {
        str[i] = 'a';
    }
    str[str_len] = 0;

    assert_int_equal(total_len % str_len, 0);

    struct http_sha1_ctx ctx;

    http_sha1_init(&ctx);
    for(int i = 0; i < total_len / str_len; i++) {
        http_sha1_update(&ctx, str, str_len);
    }
    http_sha1_final(hash, &ctx);

    assert_string_equal(hash_to_ascii(hash), expected);

    free(str);
}

#ifdef TEST_SLOW
static void test__http_sha1__can_calculate_hash6_in_steps(void **state)
{
    const char * const input = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno";
    uint8_t hash[21];
    const char *expected = "7789f0c9 ef7bfc40 d9331114 3dfbe69e 2017f592";

    struct http_sha1_ctx ctx;

    http_sha1_init(&ctx);
    for(long long i = 0; i < 16777216; i++) {
        http_sha1_update(&ctx, (const uint8_t*)input, strlen(input));
    }
    http_sha1_final(hash, &ctx);

    assert_string_equal(hash_to_ascii(hash), expected);
}
#endif

const struct CMUnitTest tests_for_http_sha1[] = {
    cmocka_unit_test(test__http_sha1__can_calculate_hash1),
    cmocka_unit_test(test__http_sha1__can_calculate_hash2),
    cmocka_unit_test(test__http_sha1__can_calculate_hash3),
    cmocka_unit_test(test__http_sha1__can_calculate_hash4),

    cmocka_unit_test(test__http_sha1__can_calculate_hash4_in_steps),
    cmocka_unit_test(test__http_sha1__can_calculate_hash5_in_small_steps),
    cmocka_unit_test(test__http_sha1__can_calculate_hash5_in_large_steps),

#ifdef TEST_SLOW
    cmocka_unit_test(test__http_sha1__can_calculate_hash6_in_steps),
#endif
};

int main(void)
{
    int fails = 0;
    fails += cmocka_run_group_tests(tests_for_http_sha1, NULL, NULL);

    return fails;
}
