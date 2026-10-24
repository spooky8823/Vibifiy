use std::ffi::{CStr, c_void};
use std::os::raw::c_char;
use std::panic::{AssertUnwindSafe, catch_unwind};

use crate::engine::VibriumAdblock;
use crate::filter_validation::validate_and_clean_filter_list;

const ABI_VERSION: u32 = 1;
const ERROR: i32 = -1;

/// Creates an engine with no custom filter rules.
///
/// Use `vibrium_adblock_create_with_rules` to create an engine with
/// validated filter rules.
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

/// Returns the C ABI version supported by this library.
#[unsafe(no_mangle)]
pub extern "C" fn vibrium_adblock_abi_version() -> u32 {
    ABI_VERSION
}
