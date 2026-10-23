#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
static int fail_call(const char *operation, unsigned count) {
    const char *target=getenv("DOE_TEST_FAIL_OPERATION");
    const char *number=getenv("DOE_TEST_FAIL_CALL");
    return target && number && strcmp(target,operation)==0 && count==(unsigned)strtoul(number,NULL,10);
}
ssize_t write(int fd,const void *buffer,size_t n) {
    static unsigned count;
    ssize_t (*real)(int,const void *,size_t)=(ssize_t (*)(int,const void *,size_t))dlsym(RTLD_NEXT,"write");
    if(fd>2 && fail_call("write",++count)){errno=ENOSPC;return -1;}
    return real(fd,buffer,n);
}
int close(int fd) {
    static unsigned count;
    int (*real)(int)=(int (*)(int))dlsym(RTLD_NEXT,"close");
    int result=real(fd);
    if(fd>2 && fail_call("close",++count)){errno=EIO;return -1;}
    return result;
}
int link(const char *old,const char *newpath) {
    static unsigned count;
    int (*real)(const char *,const char *)=(int (*)(const char *,const char *))dlsym(RTLD_NEXT,"link");
    if(fail_call("link",++count)){errno=EIO;return -1;}
    return real(old,newpath);
}
