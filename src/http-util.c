#include <stdint.h>
#include "http-private.h"

int http_hex_to_int(char c)
{
    if('0' <= c && c <= '9') {
        return c - '0';
    } else if('A' <= c && c <= 'F') {
        return 0xA + c - 'A';
    } else  if('a' <= c && c <= 'f') {
        return 0xA + c - 'a';
    }
    return 0;
}


unsigned http_base64_encode_length(unsigned len)
{
    unsigned pad = (3 - (len % 3)) % 3;
    return 4 * ((len + pad) / 3);
}

unsigned http_base64_decode_length(const char *buf, unsigned len)
{
    if(!len || !buf || len % 4) {
        return 0;
    }

    unsigned pad = (buf[len-1] == '=') + (buf[len-2] == '=');

    return 3 * (len - pad) / 4;
}

unsigned http_base64_encode(char *dest, const char *src, unsigned len)
{
    static const char encode_tab[64] ="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    if(!src || !dest) {
        return 0;
    }

    unsigned n = 0;
    unsigned extra = len % 3;
    unsigned i = 0;

    while(i < len - extra) {
        uint8_t in1 = src[i++];
        uint8_t in2 = src[i++];
        uint8_t in3 = src[i++];

        uint8_t out1 = in1 >> 2;
        uint8_t out2 = (0x30 &(in1 << 4)) | (in2 >> 4);
        uint8_t out3 = (0x3C &(in2 << 2)) | (in3 >> 6);
        uint8_t out4 = 0x3F & in3;

        dest[n++] = encode_tab[out1];
        dest[n++] = encode_tab[out2];
        dest[n++] = encode_tab[out3];
        dest[n++] = encode_tab[out4];
    }

    if(extra == 1) {
        uint8_t in1 = src[i++];

        uint8_t out1 = in1 >> 2;
        uint8_t out2 = 0x30 &(in1 << 4);

        dest[n++] = encode_tab[out1];
        dest[n++] = encode_tab[out2];
        dest[n++] = '=';
        dest[n++] = '=';
    } else if(extra == 2) {
        uint8_t in1 = src[i++];
        uint8_t in2 = src[i++];

        uint8_t out1 = in1 >> 2;
        uint8_t out2 = (0x30 &(in1 << 4)) | (in2 >> 4);
        uint8_t out3 = (0x3C &(in2 << 2));

        dest[n++] = encode_tab[out1];
        dest[n++] = encode_tab[out2];
        dest[n++] = encode_tab[out3];
        dest[n++] = '=';
    }

    return n;
}

unsigned http_base64_decode(char *dest, const char *src, unsigned len)
{
    static const char decode_tab[256] = {
         0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
         0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
         0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  62,   0,   0,   0,  63,
        52,  53,  54,  55,  56,  57,  58,  59,  60,  61,   0,   0,   0,   0,   0,   0,
         0,   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,
        15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,   0,   0,   0,   0,   0,
         0,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,
        41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,   0,   0,   0,   0,   0,
         0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
         0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
         0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
         0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
         0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
         0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
         0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
         0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    };

    if(!src || !dest || !len || len % 4) {
        return 0;
    }

    const uint8_t *s = (const uint8_t *)src;

    unsigned pad = (src[len-1] == '=') + (src[len-2] == '=');

    unsigned n = 0;
    unsigned i = 0;

    while(i + 4 <= len - pad) {
        uint8_t in1 = decode_tab[s[i++]];
        uint8_t in2 = decode_tab[s[i++]];
        uint8_t in3 = decode_tab[s[i++]];
        uint8_t in4 = decode_tab[s[i++]];

        uint8_t out1 = (in1 << 2) | (in2 >> 4);
        uint8_t out2 = (in2 << 4) | (in3 >> 2);
        uint8_t out3 = (in3 << 6) | in4;

        dest[n++] = out1;
        dest[n++] = out2;
        dest[n++] = out3;
    }

    if(pad == 1) {
        uint8_t in1 = decode_tab[s[i++]];
        uint8_t in2 = decode_tab[s[i++]];
        uint8_t in3 = decode_tab[s[i++]];

        uint8_t out1 = (in1 << 2) | (in2 >> 4);
        uint8_t out2 = (in2 << 4) | (in3 >> 2);

        dest[n++] = out1;
        dest[n++] = out2;
    } else if(pad == 2) {
        uint8_t in1 = decode_tab[s[i++]];
        uint8_t in2 = decode_tab[s[i++]];

        uint8_t out1 = (in1 << 2) | (in2 >> 4);

        dest[n++] = out1;
    }

    return n;
}
