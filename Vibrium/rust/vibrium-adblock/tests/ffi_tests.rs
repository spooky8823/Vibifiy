use std::ffi::{CString, c_void};

use vibrium_adblock::ffi::{
    vibrium_adblock_abi_version, vibrium_adblock_check, vibrium_adblock_create_with_rules,
    vibrium_adblock_destroy,
};

#[test]
fn abi_version_is_supported() {
    assert_eq!(vibrium_adblock_abi_version(), 1);
}

#[test]
fn check_rejects_null_handle() {
    let url = CString::new("https://ads.example/ad.js").unwrap();
    let source = CString::new("https://example.com").unwrap();
    let request_type = CString::new("script").unwrap();

    let result = unsafe {
        vibrium_adblock_check(
            std::ptr::null_mut(),
            url.as_ptr(),
            source.as_ptr(),
            request_type.as_ptr(),
        )
    };

    assert_eq!(result, -1);
}

#[test]
fn check_rejects_null_strings() {
    let rules = CString::new("||ads.example^").unwrap();

    let handle = unsafe { vibrium_adblock_create_with_rules(rules.as_ptr()) };
    assert!(!handle.is_null());

    let source = CString::new("https://example.com").unwrap();
    let request_type = CString::new("script").unwrap();

    let result = unsafe {
        vibrium_adblock_check(
            handle,
            std::ptr::null(),
            source.as_ptr(),
            request_type.as_ptr(),
        )
    };

    unsafe { vibrium_adblock_destroy(handle) };

    assert_eq!(result, -1);
}

#[test]
fn create_with_rules_rejects_null_input() {
    let handle = unsafe { vibrium_adblock_create_with_rules(std::ptr::null()) };

    assert!(handle.is_null());
}

#[test]
fn destroy_accepts_null_handle() {
    unsafe {
        vibrium_adblock_destroy(std::ptr::null_mut::<c_void>());
    }
}

#[test]
fn valid_rules_create_a_working_engine() {
    let rules = CString::new("||ads.example^").unwrap();

    let handle = unsafe { vibrium_adblock_create_with_rules(rules.as_ptr()) };

    assert!(!handle.is_null());

    unsafe {
        vibrium_adblock_destroy(handle);
    }
}

#[test]
fn matching_request_is_blocked_through_ffi() {
    let rules = CString::new("||ads.example^").unwrap();
    let url = CString::new("https://ads.example/ad.js").unwrap();
    let source = CString::new("https://example.com").unwrap();
    let request_type = CString::new("script").unwrap();

    let handle = unsafe { vibrium_adblock_create_with_rules(rules.as_ptr()) };
    assert!(!handle.is_null());

    let result = unsafe {
        vibrium_adblock_check(handle, url.as_ptr(), source.as_ptr(), request_type.as_ptr())
    };

    unsafe {
        vibrium_adblock_destroy(handle);
    }

    assert_eq!(result, 1);
}

#[test]
fn unmatched_request_is_allowed_through_ffi() {
    let rules = CString::new("||ads.example^").unwrap();
    let url = CString::new("https://static.example.org/app.js").unwrap();
    let source = CString::new("https://example.com").unwrap();
    let request_type = CString::new("script").unwrap();

    let handle = unsafe { vibrium_adblock_create_with_rules(rules.as_ptr()) };
    assert!(!handle.is_null());

    let result = unsafe {
        vibrium_adblock_check(handle, url.as_ptr(), source.as_ptr(), request_type.as_ptr())
    };

    unsafe {
        vibrium_adblock_destroy(handle);
    }

    assert_eq!(result, 0);
}

#[test]
fn unusable_rules_are_rejected() {
    let rules = CString::new("! Only a comment\n[Adblock Plus 2.0]\n\n").unwrap();

    let handle = unsafe { vibrium_adblock_create_with_rules(rules.as_ptr()) };

    assert!(handle.is_null());
}
