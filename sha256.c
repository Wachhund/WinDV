/* sha256.c — Pure C SHA-256 implementation (FIPS 180-4) for WinDV.
 *
 * Streaming API with no heap allocation and no OS dependencies.
 * Compatible with MSVC 6.0 (C89) and MSVC 2017.
 *
 * This file is placed in the public domain.
 */

#include <string.h>
#include "sha256.h"

/* Round constants: first 32 bits of the fractional parts of the cube
 * roots of the first 64 primes (2..311). */
static const unsigned int k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/* Bitwise operations used in the compression function. */
#define ROTR(x, n)  (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z)   (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z)  (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)  (ROTR(x,  2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x)  (ROTR(x,  6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x,  7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

/*
 * sha256_transform
 *
 * Processes one 64-byte (512-bit) block through the SHA-256 compression
 * function, updating the running hash state in ctx->state[].
 */
static void sha256_transform(SHA256_CTX *ctx, const unsigned char block[64])
{
    unsigned int a, b, c, d, e, f, g, h;
    unsigned int t1, t2;
    unsigned int w[64];
    unsigned int i;

    /* Prepare the message schedule: first 16 words from the block
     * (big-endian byte order), then extend to 64 words. */
    for (i = 0; i < 16; i++) {
        w[i] = ((unsigned int)block[i * 4    ] << 24)
             | ((unsigned int)block[i * 4 + 1] << 16)
             | ((unsigned int)block[i * 4 + 2] <<  8)
             | ((unsigned int)block[i * 4 + 3]);
    }
    for (i = 16; i < 64; i++) {
        w[i] = SIG1(w[i - 2]) + w[i - 7] + SIG0(w[i - 15]) + w[i - 16];
    }

    /* Initialize working variables from current hash state. */
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    /* 64 rounds of compression. */
    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + k[i] + w[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    /* Add compressed chunk to running hash state. */
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

/*
 * sha256_init
 *
 * Initializes the SHA-256 context with the standard initial hash values
 * (first 32 bits of the fractional parts of the square roots of the
 * first 8 primes).
 */
void sha256_init(SHA256_CTX *ctx)
{
    ctx->datalen = 0;
    ctx->bitlen[0] = 0;
    ctx->bitlen[1] = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

/*
 * sha256_update
 *
 * Feeds `len` bytes of data into the hash.  Can be called multiple
 * times to process data in chunks (streaming).  Accumulates bytes
 * in the internal 64-byte buffer and runs the transform whenever
 * a full block is available.
 */
void sha256_update(SHA256_CTX *ctx, const unsigned char *data, unsigned int len)
{
    unsigned int i;

    for (i = 0; i < len; i++) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            /* Add 512 bits (64 bytes) to the 64-bit bit counter.
             * bitlen[0] is the low 32 bits, bitlen[1] is the high 32 bits. */
            ctx->bitlen[0] += 512;
            if (ctx->bitlen[0] < 512) {
                ctx->bitlen[1]++;
            }
            ctx->datalen = 0;
        }
    }
}

/*
 * sha256_final
 *
 * Pads the message per FIPS 180-4 (append 0x80, zero-pad, append
 * 64-bit big-endian bit length) and writes the final 32-byte hash
 * digest to `hash`.
 */
void sha256_final(SHA256_CTX *ctx, unsigned char hash[32])
{
    unsigned int i;
    unsigned int padindex;
    unsigned int lo_bits, hi_bits;

    /* Account for remaining bytes in the bit counter. */
    lo_bits = ctx->bitlen[0] + (ctx->datalen * 8);
    hi_bits = ctx->bitlen[1];
    if (lo_bits < ctx->bitlen[0]) {
        hi_bits++;
    }

    /* Append the 0x80 byte. */
    padindex = ctx->datalen;
    ctx->data[padindex++] = 0x80;

    /* If there is not enough room for the 8-byte length field in this
     * block (need at least 8 bytes free after the 0x80), pad with zeros
     * and process this block, then start a new block of zeros. */
    if (padindex > 56) {
        while (padindex < 64) {
            ctx->data[padindex++] = 0x00;
        }
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    } else {
        while (padindex < 56) {
            ctx->data[padindex++] = 0x00;
        }
    }

    /* Append the 64-bit message length in big-endian byte order. */
    ctx->data[56] = (unsigned char)(hi_bits >> 24);
    ctx->data[57] = (unsigned char)(hi_bits >> 16);
    ctx->data[58] = (unsigned char)(hi_bits >>  8);
    ctx->data[59] = (unsigned char)(hi_bits);
    ctx->data[60] = (unsigned char)(lo_bits >> 24);
    ctx->data[61] = (unsigned char)(lo_bits >> 16);
    ctx->data[62] = (unsigned char)(lo_bits >>  8);
    ctx->data[63] = (unsigned char)(lo_bits);
    sha256_transform(ctx, ctx->data);

    /* Write the final hash value in big-endian byte order. */
    for (i = 0; i < 8; i++) {
        hash[i * 4    ] = (unsigned char)(ctx->state[i] >> 24);
        hash[i * 4 + 1] = (unsigned char)(ctx->state[i] >> 16);
        hash[i * 4 + 2] = (unsigned char)(ctx->state[i] >>  8);
        hash[i * 4 + 3] = (unsigned char)(ctx->state[i]);
    }
}

/*
 * sha256_hex
 *
 * Formats a 32-byte binary hash as a 64-character lowercase hex string
 * with a terminating NUL byte.  `out` must point to at least 65 bytes.
 */
void sha256_hex(const unsigned char hash[32], char out[65])
{
    static const char hex[] = "0123456789abcdef";
    unsigned int i;

    for (i = 0; i < 32; i++) {
        out[i * 2]     = hex[(hash[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[hash[i] & 0x0f];
    }
    out[64] = '\0';
}
