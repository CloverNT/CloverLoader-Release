#ifndef CLOVERNT_RUNTIME_NT_H
#define CLOVERNT_RUNTIME_NT_H

#include <CloverNT/Runtime/Kernel.h>

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef void(CloverNT_KERNEL_CALL* CloverPacketSink)(const uint8_t* data, size_t length, void* user);

CloverNT_KERNEL_API CloverBool CloverNT_KERNEL_CALL CloverPacketReady();

CloverNT_KERNEL_API CloverBool CloverNT_KERNEL_CALL CloverPacketSend(
        const char* command, size_t commandLength, const uint8_t* body, size_t bodyLength, uint32_t* outSequence);

CloverNT_KERNEL_API CloverBool CloverNT_KERNEL_CALL CloverPacketCall(const char*      command,
                                                                     size_t           commandLength,
                                                                     const uint8_t*   body,
                                                                     size_t           bodyLength,
                                                                     uint32_t         timeoutMs,
                                                                     CloverPacketSink sink,
                                                                     void*            user);

CloverNT_KERNEL_API CloverBool CloverNT_KERNEL_CALL CloverPacketReceiveByCommand(
        const char* command, size_t commandLength, uint32_t timeoutMs, CloverPacketSink sink, void* user);

CloverNT_KERNEL_API CloverBool CloverNT_KERNEL_CALL CloverPacketReceiveBySequence(uint32_t         sequence,
                                                                                  uint32_t         timeoutMs,
                                                                                  CloverPacketSink sink,
                                                                                  void*            user);

CloverNT_KERNEL_API CloverBool CloverNT_KERNEL_CALL CloverPacketCredential(CloverPacketSink sink, void* user);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* CLOVERNT_RUNTIME_NT_H */
