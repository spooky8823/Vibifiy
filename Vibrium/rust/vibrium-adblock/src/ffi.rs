use std::ffi::{CStr, CString, c_void};
use std::os::raw::c_char;
use std::panic::{AssertUnwindSafe, catch_unwind};

use crate::engine::VibriumAdblock;
use crate::filter_list_updater::validate_downloaded_response;
use crate::filter_lists::FilterList;
use crate::filter_validation::validate_and_clean_filter_list;

const ABI_VERSION: u32 = 3;

/// Largest JSON array of class names / ids accepted from a page.
const MAX_NAMES_JSON_BYTES: usize = 256 * 1024;
const ERROR: i32 = -1;

/// Creates an engine with no custom filter rules.
///
/// # Safety
///
/// This function has no pointer arguments and imposes no additional
/// safety requirements on the caller.
#[unsafe(no_mangle)]
pub extern "C" fn vibrium_adblock_create() -> *mut c_void {
    catch_unwind(|| Box::into_raw(Box::new(VibriumAdblock::new(&[]))).cast::<c_void>())
        .unwrap_or(std::ptr::null_mut())
}

/// Creates an ad-blocking engine from validated filter rules.
///
/// Returns null if the input is null, invalid UTF-8, contains no usable
/// rules, or engine creation fails.
///
/// # Safety
///
/// If `rules` is non-null, it must point to a valid, readable,
/// null-terminated C string that remains valid for the duration of this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vibrium_adblock_create_with_rules(rules: *const c_char) -> *mut c_void {
    if rules.is_null() {
        return std::ptr::null_mut();
    }

    catch_unwind(AssertUnwindSafe(|| {
        let input = unsafe { CStr::from_ptr(rules) };
        let input = input.to_str().ok()?;

        let (cleaned_rules, _) = validate_and_clean_filter_list(input).ok()?;

        if cleaned_rules.trim().is_empty() {
            return None;
        }

        Some(
            Box::into_raw(Box::new(VibriumAdblock::from_filter_list(&cleaned_rules)))
                .cast::<c_void>(),
        )
    }))
    .ok()
    .flatten()
    .unwrap_or(std::ptr::null_mut())
}

/// Destroys an engine created by one of the creation functions.
///
/// # Safety
///
/// `handle` must be null or a valid, live handle returned by an engine
/// creation function that has not already been destroyed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vibrium_adblock_destroy(handle: *mut c_void) {
    if handle.is_null() {
        return;
    }

    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        drop(Box::from_raw(handle.cast::<VibriumAdblock>()));
    }));
}

/// Checks whether a network request should be blocked.
///
/// Returns `1` if blocked, `0` if allowed, and `-1` on invalid input
/// or a request-processing error.
///
/// # Safety
///
/// `handle` must point to a valid, live engine instance. Each string
/// pointer must point to a valid, readable, null-terminated C string
/// for the duration of this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vibrium_adblock_check(
    handle: *mut c_void,
    url: *const c_char,
    source_url: *const c_char,
    request_type: *const c_char,
) -> i32 {
    if handle.is_null() || url.is_null() || source_url.is_null() || request_type.is_null() {
        return ERROR;
    }

    catch_unwind(AssertUnwindSafe(|| unsafe {
        let engine = &*handle.cast::<VibriumAdblock>();

        let url = CStr::from_ptr(url).to_str().ok()?;
        let source_url = CStr::from_ptr(source_url).to_str().ok()?;
        let request_type = CStr::from_ptr(request_type).to_str().ok()?;

        Some(match engine.should_block(url, source_url, request_type) {
            Ok(true) => 1,
            Ok(false) => 0,
            Err(_) => ERROR,
        })
    }))
    .ok()
    .flatten()
    .unwrap_or(ERROR)
}

