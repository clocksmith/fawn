/* SPDX-License-Identifier: MIT */
#include "../src/objc_abi.h"
#include <assert.h>
#include <stdio.h>

typedef struct object { unsigned refs; } object;
static object objects[256], device_a, device_b, string_class;
static size_t used;
static unsigned libraries, functions;
static bool fail_library;
static char *mutate_source;
#ifdef DOE_CACHE_TEST_WRAP
static bool fail_allocate;
void *__real_malloc(size_t);
void *__wrap_malloc(size_t bytes) { return fail_allocate ? NULL : __real_malloc(bytes); }
#endif
static bool is(void *selector, const char *name) { return !strcmp(selector,name); }
static object *new_object(void) { assert(used < 256); objects[used].refs = 1; return &objects[used++]; }
static void *object_call(void *receiver, void *selector)
{
    if (is(selector,"alloc")) { assert(receiver == &string_class); return new_object(); }
    assert(is(selector,"retain")); object *value = receiver; assert(value->refs); ++value->refs; return receiver;
}
static void void_call(void *receiver, void *selector)
{
    assert(is(selector,"release")); object *value = receiver; assert(value->refs); --value->refs;
}
static void *string_init(void *receiver, void *selector, const void *bytes, size_t length, size_t encoding)
{
    assert(is(selector,"string") && (bytes || !length) && encoding == 4); return receiver;
}
static void *library_call(void *receiver, void *selector, void *source, void *options, void **error)
{
    assert((receiver == &device_a || receiver == &device_b) && is(selector,"library") && source && !options && error);
    ++libraries;
    if (mutate_source) { mutate_source[0] = 'M'; mutate_source = NULL; }
    return fail_library ? NULL : new_object();
}
static void *named_call(void *receiver, void *selector, void *name)
{
    assert(((object *)receiver)->refs && is(selector,"function") && name); ++functions; return new_object();
}
static doe_mtl_status compile(doe_mtl_objc *runtime, void *device, const char *source, size_t length, const char *entry)
{
    void *function = NULL;
    doe_mtl_error error;
    doe_mtl_status result = doe_mtl_objc_function(runtime,device,source,length,entry,&function,&error);
    if (result == DOE_MTL_OK) { assert(function); void_call(function,"release"); }
    else assert(!function);
    return result;
}
int main(void)
{
    device_a.refs = device_b.refs = 1;
    doe_mtl_objc runtime = {.object_call=object_call,.void_call=void_call,.string_init_call=string_init,
        .library_call=library_call,.named_call=named_call,.string_class=&string_class,
        .alloc="alloc",.retain="retain",.release="release",.string_init="string",
        .new_library="library",.new_function="function"};
    doe_mtl_objc_library_cache_open(&runtime);
    assert(runtime.libraries);
    char source[] = "a\0bcd";
    assert(compile(&runtime,&device_a,source,5,"one") == DOE_MTL_OK);
    assert(compile(&runtime,&device_a,source,5,"two") == DOE_MTL_OK);
    assert(libraries == 1 && functions == 2);
    source[2] = 'B';
    assert(compile(&runtime,&device_a,source,5,"one") == DOE_MTL_OK);
    assert(libraries == 2);
    source[2] = 'b';
    assert(compile(&runtime,&device_a,source,5,"one") == DOE_MTL_OK && libraries == 2);
    assert(compile(&runtime,&device_b,source,5,"one") == DOE_MTL_OK && libraries == 3);
    assert(compile(&runtime,&device_a,source,4,"one") == DOE_MTL_OK && libraries == 4);
    fail_library = true;
    assert(compile(&runtime,&device_a,"bad",3,"one") == DOE_MTL_NATIVE_FAILURE);
    assert(compile(&runtime,&device_a,"bad",3,"one") == DOE_MTL_NATIVE_FAILURE && libraries == 6);
    fail_library = false;
#ifdef DOE_CACHE_TEST_WRAP
    fail_allocate = true;
    assert(compile(&runtime,&device_a,"oom",3,"one") == DOE_MTL_OK);
    fail_allocate = false;
    assert(compile(&runtime,&device_a,"oom",3,"one") == DOE_MTL_OK && libraries == 8);
#else
    assert(compile(&runtime,&device_a,"oom",3,"one") == DOE_MTL_OK && libraries == 7);
#endif
    unsigned before_empty = libraries;
    assert(compile(&runtime,&device_a,"",0,"one") == DOE_MTL_OK);
    assert(compile(&runtime,&device_a,"",0,"one") == DOE_MTL_OK && libraries == before_empty + 2);
    for (unsigned i = 0; i < 20; ++i) {
        char bytes[2] = {(char)i,'x'};
        assert(compile(&runtime,&device_a,bytes,2,"one") == DOE_MTL_OK);
    }
    assert(compile(&runtime,&device_a,source,5,"one") == DOE_MTL_OK && libraries == before_empty + 23);
    char changing[] = "mut";
    mutate_source = changing;
    unsigned before_mutation = libraries;
    assert(compile(&runtime,&device_a,changing,3,"one") == DOE_MTL_OK && changing[0] == 'M');
    assert(compile(&runtime,&device_a,"mut",3,"one") == DOE_MTL_OK && libraries == before_mutation+1);
    assert(compile(&runtime,&device_a,changing,3,"one") == DOE_MTL_OK && libraries == before_mutation+2);
    doe_mtl_objc_library_cache_close(&runtime);
    doe_mtl_objc_library_cache_close(&runtime);
    for (size_t i = 0; i < used; ++i) assert(objects[i].refs == 0);
    assert(device_a.refs == 1 && device_b.refs == 1);
    puts("Metal library cache: source/device identity, entries, failure/OOM, eviction and release passed (host call ledger)");
    return 0;
}
