use std::ffi::c_void;
use std::os::raw::c_char;

use crate::engine::VibriumAdblock;

#[unsafe(no_mangle)]
pub extern "C" fn vibrium_adblock_create() -> *mut c_void {
    let engine = VibriumAdblock::new(&[]);
    Box::into_raw(Box::new(engine)).cast()
}

#[unsafe(no_mangle)]
/// Destroys an adblock engine created by `vibrium_adblock_create`.
///
/// # Safety
/// `handle` must be null or a valid, live handle returned by
/// `vibrium_adblock_create` that has not already been destroyed.
pub unsafe extern "C" fn vibrium_adblock_destroy(
    handle: *mut c_void,
) {
    if !handle.is_null() {
        unsafe {
            drop(Box::from_raw(handle.cast::<VibriumAdblock>()));
        }
    }
}

#[unsafe(no_mangle)]
/// Checks whether a network request should be blocked.
///
/// # Safety
/// `handle` must point to a valid, live `VibriumAdblock` instance.
/// Each non-null string pointer must point to a valid, null-terminated
/// C string readable for the duration of this call.
pub unsafe extern "C" fn vibrium_adblock_check(
    handle: *mut c_void,
    url: *const c_char,
    source_url: *const c_char,
    request_type: *const c_char,
) -> i32 {
    if handle.is_null()
        || url.is_null()
        || source_url.is_null()
        || request_type.is_null()
    {
        return -1;
    }

    unsafe {
        let engine = &*handle.cast::<VibriumAdblock>();

        let url = match std::ffi::CStr::from_ptr(url).to_str() {
            Ok(value) => value,
            Err(_) => return -1,
        };

        let source_url =
            match std::ffi::CStr::from_ptr(source_url).to_str() {
                Ok(value) => value,
                Err(_) => return -1,
            };

        let request_type =
            match std::ffi::CStr::from_ptr(request_type).to_str() {
                Ok(value) => value,
                Err(_) => return -1,
            };

        match engine.should_block(url, source_url, request_type) {
            Ok(true) => 1,
            Ok(false) => 0,
            Err(_) => -1,
        }
    }
}