/// Validates a filter-list response downloaded by the host browser.
///
/// Returns 0 on success and -1 on failure. This validates the downloaded
/// text only; it does not install the rules into an engine or persist them.
///
/// Use `usize::MAX` for `content_length` when the server did not report it.
///
/// # Safety
///
/// `name` and `source_url` must point to valid, readable, null-terminated
/// C strings. If `body_len` is nonzero, `body` must point to at least
/// `body_len` readable bytes. All pointers must remain valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vibrium_adblock_validate_filter_response(
    name: *const c_char,
    source_url: *const c_char,
    status: u16,
    content_length: usize,
    body: *const u8,
    body_len: usize,
) -> i32 {
    if name.is_null()
        || source_url.is_null()
        || (body.is_null() && body_len != 0)
        || body_len > crate::filter_list_updater::MAX_FILTER_LIST_SIZE
    {
        return ERROR;
    }

    catch_unwind(AssertUnwindSafe(|| unsafe {
        let name = CStr::from_ptr(name).to_str().ok()?;
        let source_url = CStr::from_ptr(source_url).to_str().ok()?;

        let bytes = if body_len == 0 {
            Vec::new()
        } else {
            std::slice::from_raw_parts(body, body_len).to_vec()
        };

        let reported_length = if content_length == usize::MAX {
            None
        } else {
            Some(content_length)
        };

        let list = FilterList::new(name, source_url);

        validate_downloaded_response(&list, status, reported_length, bytes).ok()?;

        Some(0)
    }))
    .ok()
    .flatten()
    .unwrap_or(ERROR)
}

/// Creates an engine from every `<name>.txt` filter list in `directory`.
///
/// Unreadable or invalid lists are skipped. Returns null if `directory` is
/// null, not valid UTF-8, unreadable, or yields no usable rules.
///
/// # Safety
///
/// If `directory` is non-null, it must point to a valid, readable,
/// null-terminated C string that remains valid for the duration of this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vibrium_adblock_create_from_directory(
    directory: *const c_char,
) -> *mut c_void {
    if directory.is_null() {
        return std::ptr::null_mut();
    }

    catch_unwind(AssertUnwindSafe(|| {
        let directory = unsafe { CStr::from_ptr(directory) }.to_str().ok()?;
        let (engine, _skipped) =
            VibriumAdblock::from_directory(std::path::Path::new(directory)).ok()?;
        Some(Box::into_raw(Box::new(engine)).cast::<c_void>())
    }))
    .ok()
    .flatten()
    .unwrap_or(std::ptr::null_mut())
}

/// Returns CSS hiding the elements that cosmetic filters target on `page_url`.
///
/// Returns null when there is nothing to hide, the URL is not http(s), or an
/// error occurs. A non-null result must be released with
/// `vibrium_adblock_string_free`.
///
/// # Safety
///
/// `handle` must be a valid, live engine. `page_url` must point to a valid,
/// readable, null-terminated C string for the duration of this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vibrium_adblock_cosmetic_css(
    handle: *mut c_void,
    page_url: *const c_char,
) -> *mut c_char {
    if handle.is_null() || page_url.is_null() {
        return std::ptr::null_mut();
    }

    catch_unwind(AssertUnwindSafe(|| unsafe {
        let engine = &*handle.cast::<VibriumAdblock>();
        let url = CStr::from_ptr(page_url).to_str().ok()?;
        let css = engine.cosmetic_css(url).ok()??;
        CString::new(css).ok().map(CString::into_raw)
    }))
    .ok()
    .flatten()
    .unwrap_or(std::ptr::null_mut())
}

/// Releases a string returned by `vibrium_adblock_cosmetic_css`,
/// `vibrium_adblock_cosmetic_payload`, `vibrium_adblock_cosmetic_script` or
/// `vibrium_adblock_generic_css`.
///
/// # Safety
///
/// `string` must be null or a pointer returned by this library that has not
/// already been freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vibrium_adblock_string_free(string: *mut c_char) {
    if string.is_null() {
        return;
    }
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        drop(CString::from_raw(string));
    }));
}

