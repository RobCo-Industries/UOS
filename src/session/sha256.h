#ifndef UOS_SHA256_H
#define UOS_SHA256_H

#include <stddef.h>
#include <stdint.h>

/*
 * SHA-256 (FIPS 180-4), streaming. sha_init, then sha_update any number of
 * times, then sha_final. The context holds no pointers; copy it freely.
 * `blocks` counts compressions done on this context, for tests and for the
 * work accounting of uaf_verify.
 */
#define SHA_BLOCK_SIZE  64
#define SHA_DIGEST_SIZE 32

typedef struct sha_ctx {
    uint32_t state[8];
    uint32_t len_lo;            /* total message bytes, low 32 bits */
    uint32_t len_hi;            /* total message bytes, high 32 bits */
    uint32_t blocks;            /* compressions done */
    uint32_t buf_len;           /* bytes waiting in buf, 0 to 63 */
    uint8_t buf[SHA_BLOCK_SIZE];
} sha_ctx_t;

void sha_init(sha_ctx_t *ctx);
void sha_update(sha_ctx_t *ctx, const void *data, size_t len);
/* Writes the 32-byte digest. The context must be re-initialised after. */
void sha_final(sha_ctx_t *ctx, uint8_t out[SHA_DIGEST_SIZE]);
/* One-shot digest of one buffer. */
void sha_digest(const void *data, size_t len, uint8_t out[SHA_DIGEST_SIZE]);

#endif
