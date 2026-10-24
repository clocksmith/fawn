/* SPDX-License-Identifier: MIT
 * Actual Win32 CPU dependency/Unicode/error control; creates no GPU or provider. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../doe_napi_library.h"

enum { PATH_UNITS = 32768 };
static void path_join(WCHAR out[PATH_UNITS],const WCHAR *root,const WCHAR *name)
{
    size_t prefix = wcslen(root), suffix = wcslen(name);
    assert(prefix + suffix + 2 <= PATH_UNITS);
    memcpy(out,root,prefix * sizeof(*out));
    out[prefix] = L'\\';
    memcpy(out + prefix + 1,name,(suffix + 1) * sizeof(*out));
}
static char *utf8(const WCHAR *path)
{
    int bytes = WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path,-1,NULL,0,NULL,NULL);
    assert(bytes > 0);
    char *result = malloc((size_t)bytes);
    assert(result && WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path,-1,result,bytes,NULL,NULL) == bytes);
    return result;
}
static void rejected(const char *path,size_t size,const char *operation,DWORD native)
{
    doe_library_error error = {0};
    assert(!doe_library_open(path,size,&error));
    assert(error.operation && !strcmp(error.operation,operation));
    assert(error.native_code == native && error.message[0]);
    assert(GetLastError() == native);
}
int wmain(int argc,WCHAR **argv)
{
    assert(argc == 2);
    WCHAR source[PATH_UNITS], destination[PATH_UNITS], staged[PATH_UNITS], absent[PATH_UNITS];
    path_join(staged,argv[1],L"staged-\u03bb-\u65e5");
    path_join(absent,argv[1],L"missing-dependency");
    assert(CreateDirectoryW(staged,NULL) && CreateDirectoryW(absent,NULL));
    assert(SetCurrentDirectoryW(argv[1])); /* Outside the staged DLL directory. */
    /* A real import in the application/CWD must not rescue the missing sibling.
     * The selected directory plus System32 are the only additional search roots. */
    path_join(source,argv[1],L"fixtures\\doe_addon_probe_dependency.dll");
    path_join(destination,argv[1],L"doe_addon_probe_dependency.dll");
    assert(CopyFileW(source,destination,TRUE));
    path_join(source,argv[1],L"fixtures\\doe_addon_probe_provider.dll");
    path_join(destination,absent,L"doe_addon_probe_provider.dll");
    assert(CopyFileW(source,destination,TRUE));
    char *path = utf8(destination);
    rejected(path,strlen(path),"LoadLibraryExW.provider",ERROR_MOD_NOT_FOUND);
    free(path);
    assert(!GetModuleHandleW(L"doe_addon_probe_dependency.dll"));
    path_join(destination,argv[1],L"doe_addon_probe_dependency.dll");
    assert(DeleteFileW(destination));
    path_join(destination,staged,L"doe_addon_probe_provider.dll");
    assert(CopyFileW(source,destination,TRUE));
    path_join(source,argv[1],L"fixtures\\doe_addon_probe_dependency.dll");
    path_join(destination,staged,L"doe_addon_probe_dependency.dll");
    assert(CopyFileW(source,destination,TRUE));
    rejected(NULL,0,"provider.path",ERROR_INVALID_PARAMETER);
    rejected("",0,"provider.path",ERROR_INVALID_PARAMETER);
    const char embedded[] = {'a',0,'b'};
    rejected(embedded,sizeof(embedded),"provider.path",ERROR_INVALID_PARAMETER);
    const char invalid[] = {(char)0xc0,(char)0xaf};
    rejected(invalid,sizeof(invalid),"MultiByteToWideChar.provider",ERROR_NO_UNICODE_TRANSLATION);
    rejected("x",(size_t)INT_MAX + 1,"provider.path",ERROR_INVALID_PARAMETER);
    char *long_path = malloc(PATH_UNITS);
    assert(long_path);
    memset(long_path,'x',PATH_UNITS);
    rejected(long_path,PATH_UNITS,"provider.path.length",ERROR_FILENAME_EXCED_RANGE);
    free(long_path);
    path_join(destination,staged,L"not-there.dll");
    path = utf8(destination);
    rejected(path,strlen(path),"LoadLibraryExW.provider",ERROR_MOD_NOT_FOUND);
    free(path);
    path_join(destination,staged,L"doe_addon_probe_provider.dll");
    path = utf8(destination);
    doe_library_error untouched;
    memset(&untouched,0xa5,sizeof(untouched));
    doe_library_error saved = untouched;
    HMODULE loaded = doe_library_open(path,strlen(path),&untouched);
    assert(loaded && !memcmp(&untouched,&saved,sizeof(saved)));
    free(path);
    int (*probe)(void) = (int (*)(void))GetProcAddress(loaded,"doe_addon_loader_probe");
    assert(probe && probe() == 1256);
    path = utf8(L"staged-\u03bb-\u65e5\\doe_addon_probe_provider.dll");
    HMODULE relative = doe_library_open(path,strlen(path),NULL);
    assert(relative == loaded);
    free(path);
    assert(FreeLibrary(relative) && FreeLibrary(loaded));
    assert(!GetModuleHandleW(L"doe_addon_probe_dependency.dll"));
    assert(!GetModuleHandleW(L"doe_addon_probe_provider.dll"));
    assert(DeleteFileW(destination));
    path_join(destination,staged,L"doe_addon_probe_dependency.dll");
    assert(DeleteFileW(destination));
    assert(RemoveDirectoryW(staged));
    path_join(destination,absent,L"doe_addon_probe_provider.dll");
    assert(DeleteFileW(destination) && RemoveDirectoryW(absent));
    puts("PASS actual Win32 provider loader: Unicode path, sibling import, relative input, no dependent search fallback, malformed/oversize/embedded-NUL bounds, owned first errors and release; CPU only");
    return 0;
}
