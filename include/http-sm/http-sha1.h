#ifndef HTTP_SHA1_H_
#define HTTP_SHA1_H_

/*
  SHA-1 in C
  By Steve Reid <steve@edmweb.com>
  100% Public Domain
*/

#include <stdint.h>

struct http_sha1_ctx
{
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
};

void http_sha1_transform(uint32_t state[5], const uint8_t buffer[64]);

void http_sha1_init(struct http_sha1_ctx *context);

void http_sha1_update(struct http_sha1_ctx *context, const uint8_t *data, uint32_t len);

void http_sha1_final(uint8_t digest[20], struct http_sha1_ctx *context);

void http_sha1(uint8_t *hash_out, const char *str, int len);

#endif /* HTTP_SHA1_H_ */
