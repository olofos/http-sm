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
    return 0;
}
