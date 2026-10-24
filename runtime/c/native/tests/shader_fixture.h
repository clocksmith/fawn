/* SPDX-License-Identifier: MIT
 * Bounded, byte-preserving source input for public native consumer fixtures. */
#ifndef DOE_NATIVE_TEST_SHADER_FIXTURE_H
#define DOE_NATIVE_TEST_SHADER_FIXTURE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void native_test_shader_error(const char *path,const char *reason)
{
    fprintf(stderr,"shader fixture %s: %s\n",path,reason);
    exit(2);
}
static inline char *native_test_shader(const char *path)
{
    FILE *file=fopen(path,"rb");
    if (!file) native_test_shader_error(path,"open failed");
    if (fseek(file,0,SEEK_END)) native_test_shader_error(path,"seek failed");
    const long length=ftell(file);
    if (length<=0 || length>65536 || fseek(file,0,SEEK_SET))
        native_test_shader_error(path,"source must be nonempty and at most 65536 bytes");
    const size_t bytes=(size_t)length;
    char *source=malloc(bytes+1);
    if (!source) native_test_shader_error(path,"allocation failed");
    if (fread(source,1,bytes,file)!=bytes || fgetc(file)!=EOF || ferror(file))
        native_test_shader_error(path,"short read, I/O failure or changing file extent");
    if (fclose(file)) native_test_shader_error(path,"close failed");
    if (memchr(source,0,bytes)) native_test_shader_error(path,"embedded NUL byte");
    source[bytes]='\0';
    return source;
}
#endif
