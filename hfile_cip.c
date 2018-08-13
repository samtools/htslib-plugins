/*  hfile_cip.c -- EGA en-/decryption backend for low-level file streams.

    Copyright (C) 2018 University of Glasgow

    Author: John Marshall <John.W.Marshall@glasgow.ac.uk>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.  */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined HAVE_OPENSSL
#include <openssl/aes.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#define BLOCKSIZE AES_BLOCK_SIZE

#elif defined HAVE_COMMONCRYPTO
#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonKeyDerivation.h>
#include <CommonCrypto/CommonRandom.h>

#define BLOCKSIZE kCCBlockSizeAES128

#else
#error No cryptography library specified
#endif

#include "htslib/hts.h"  // for hts_verbose
#include "hfile_internal.h"

typedef struct {
    hFILE base;
    unsigned is_write : 1;
    unsigned char *buffer;
    size_t bufsize;
    hFILE *rawfp;
#if defined HAVE_OPENSSL
    EVP_CIPHER_CTX ctx;
#elif defined HAVE_COMMONCRYPTO
    CCCryptorRef cryptor;
#endif
} hFILE_cip;

#if defined HAVE_OPENSSL

static int ssl_errno(const char *function)
{
    unsigned long err = ERR_get_error();

    if (hts_verbose >= 4) {
        fprintf(stderr, "[E::hfile_cip] %s() failed", function);
        if (err) {
            ERR_load_crypto_strings();
            fprintf(stderr, ": %s", ERR_error_string(err, NULL));
        }
        fprintf(stderr, "\n");
    }

    // TODO switch (err) { ... }
    return EINVAL;
}

static inline int gen_random(uint8_t *buffer, size_t length)
{
    if (RAND_bytes(buffer, length) <= 0)
        { errno = ssl_errno("RAND_bytes"); return -1; }
    return 0;
}

static inline ssize_t
cipher_update(hFILE_cip *fp, const void *in, void *out, size_t length)
{
    int n = length;
    if (! EVP_CipherUpdate(&fp->ctx, out, &n, in, length))
        { errno = ssl_errno("EVP_CipherUpdate"); return -1; }
    return n;
}

static inline ssize_t cipher_final(hFILE_cip *fp, void *out, size_t length)
{
    int n = length;
    if (! EVP_CipherFinal(&fp->ctx, out, &n))
        { errno = ssl_errno("EVP_CipherFinal"); return -1; }
    return n;
}

#elif defined HAVE_COMMONCRYPTO

static int cc_errno(CCStatus status, const char *function)
{
    if (hts_verbose >= 4)
        fprintf(stderr, "[E::hfile_cip] %s() failed: code %d\n",
                function, status);

    switch (status) {
    case kCCSuccess:            return 0;
    case kCCParamError:         return EINVAL;
    case kCCBufferTooSmall:     return ENOSPC;
    case kCCMemoryFailure:      return ENOMEM;
    case kCCAlignmentError:     return ERANGE;
    case kCCDecodeError:        return ERANGE;
    case kCCUnimplemented:      return ENOSYS;
    case kCCOverflow:           return EOVERFLOW;
    case kCCRNGFailure:         return ERANGE;
    case kCCUnspecifiedError:   return ERANGE;
    case kCCCallSequenceError:  return EBADF;
    }

    return EINVAL;
}

static inline int gen_random(uint8_t *buffer, size_t length)
{
    CCStatus ret = CCRandomGenerateBytes(buffer, length);
    if (ret != kCCSuccess)
        { errno = cc_errno(ret, "CCRandomGenerateBytes"); return -1; }
    return 0;
}

static inline ssize_t
cipher_update(hFILE_cip *fp, const void *in, void *out, size_t length)
{
    size_t n;
    CCStatus ret = CCCryptorUpdate(fp->cryptor, in, length, out, length, &n);
    if (ret != kCCSuccess)
        { errno = cc_errno(ret, "CCCryptorUpdate"); return -1; }
    return n;
}

static inline ssize_t cipher_final(hFILE_cip *fp, void *out, size_t length)
{
    size_t n;
    CCStatus ret = CCCryptorFinal(fp->cryptor, out, length, &n);
    if (ret != kCCSuccess)
        { errno = cc_errno(ret, "CCCryptorFinal"); return -1; }
    return n;
}

#endif

static ssize_t cip_read(hFILE *fpv, void *bufferv, size_t nbytes)
{
    hFILE_cip *fp = (hFILE_cip *) fpv;
    char *buffer = (char *) bufferv;
    ssize_t total = 0;

    while (nbytes > 0) {
        size_t n = (nbytes < fp->bufsize)? nbytes : fp->bufsize;
        ssize_t nread = hread(fp->rawfp, fp->buffer, n);
        if (nread == 0) break;
        else if (nread < 0) return -1;

        ssize_t nout = cipher_update(fp, fp->buffer, buffer, nread);
        if (nout < 0) return -1;

        buffer += nout;
        nbytes -= nout;
        total += nout;
    }

    return total;
}

static ssize_t cip_write(hFILE *fpv, const void *bufferv, size_t nbytes)
{
    hFILE_cip *fp = (hFILE_cip *) fpv;
    const char *buffer = (const char *) bufferv;
    ssize_t total = 0;

    while (nbytes > 0) {
        size_t n = (nbytes < fp->bufsize)? nbytes : fp->bufsize;
        ssize_t nout = cipher_update(fp, buffer, fp->buffer, n);
        if (nout < 0) return -1;

        if (hwrite(fp->rawfp, fp->buffer, nout) != nout) return -1;

        buffer += n;
        nbytes -= n;
        total += n;
    }

    return total;
}

