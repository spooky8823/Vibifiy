#ifndef VIBRIUM_ADBLOCK_H_
#define VIBRIUM_ADBLOCK_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VIBRIUM_ADBLOCK_ABI_VERSION 3

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

/*
 * Thread safety: a live handle may be used by vibrium_adblock_check() and the
 * vibrium_adblock_cosmetic_*() / vibrium_adblock_generic_css() functions from
 * any number of threads concurrently.
 * vibrium_adblock_destroy() must not run while another call on the same
 * handle is in progress; the host must ensure that (e.g. reference counting).
 */

/*
 * Creates an engine from every "<name>.txt" filter list in `directory`.
 * Unreadable or invalid lists are skipped. Returns NULL if no usable rules
 * could be loaded.
 */
VibriumAdblockHandle vibrium_adblock_create_from_directory(
    const char* directory);

/*
 * Returns CSS that hides elements targeted by cosmetic filters on page_url,
 * one rule per selector. Returns NULL when there is nothing to hide, the URL
 * is not http(s), or on error. Free a non-NULL result with
 * vibrium_adblock_string_free().
 */
char* vibrium_adblock_cosmetic_css(
    VibriumAdblockHandle handle,
    const char* page_url);

/*
 * Page-specific cosmetic data for page_url as a JSON object:
 *   {"css": string, "procedural": [string], "generichide": bool}
 * "css" hides plain selectors, "procedural" holds filters that need in-page
 * logic (:has-text(), :upward(), :remove(), :style(), ...) and "generichide"
 * is true when the page must not ask for generic class/id selectors.
 * Returns NULL when the page has nothing to do, the URL is not http(s), or on
 * error. Free a non-NULL result with vibrium_adblock_string_free().
 */
char* vibrium_adblock_cosmetic_payload(
    VibriumAdblockHandle handle,
    const char* page_url);

/*
 * Scriptlet JavaScript that filter lists inject into page_url. Needs a
 * resources.json next to the filter lists; returns NULL otherwise, when no
 * scriptlet rule matches, or on error. Free with vibrium_adblock_string_free().
 */
char* vibrium_adblock_cosmetic_script(
    VibriumAdblockHandle handle,
    const char* page_url);

/*
 * CSS hiding the generic (##.class / ###id) matches for the class names and
 * ids a page reports. classes_json and ids_json are JSON arrays of strings,
 * at most 256 KiB each; they come from an untrusted page, so malformed or
 * oversized input returns NULL. Also returns NULL when nothing matches,
 * $generichide applies, the URL is not http(s), or on error.
 * Free a non-NULL result with vibrium_adblock_string_free().
 */
char* vibrium_adblock_generic_css(
    VibriumAdblockHandle handle,
    const char* page_url,
    const char* classes_json,
    const char* ids_json);

/*
 * Frees a string returned by vibrium_adblock_cosmetic_css(),
 * vibrium_adblock_cosmetic_payload(), vibrium_adblock_cosmetic_script() or
 * vibrium_adblock_generic_css(). NULL is a no-op.
 */
void vibrium_adblock_string_free(char* string);

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
