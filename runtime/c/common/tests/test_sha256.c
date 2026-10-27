/* SPDX-License-Identifier: MIT
 * Frozen independent hashlib references cover padding, alignment and aliasing. */
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#include <stddef.h>
#include <string.h>
static unsigned platform_calls;
static unsigned char *traced_sha256(const void *data, CC_LONG size, unsigned char *out)
{
    ++platform_calls;
#if defined(DOE_SHA_TEST_PLATFORM_FAILURE)
    (void)data; (void)size;
    memset(out, 0xa5, CC_SHA256_DIGEST_LENGTH);
    return NULL;
#else
    return CC_SHA256(data,size,out);
#endif
}
#define CC_SHA256 traced_sha256
#endif
#include "doe_sha256.h"
#if defined(__APPLE__)
#undef CC_SHA256
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static const struct vector { size_t size; const char *sha256; } vectors[] = {
    {0u,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {1u,"e77b9a9ae9e30b0dbdb6f510a264ef9de781501d7b6b92ae89eb059c5ab743db"},
    {3u,"11e5515b1df875667f420ad56b753d049451453321069476130d9916aed78ea1"},
    {55u,"63f52ae0ec1391993ee650b77287900a094013bdd9cf28120f9ff7c703791ddd"},
    {56u,"3d369c762eaea64e0dfb8cf4dde147ff4138fe1fee32fa93cb318302264c1f32"},
    {63u,"b73e65e88fbed8af578fad59b7f5ed237d73b5f5f0ebd608ef9d041073262928"},
    {64u,"4806b7815935102d84ecbaa7958da9b6b1911f10fb4ce1a5670480d125a5f9bd"},
    {65u,"fc9d4e5c2fdb736db73b59b1833f11606ff98a324a5fe08bab69fcf77ff7d7ca"},
    {119u,"251242f0ca79a6c005977aa95bef1cd9a6ed288ec874f8fc861ed3c60116e379"},
    {120u,"0556360a4457b879efbd578bc223bb7ac04f2a834f62bff292c7fb9bcbfc7765"},
    {127u,"e35f659e6963ee3a1a98e77963872dee26ae04aaaa3bd5b9ec8bd40be9b8215f"},
    {128u,"01868e5ca495c97ee8e69c286ee53826f6bc4ed2ad867bd5bd830360e1f285e2"},
    {129u,"931dcfb12cd8448fbbc3830e7683d2ac3407f1fa9103aa584e46f5a9f655265a"},
    {1024u,"745bd149c063613387c80fc5741736c459762499ea28b35a4b62f1ca7a2d2b10"},
    {4097u,"b33dbcdeefec003e5376aec8ebb442ead36bd1e2ae4b099a174c122676f6108f"},
    {1048576u,"c25b4e8de7c9feaa03db075a965c9a2e75425a19b894a6025ba0044512fc1751"},
};
int main(void)
{
    unsigned char *storage = malloc(1048576u+64u);
    assert(storage);
    unsigned checks = 0;
    for (size_t v=0; v<sizeof(vectors)/sizeof(vectors[0]); ++v) {
        for (size_t offset=0; offset<8; ++offset) {
            unsigned char *bytes=storage+offset, digest[32];
            char hex[65];
            for (size_t i=0; i<vectors[v].size; ++i) bytes[i]=(unsigned char)(i*37u+5u);
            doe_common_sha256(bytes,vectors[v].size,digest);
            doe_common_sha256_hex(digest,hex);
            assert(!strcmp(hex,vectors[v].sha256));
            ++checks;
            /* Input/output may overlap: final digest must follow all reads. */
            doe_common_sha256(bytes,vectors[v].size,bytes+1);
            doe_common_sha256_hex(bytes+1,hex);
            assert(!strcmp(hex,vectors[v].sha256));
            ++checks;
        }
    }
    unsigned char digest[32]; char hex[65];
    doe_common_sha256(NULL,0,digest);
    doe_common_sha256_hex(digest,hex);
    assert(!strcmp(hex,vectors[0].sha256));
    ++checks;
    doe_common_sha256("abc",3,digest);
    doe_common_sha256_hex(digest,hex);
    assert(!strcmp(hex,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    ++checks;
#if defined(__APPLE__)
    assert(platform_calls==checks);
#endif
    free(storage);
    printf("SHA-256: %u independent padding/alignment/alias/empty controls passed\n",checks);
    return 0;
}
