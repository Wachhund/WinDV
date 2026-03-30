/* sha256.h — Pure C SHA-256 implementation for WinDV.
 *
 * Streaming API: init, update, final. No heap allocation, no OS dependencies.
 * Compatible with MSVC 6.0 (C89) and MSVC 2017.
 *
 * Based on public domain SHA-256 implementations.
 * This file is placed in the public domain.
 */

#ifndef SHA256_H
#define SHA256_H

#ifdef __cplusplus
extern "C" {
#endif

/* SHA-256 context. Caller allocates on stack or as member variable. */
typedef struct {
    unsigned char data[64];      /* current 64-byte input block */
    unsigned int  datalen;       /* number of bytes in current block */
    unsigned int  bitlen[2];     /* total message length in bits (64-bit as two 32-bit) */
    unsigned int  state[8];      /* running hash state (H0..H7) */
} SHA256_CTX;

/* Initialize SHA-256 context. Must be called before first update. */
void sha256_init(SHA256_CTX *ctx);

/* Feed data into the hash. Can be called multiple times with chunks. */
void sha256_update(SHA256_CTX *ctx, const unsigned char *data, unsigned int len);

/* Finalize and write the 32-byte (256-bit) hash to `hash`.
 * `hash` must point to at least 32 bytes. */
void sha256_final(SHA256_CTX *ctx, unsigned char hash[32]);

/* Convenience: format a 32-byte hash as 64 lowercase hex characters + NUL.
 * `out` must point to at least 65 bytes. */
void sha256_hex(const unsigned char hash[32], char out[65]);

#ifdef __cplusplus
}
#endif

#endif /* SHA256_H */
