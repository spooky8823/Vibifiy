#ifndef VIBRIUM_ADBLOCK_H_
#define VIBRIUM_ADBLOCK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VIBRIUM_ADBLOCK_ABI_VERSION 1

typedef void* VibriumAdblockHandle;

enum VibriumAdblockResult {
  VIBRIUM_ADBLOCK_ERROR = -1,
  VIBRIUM_ADBLOCK_ALLOWED = 0,
  VIBRIUM_ADBLOCK_BLOCKED = 1
};

VibriumAdblockHandle vibrium_adblock_create(void);

VibriumAdblockHandle vibrium_adblock_create_with_rules(
    const char* rules);

void vibrium_adblock_destroy(VibriumAdblockHandle handle);

int32_t vibrium_adblock_check(
    VibriumAdblockHandle handle,
    const char* url,
    const char* source_url,
    const char* request_type);

uint32_t vibrium_adblock_abi_version(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VIBRIUM_ADBLOCK_H_