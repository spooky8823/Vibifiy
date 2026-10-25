#ifndef VIBRIUM_ADBLOCK_H_
#define VIBRIUM_ADBLOCK_H_

#include <stddef.h>
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

/*
 * Validates a downloaded filter-list response.
 *
 * Pass SIZE_MAX for content_length when the HTTP Content-Length
 * header is unavailable. body may be NULL only when body_len is zero.
 *
 * Returns 0 on success and VIBRIUM_ADBLOCK_ERROR on failure.
 * Validation does not install the rules into an engine.
 */
int32_t vibrium_adblock_validate_filter_response(
    const char* name,
    const char* source_url,
    uint16_t status,
    size_t content_length,
    const uint8_t* body,
    size_t body_len);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VIBRIUM_ADBLOCK_H_