/// Returns the page-specific cosmetic data for `page_url` as a JSON object:
/// `{"css": string, "procedural": [string], "generichide": bool}`.
///
/// Returns null when the page has nothing to do (no hiding rules, no
/// procedural filters and generic selectors disabled), the URL is not http(s),
/// or an error occurs. A non-null result must be released with
/// `vibrium_adblock_string_free`.
///
/// # Safety
///
/// `handle` must be a valid, live engine. `page_url` must point to a valid,
/// readable, null-terminated C string for the duration of this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vibrium_adblock_cosmetic_payload(
    handle: *mut c_void,
    page_url: *const c_char,
) -> *mut c_char {
    if handle.is_null() || page_url.is_null() {
        return std::ptr::null_mut();
    }

    catch_unwind(AssertUnwindSafe(|| unsafe {
        let engine = &*handle.cast::<VibriumAdblock>();
        let url = CStr::from_ptr(page_url).to_str().ok()?;
        let payload = engine.cosmetic_payload(url).ok()?;

        if payload.is_noop() {
            return None;
        }

        CString::new(payload.to_json()).ok().map(CString::into_raw)
    }))
    .ok()
    .flatten()
    .unwrap_or(std::ptr::null_mut())
}

/// Returns the scriptlet JavaScript that filter lists inject into `page_url`.
///
/// Returns null when there is none (no matching scriptlet rule, or no
/// `resources.json` installed), the URL is not http(s), or an error occurs. A
/// non-null result must be released with `vibrium_adblock_string_free`.
///
/// # Safety
///
/// `handle` must be a valid, live engine. `page_url` must point to a valid,
/// readable, null-terminated C string for the duration of this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vibrium_adblock_cosmetic_script(
    handle: *mut c_void,
    page_url: *const c_char,
) -> *mut c_char {
    if handle.is_null() || page_url.is_null() {
        return std::ptr::null_mut();
    }

    catch_unwind(AssertUnwindSafe(|| unsafe {
        let engine = &*handle.cast::<VibriumAdblock>();
        let url = CStr::from_ptr(page_url).to_str().ok()?;
        let script = engine.injected_script(url).ok()?;

        if script.is_empty() {
            return None;
        }

        CString::new(script).ok().map(CString::into_raw)
    }))
    .ok()
    .flatten()
    .unwrap_or(std::ptr::null_mut())
}

/// Returns CSS hiding the generic (`##.class` / `###id`) matches for the
/// class names and ids found on `page_url`.
///
/// `classes_json` and `ids_json` are JSON arrays of strings (for example
/// `["ad","banner"]`), each at most 256 KiB. They come from an untrusted page,
/// so malformed or oversized input yields null rather than an error.
///
/// Returns null when nothing matches, `$generichide` applies to the page, the
/// URL is not http(s), or on error. A non-null result must be released with
/// `vibrium_adblock_string_free`.
///
/// # Safety
///
/// `handle` must be a valid, live engine. All three strings must be valid,
/// readable, null-terminated C strings for the duration of this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vibrium_adblock_generic_css(
    handle: *mut c_void,
    page_url: *const c_char,
    classes_json: *const c_char,
    ids_json: *const c_char,
) -> *mut c_char {
    if handle.is_null() || page_url.is_null() || classes_json.is_null() || ids_json.is_null() {
        return std::ptr::null_mut();
    }

    catch_unwind(AssertUnwindSafe(|| unsafe {
        let engine = &*handle.cast::<VibriumAdblock>();
        let url = CStr::from_ptr(page_url).to_str().ok()?;
        let classes = parse_names(classes_json)?;
        let ids = parse_names(ids_json)?;

        let css = engine.generic_css(url, &classes, &ids).ok()??;
        CString::new(css).ok().map(CString::into_raw)
    }))
    .ok()
    .flatten()
    .unwrap_or(std::ptr::null_mut())
}

/// Parses a JSON array of strings supplied by a page.
///
/// # Safety
///
/// `json` must be a valid, readable, null-terminated C string.
unsafe fn parse_names(json: *const c_char) -> Option<Vec<String>> {
    let text = unsafe { CStr::from_ptr(json) }.to_str().ok()?;

    if text.len() > MAX_NAMES_JSON_BYTES {
        return None;
    }

    serde_json::from_str::<Vec<String>>(text).ok()
}

/// Returns the C ABI version supported by this library.
#[unsafe(no_mangle)]
pub extern "C" fn vibrium_adblock_abi_version() -> u32 {
    ABI_VERSION
}
