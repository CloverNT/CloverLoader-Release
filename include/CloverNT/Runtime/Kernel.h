#ifndef CLOVERNT_RUNTIME_KERNEL_H
#define CLOVERNT_RUNTIME_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#if defined(CloverNT_SYSTEM_WINDOWS)
    #define CloverNT_KERNEL_CALL __cdecl
    #if defined(CloverNT_KERNEL_EXPORTS)
        #define CloverNT_KERNEL_API __declspec(dllexport)
    #elif defined(CloverNT_KERNEL_STATIC)
        #define CloverNT_KERNEL_API
    #else
        #define CloverNT_KERNEL_API __declspec(dllimport)
    #endif
#else
    #define CloverNT_KERNEL_CALL
    #if defined(CloverNT_KERNEL_EXPORTS)
        #define CloverNT_KERNEL_API __attribute__((visibility("default")))
    #else
        #define CloverNT_KERNEL_API
    #endif
#endif

#if defined(__cplusplus)
extern "C" {
#endif

typedef uint64_t CloverHook;         /* inline/import hook handle (0 = invalid) */
typedef uint64_t CloverSubscription; /* event subscription handle (0 = invalid) */
typedef uint64_t CloverAddress;      /* process virtual address                */
typedef int32_t  CloverBool;         /* 0 = false, non-zero = true             */

CloverNT_KERNEL_API CloverHook CloverNT_KERNEL_CALL CloverHookInline(CloverAddress address,
                                                                     void*         detour,
                                                                     void**        outOriginal);
CloverNT_KERNEL_API CloverHook CloverNT_KERNEL_CALL CloverHookImport(const char* module,
                                                                     const char* symbol,
                                                                     void*       detour,
                                                                     void**      outOriginal);
CloverNT_KERNEL_API CloverBool CloverNT_KERNEL_CALL CloverHookRemove(CloverHook hook);

CloverNT_KERNEL_API CloverAddress CloverNT_KERNEL_CALL CloverModuleBase(const char* module);
CloverNT_KERNEL_API size_t CloverNT_KERNEL_CALL        CloverModuleSize(const char* module);

typedef enum {
    CLOVER_LOG_TRACE = 0,
    CLOVER_LOG_DEBUG = 1,
    CLOVER_LOG_INFO  = 2,
    CLOVER_LOG_WARN  = 3,
    CLOVER_LOG_ERROR = 4,
    CLOVER_LOG_FATAL = 5,
} CloverLogLevel;

CloverNT_KERNEL_API void CloverNT_KERNEL_CALL CloverLogWrite(CloverLogLevel level,
                                                             const char*    category,
                                                             const char*    message);

CloverNT_KERNEL_API void CloverNT_KERNEL_CALL CloverConsoleEmit(const char* text);

typedef void(CloverNT_KERNEL_CALL* CloverDllLoadCallback)(
        const char* baseName, const char* fullPath, CloverAddress base, size_t size, void* user);
CloverNT_KERNEL_API CloverSubscription CloverNT_KERNEL_CALL CloverDllLoadSubscribe(CloverDllLoadCallback callback,
                                                                                   void*                 user);
CloverNT_KERNEL_API void CloverNT_KERNEL_CALL               CloverDllLoadUnsubscribe(CloverSubscription subscription);

typedef struct CloverEvent {
    const char*    name;
    const uint8_t* data;
    size_t         length;
    uint8_t        id[16];
    CloverBool     cancelled;
    const uint8_t* replacement;
    size_t         replacementLength;
} CloverEvent;

typedef void(CloverNT_KERNEL_CALL* CloverEventCallback)(CloverEvent* event, void* user);
CloverNT_KERNEL_API CloverSubscription CloverNT_KERNEL_CALL CloverEventSubscribe(const char*         name,
                                                                                 CloverEventCallback callback,
                                                                                 void*               user);
CloverNT_KERNEL_API void CloverNT_KERNEL_CALL               CloverEventUnsubscribe(CloverSubscription subscription);
CloverNT_KERNEL_API void CloverNT_KERNEL_CALL               CloverEventEmit(CloverEvent* event);

CloverNT_KERNEL_API const char* CloverNT_KERNEL_CALL CloverPluginsDirectory();
CloverNT_KERNEL_API const char* CloverNT_KERNEL_CALL CloverQqVersion();

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* CLOVERNT_RUNTIME_KERNEL_H */