static off_t cip_seek(hFILE *fpv, off_t offset, int whence)
{
    errno = ESPIPE;
    return -1;
}

static int cip_close(hFILE *fpv)
{
    hFILE_cip *fp = (hFILE_cip *) fpv;
    int err = 0;

    if (fp->is_write) {
        ssize_t nout = cipher_final(fp, fp->buffer, fp->bufsize);
        if (nout > 0) {
            if (hwrite(fp->rawfp, fp->buffer, nout) != nout) err = errno;
        }
        else if (nout < 0) err = errno;
    }

#if defined HAVE_OPENSSL
    if (! EVP_CIPHER_CTX_cleanup(&fp->ctx))
        err = ssl_errno("EVP_CIPHER_CTX_cleanup");
#elif defined HAVE_COMMONCRYPTO
    CCStatus ret = CCCryptorRelease(fp->cryptor);
    if (ret != kCCSuccess) err = cc_errno(ret, "CCCryptorRelease");
#endif

    if (hclose(fp->rawfp) < 0) err = errno;

    if (err) { errno = err; return -1; }
    else return 0;
}

static const struct hFILE_backend cip_backend =
{
    cip_read, cip_write, cip_seek, NULL, cip_close
};

static const char *strip_cip_scheme(const char *filename)
{
    if (strncmp(filename, "cip://localhost/", 16) == 0) filename += 15;
    else if (strncmp(filename, "cip:///", 7) == 0) filename += 6;
    else if (strncmp(filename, "cip:", 4) == 0) filename += 4;
    return filename;
}

static hFILE *hopen_cip(const char *filename, const char *mode)
{
    hFILE_cip *fp = NULL;
    int save;

    const char *key = getenv("HTS_CIP_KEY");
    if (key == NULL) { errno = EPERM; goto error; }

    fp = (hFILE_cip *) hfile_init(sizeof (hFILE_cip), mode, 0);
    if (fp == NULL) goto error;

    fp->rawfp = NULL;
    fp->buffer = NULL;

    fp->rawfp = hopen(strip_cip_scheme(filename), mode);
    if (fp->rawfp == NULL) goto error;

    fp->bufsize = 8192 * BLOCKSIZE;
    fp->buffer = malloc(fp->bufsize);

    int accmode = hfile_oflags(mode) & O_ACCMODE;

    uint8_t iv[16];
    if (accmode == O_RDONLY) {
        ssize_t n = hread(fp->rawfp, iv, sizeof iv);
        if (n < 0) goto error;
        if (n < sizeof iv) { errno = EDOM; goto error; }
        fp->is_write = 0;
    }
    else if (accmode == O_WRONLY) {
        if (gen_random(iv, sizeof iv) < 0) goto error;
        if (hwrite(fp->rawfp, iv, sizeof iv) != sizeof iv) goto error;
        fp->is_write = 1;
    }
    else { errno = EINVAL; goto error; }

    static const uint8_t salt[] = { 244, 34, 1, 0, 158, 223, 78, 21 };
    uint8_t secret[16];

#if defined HAVE_OPENSSL
    if (! PKCS5_PBKDF2_HMAC(key, -1, salt, sizeof salt, 1024, EVP_sha1(),
            sizeof secret, secret))
        { errno = ssl_errno("PKCS5_PBKDF2_HMAC"); goto error; }

    EVP_CIPHER_CTX_init(&fp->ctx);
    if (! EVP_CipherInit_ex(&fp->ctx, EVP_aes_128_ctr(), NULL, secret, iv,
            (accmode == O_WRONLY)))
        { errno = ssl_errno("EVP_CipherInit_ex"); goto error; }

#elif defined HAVE_COMMONCRYPTO
    CCStatus ret;
    ret = CCKeyDerivationPBKDF(kCCPBKDF2, key, strlen(key), salt, sizeof salt,
            kCCPRFHmacAlgSHA1, 1024, secret, sizeof secret);
    if (ret != kCCSuccess)
        { errno = cc_errno(ret, "CCKeyDerivationPBKDF"); goto error; }

    // Even though kCCModeOptionCTR_BE is deprecated, CCCryptorCreateWithMode()
    // fails (returning kCCUnimplemented) if it is not specified.
    CCOperation operation = (accmode == O_RDONLY)? kCCDecrypt : kCCEncrypt;
    ret = CCCryptorCreateWithMode(operation, kCCModeCTR, kCCAlgorithmAES, 0,
            iv, secret, sizeof secret, NULL, 0, 0, kCCModeOptionCTR_BE,
            &fp->cryptor);
    if (ret != kCCSuccess)
        { errno = cc_errno(ret, "CCCryptorCreateWithMode"); goto error; }
#endif

    fp->base.backend = &cip_backend;
    return &fp->base;

error:
    save = errno;
    if (fp) {
        if (fp->rawfp) hclose_abruptly(fp->rawfp);
        free(fp->buffer);
        hfile_destroy((hFILE *) fp);
    }
    errno = save;
    return NULL;
}

static int cip_isremote(const char *filename)
{
    return hisremote(strip_cip_scheme(filename));
}

int hfile_plugin_init(struct hFILE_plugin *self)
{
    static const struct hFILE_scheme_handler handler =
        { hopen_cip, cip_isremote, "cip", 50 };

    self->name = "cip";
    hfile_add_scheme_handler("cip", &handler);
    return 0;
}
