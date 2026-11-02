//! End-to-end tests of the C ABI the CEF layer calls.

use std::ffi::{CStr, CString, c_void};
use std::os::raw::c_char;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::{Arc, Barrier};

use vibrium_adblock::ffi::*;

fn c(s: &str) -> CString {
    CString::new(s).unwrap()
}

struct Engine(*mut c_void);
// The ABI contract is that a live handle may be used from several threads.
unsafe impl Send for Engine {}
unsafe impl Sync for Engine {}

fn create(rules: &str) -> Engine {
    let rules = c(rules);
    let handle = unsafe { vibrium_adblock_create_with_rules(rules.as_ptr()) };
    assert!(!handle.is_null());
    Engine(handle)
}

fn check(e: &Engine, url: &str, source: &str, kind: &str) -> i32 {
    let (u, s, k) = (c(url), c(source), c(kind));
    unsafe { vibrium_adblock_check(e.0, u.as_ptr(), s.as_ptr(), k.as_ptr()) }
}

fn temp_dir(tag: &str) -> PathBuf {
    static N: AtomicU32 = AtomicU32::new(0);
    let dir = std::env::temp_dir().join(format!(
        "vibrium-ffi-{tag}-{}-{}",
        std::process::id(),
        N.fetch_add(1, Ordering::Relaxed)
    ));
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

#[test]
fn abi_version_is_three() {
    assert_eq!(vibrium_adblock_abi_version(), 3);
}

#[test]
fn many_threads_check_one_shared_engine() {
    let engine = Arc::new(create("||ads.example.net^\n||tracker.test^$third-party"));
    let threads = 8;
    let barrier = Arc::new(Barrier::new(threads));

    let workers: Vec<_> = (0..threads)
        .map(|i| {
            let (engine, barrier) = (engine.clone(), barrier.clone());
            std::thread::spawn(move || {
                barrier.wait();
                for n in 0..500 {
                    let blocked = check(
                        &engine,
                        &format!("https://ads.example.net/{i}/{n}.js"),
                        "https://site.test/",
                        "script",
                    );
                    let allowed = check(
                        &engine,
                        &format!("https://cdn.example.net/{i}/{n}.js"),
                        "https://site.test/",
                        "script",
                    );
                    assert_eq!((blocked, allowed), (1, 0));
                }
            })
        })
        .collect();

    for w in workers {
        w.join().unwrap();
    }
    unsafe { vibrium_adblock_destroy(engine.0) };
}

#[test]
fn invalid_input_returns_error_without_crashing() {
    let e = create("||ads.example.net^");
    assert_eq!(check(&e, "not a url", "https://site.test/", "script"), -1);
    assert_eq!(check(&e, "", "", ""), -1);
    unsafe {
        let s = c("x");
        assert_eq!(
            vibrium_adblock_check(std::ptr::null_mut(), s.as_ptr(), s.as_ptr(), s.as_ptr()),
            -1
        );
        assert_eq!(
            vibrium_adblock_check(e.0, std::ptr::null(), s.as_ptr(), s.as_ptr()),
            -1
        );
        vibrium_adblock_destroy(e.0);
        vibrium_adblock_destroy(std::ptr::null_mut()); // null destroy is a no-op
    }
}

#[test]
fn create_from_directory_loads_valid_lists_and_skips_invalid_ones() {
    let dir = temp_dir("dir");
    std::fs::write(dir.join("good.txt"), "||ads.example.net^\n").unwrap();
    std::fs::write(dir.join("empty.txt"), "   \n").unwrap();
    std::fs::write(dir.join(".hidden.0.tmp"), "||ignored.test^\n").unwrap();
    std::fs::write(dir.join("notes.md"), "||ignored2.test^\n").unwrap();

    let path = c(dir.to_str().unwrap());
    let handle = unsafe { vibrium_adblock_create_from_directory(path.as_ptr()) };
    assert!(!handle.is_null(), "one valid list must be enough");
    let e = Engine(handle);

    assert_eq!(
        check(
            &e,
            "https://ads.example.net/a.js",
            "https://site.test/",
            "script"
        ),
        1
    );
    assert_eq!(
        check(
            &e,
            "https://ignored.test/a.js",
            "https://site.test/",
            "script"
        ),
        0
    );
    assert_eq!(
        check(
            &e,
            "https://ignored2.test/a.js",
            "https://site.test/",
            "script"
        ),
        0
    );

    unsafe { vibrium_adblock_destroy(e.0) };
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn create_from_directory_fails_safely() {
    unsafe {
        assert!(vibrium_adblock_create_from_directory(std::ptr::null()).is_null());

        let missing = c("/definitely/not/a/real/vibrium/dir");
        assert!(vibrium_adblock_create_from_directory(missing.as_ptr()).is_null());
    }

    let dir = temp_dir("empty");
    let path = c(dir.to_str().unwrap());
    assert!(unsafe { vibrium_adblock_create_from_directory(path.as_ptr()) }.is_null());
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn cosmetic_css_returns_one_rule_per_selector_and_frees_cleanly() {
    let e = create("example.com##.ad-banner\nexample.com###sponsor\nother.test##.elsewhere");
    let page = c("https://example.com/article");

    let css_ptr: *mut c_char = unsafe { vibrium_adblock_cosmetic_css(e.0, page.as_ptr()) };
    assert!(!css_ptr.is_null());
    let css = unsafe { CStr::from_ptr(css_ptr) }
        .to_str()
        .unwrap()
        .to_owned();
    unsafe { vibrium_adblock_string_free(css_ptr) };

    assert!(
        css.contains(".ad-banner{display:none!important}\n"),
        "{css}"
    );
    assert!(css.contains("#sponsor{display:none!important}\n"), "{css}");
    assert!(
        !css.contains(".elsewhere"),
        "selector for another site leaked: {css}"
    );

    // Nothing to hide on an unrelated site, and non-http(s) pages are rejected.
    let unrelated = c("https://unrelated.test/");
    assert!(unsafe { vibrium_adblock_cosmetic_css(e.0, unrelated.as_ptr()) }.is_null());
    let internal = c("chrome://settings");
    assert!(unsafe { vibrium_adblock_cosmetic_css(e.0, internal.as_ptr()) }.is_null());

    unsafe {
        assert!(vibrium_adblock_cosmetic_css(std::ptr::null_mut(), page.as_ptr()).is_null());
        vibrium_adblock_string_free(std::ptr::null_mut()); // no-op
        vibrium_adblock_destroy(e.0);
    }
}

#[test]
fn engines_can_be_created_and_destroyed_repeatedly() {
    for _ in 0..50 {
        let e = create("||ads.example.net^");
        assert_eq!(
            check(&e, "https://ads.example.net/x", "https://s.test/", "image"),
            1
        );
        unsafe { vibrium_adblock_destroy(e.0) };
    }
}

/// The list used by `vibrium --selftest` must keep producing exactly the
/// behaviour the end-to-end selftest asserts.
#[test]
fn selftest_fixture_list_behaves_as_the_selftest_expects() {
    let dir = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/selftest-lists");
    let path = c(dir.to_str().unwrap());
    let handle = unsafe { vibrium_adblock_create_from_directory(path.as_ptr()) };
    assert!(!handle.is_null(), "selftest list directory must load");
    let e = Engine(handle);

    let page = "http://localhost:8123/selftest.html";
    assert_eq!(
        check(
            &e,
            "http://ads.vibrium-selftest.test/blocked.js",
            page,
            "script"
        ),
        1
    );
    assert_eq!(
        check(
            &e,
            "http://ads.vibrium-selftest.test/redirected.js",
            page,
            "script"
        ),
        1
    );
    assert_eq!(
        check(
            &e,
            "http://allowed.vibrium-selftest.test/pic.png",
            page,
            "image"
        ),
        0
    );

    let page_c = c(page);
    let css = unsafe { vibrium_adblock_cosmetic_css(e.0, page_c.as_ptr()) };
    assert!(!css.is_null());
    let text = unsafe { CStr::from_ptr(css) }.to_str().unwrap().to_owned();
    unsafe { vibrium_adblock_string_free(css) };
    assert!(
        text.contains(".vibrium-ad{display:none!important}"),
        "{text}"
    );

    unsafe { vibrium_adblock_destroy(e.0) };
}
