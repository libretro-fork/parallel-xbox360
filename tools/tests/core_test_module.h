/* Native core loading for C89 frontend fixtures. */
#ifndef XE_CORE_TEST_MODULE_H
#define XE_CORE_TEST_MODULE_H
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HMODULE CoreModule;
typedef FARPROC CoreSymbol;
#define CORE_LOAD(path) LoadLibraryA(path)
#define CORE_FIND(module, name) GetProcAddress(module, name)
#define CORE_CLOSE(module) FreeLibrary(module)
#else
#include <dlfcn.h>
typedef void *CoreModule;
typedef void *CoreSymbol;
#define CORE_LOAD(path) dlopen(path, RTLD_NOW | RTLD_LOCAL)
#define CORE_FIND(module, name) dlsym(module, name)
#define CORE_CLOSE(module) (dlclose(module) == 0)
#endif
#endif
