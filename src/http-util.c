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
