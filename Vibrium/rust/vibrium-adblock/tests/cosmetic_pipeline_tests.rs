//! Cosmetic filtering beyond plain selector hiding: generic class/id
//! selectors, exceptions, `$generichide`, procedural filters and scriptlets.

use std::ffi::{CStr, CString, c_void};
use std::os::raw::c_char;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU32, Ordering};

use vibrium_adblock::engine::{VibriumAdblock, limits};
use vibrium_adblock::ffi::*;

const PAGE: &str = "https://example.com/article";
const OTHER_PAGE: &str = "https://other.example/";

fn names(list: &[&str]) -> Vec<String> {
    list.iter().map(|name| name.to_string()).collect()
}

fn temp_dir(tag: &str) -> PathBuf {
    static N: AtomicU32 = AtomicU32::new(0);
    let dir = std::env::temp_dir().join(format!(
        "vibrium-cosmetic-{tag}-{}-{}",
        std::process::id(),
        N.fetch_add(1, Ordering::Relaxed)
    ));
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

/// A scriptlet resource named `my-scriptlet` (what resources.json holds).
fn resources_json() -> String {
    // base64("window.__vibriumScriptlet = '{{1}}';")
    let source = "window.__vibriumScriptlet = '{{1}}';";
    let encoded = base64(source.as_bytes());
    format!(
        r#"[{{"name":"my-scriptlet.js","aliases":[],"kind":"template","content":"{encoded}"}}]"#
    )
}

fn base64(bytes: &[u8]) -> String {
    const TABLE: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::new();
    for chunk in bytes.chunks(3) {
        let b = [chunk[0], *chunk.get(1).unwrap_or(&0), *chunk.get(2).unwrap_or(&0)];
        let n = (u32::from(b[0]) << 16) | (u32::from(b[1]) << 8) | u32::from(b[2]);
        out.push(TABLE[(n >> 18) as usize & 63] as char);
        out.push(TABLE[(n >> 12) as usize & 63] as char);
        out.push(if chunk.len() > 1 { TABLE[(n >> 6) as usize & 63] as char } else { '=' });
        out.push(if chunk.len() > 2 { TABLE[n as usize & 63] as char } else { '=' });
    }
    out
}

// ---------------------------------------------------------------- generic

#[test]
fn generic_selectors_match_only_names_the_page_reports() {
    let engine = VibriumAdblock::from_filter_list("##.generic-ad\n###sponsor-box\n##.unused-rule");

    let css = engine
        .generic_css(PAGE, &names(&["generic-ad", "plain"]), &names(&["sponsor-box"]))
        .unwrap()
        .expect("reported names match generic rules");

    assert!(css.contains(".generic-ad{display:none!important}"));
    assert!(css.contains("#sponsor-box{display:none!important}"));
    assert!(!css.contains("unused-rule"), "unreported rules must not be sent");
    assert!(!css.contains("plain"));
}

#[test]
fn generic_selectors_return_none_when_nothing_matches() {
    let engine = VibriumAdblock::from_filter_list("##.generic-ad");

    let css = engine
        .generic_css(PAGE, &names(&["something-else"]), &[])
        .unwrap();

    assert_eq!(css, None);
}

#[test]
fn generichide_exception_disables_generic_selectors() {
    let engine = VibriumAdblock::from_filter_list("##.generic-ad\n@@||example.com^$generichide");

    assert_eq!(
        engine.generic_css(PAGE, &names(&["generic-ad"]), &[]).unwrap(),
        None,
        "$generichide must suppress generic selectors on that site"
    );
    assert!(
        engine
            .generic_css(OTHER_PAGE, &names(&["generic-ad"]), &[])
            .unwrap()
            .is_some(),
        "other sites are unaffected"
    );

    let payload = engine.cosmetic_payload(PAGE).unwrap();
    assert!(payload.generichide, "the page is told not to ask");
}

#[test]
fn generic_exceptions_apply_per_site() {
    let engine = VibriumAdblock::from_filter_list("##.generic-ad\nexample.com#@#.generic-ad");

    assert_eq!(
        engine.generic_css(PAGE, &names(&["generic-ad"]), &[]).unwrap(),
        None,
        "an exception rule must exempt the selector on its site"
    );
    assert!(
        engine
            .generic_css(OTHER_PAGE, &names(&["generic-ad"]), &[])
            .unwrap()
            .is_some()
    );
}

#[test]
fn page_supplied_names_cannot_inject_css() {
    let engine = VibriumAdblock::from_filter_list("##.generic-ad");

    let hostile = names(&[
        "x{}body{display:none}",
        "generic-ad{color:red}",
        "}</style><script>alert(1)</script>",
    ]);
    let css = engine.generic_css(PAGE, &hostile, &hostile).unwrap();

    assert_eq!(css, None, "only selectors from the filter list can appear in CSS");
}

#[test]
fn oversized_and_duplicate_names_are_bounded() {
    let engine = VibriumAdblock::from_filter_list("##.generic-ad");

    let long = "a".repeat(limits::MAX_NAME_BYTES + 1);
    let mut flood: Vec<String> = (0..limits::MAX_NAMES_PER_REQUEST * 2)
        .map(|n| format!("filler-{n}"))
        .collect();
    flood.push("generic-ad".into()); // past the cap, so ignored
    flood.insert(0, long);

    assert_eq!(engine.generic_css(PAGE, &flood, &[]).unwrap(), None);

    // Duplicates collapse, so a duplicate-heavy list still reaches the real name.
    let duplicates: Vec<String> = std::iter::repeat_n("noise".to_string(), 50_000)
        .chain(["generic-ad".to_string()])
        .collect();
    assert!(engine.generic_css(PAGE, &duplicates, &[]).unwrap().is_some());
}

#[test]
fn generic_css_rejects_non_http_pages() {
    let engine = VibriumAdblock::from_filter_list("##.generic-ad");
    assert!(engine.generic_css("file:///c:/x.html", &names(&["generic-ad"]), &[]).is_err());
}

// ------------------------------------------------------------- procedural

#[test]
fn plain_selectors_become_css_and_procedural_filters_do_not() {
    let engine = VibriumAdblock::from_filter_list(
        "example.com##.banner\nexample.com##.promo:has-text(Sponsored)",
    );

    let payload = engine.cosmetic_payload(PAGE).unwrap();

    assert!(payload.css.contains(".banner{display:none!important}"));
    assert!(!payload.css.contains("promo"), "procedural selectors are not valid CSS");
    assert_eq!(payload.procedural.len(), 1);
    assert!(payload.procedural[0].contains("has-text"));
    assert!(payload.procedural[0].contains("Sponsored"));
}

#[test]
fn procedural_filters_are_valid_json_with_operators_and_actions() {
    let engine = VibriumAdblock::from_filter_list(
        "example.com##.a:upward(2)\n\
         example.com##.b:style(color: red)\n\
         example.com##.c:remove()\n\
         example.com##.d:has-text(/ad(s)?/i):matches-css(display: block)",
    );

    let payload = engine.cosmetic_payload(PAGE).unwrap();
    assert!(payload.procedural.len() >= 3, "got {:?}", payload.procedural);

    for filter in &payload.procedural {
        let value: serde_json::Value = serde_json::from_str(filter).expect("each entry is JSON");
        assert!(value.get("selector").is_some(), "{filter}");
    }

    let joined = payload.procedural.join("\n");
    assert!(joined.contains("upward"));
    assert!(joined.contains("style"));
    assert!(joined.contains("remove") || joined.contains("Remove"));
}

#[test]
fn procedural_filters_apply_only_on_their_site() {
    let engine = VibriumAdblock::from_filter_list("example.com##.promo:has-text(Sponsored)");

    assert_eq!(engine.cosmetic_payload(PAGE).unwrap().procedural.len(), 1);
    assert!(engine.cosmetic_payload(OTHER_PAGE).unwrap().procedural.is_empty());
}

#[test]
fn payload_json_round_trips() {
    let engine = VibriumAdblock::from_filter_list(
        "example.com##.banner\nexample.com##.promo:has-text(\"quoted\" \\ back)",
    );

    let json = engine.cosmetic_payload(PAGE).unwrap().to_json();
    let value: serde_json::Value = serde_json::from_str(&json).unwrap();

    assert!(value["css"].as_str().unwrap().contains(".banner"));
    assert!(value["procedural"].is_array());
    assert_eq!(value["generichide"], false);
}

#[test]
fn page_without_matching_rules_is_a_noop_only_when_generichide() {
    let plain = VibriumAdblock::from_filter_list("||ads.example.net^");
    // Generic lookups still have to happen, so the page is not a no-op.
    assert!(!plain.cosmetic_payload(PAGE).unwrap().is_noop());

    let hidden = VibriumAdblock::from_filter_list("||ads.example.net^\n@@||example.com^$generichide");
    assert!(hidden.cosmetic_payload(PAGE).unwrap().is_noop());
}

// -------------------------------------------------------------- scriptlets

#[test]
fn scriptlets_are_empty_without_resources() {
    let engine = VibriumAdblock::from_filter_list("example.com##+js(my-scriptlet, hello)");
    assert_eq!(engine.injected_script(PAGE).unwrap(), "");
}

#[test]
fn scriptlets_use_installed_resources() {
    let mut engine = VibriumAdblock::from_filter_list("example.com##+js(my-scriptlet, hello)");

    assert_eq!(engine.use_resources_json(&resources_json()).unwrap(), 1);

    let script = engine.injected_script(PAGE).unwrap();
    assert!(script.contains("__vibriumScriptlet"), "got: {script}");
    assert!(script.contains("hello"), "template argument is substituted");

    assert_eq!(engine.injected_script(OTHER_PAGE).unwrap(), "", "site-specific");
}

#[test]
fn malformed_or_oversized_resources_are_rejected() {
    let mut engine = VibriumAdblock::from_filter_list("||ads.example.net^");

    assert!(engine.use_resources_json("not json").is_err());
    assert!(engine.use_resources_json("{\"name\":\"x\"}").is_err());
    assert!(
        engine
            .use_resources_json(&" ".repeat(limits::MAX_RESOURCES_BYTES + 1))
            .is_err()
    );
}

#[test]
fn from_directory_loads_resources_json_next_to_the_lists() {
    let dir = temp_dir("resources");
    std::fs::write(dir.join("a.txt"), "example.com##+js(my-scriptlet, from-dir)\n").unwrap();
    std::fs::write(dir.join("resources.json"), resources_json()).unwrap();

    let (engine, skipped) = VibriumAdblock::from_directory(&dir).unwrap();

    assert!(skipped.is_empty(), "{skipped:?}");
    assert!(engine.injected_script(PAGE).unwrap().contains("from-dir"));
}

#[test]
fn bad_resources_json_never_disables_blocking() {
    let dir = temp_dir("badresources");
    std::fs::write(dir.join("a.txt"), "||ads.example.net^\n").unwrap();
    std::fs::write(dir.join("resources.json"), "<html>oops</html>").unwrap();

    let (engine, skipped) = VibriumAdblock::from_directory(&dir).unwrap();

    assert_eq!(skipped, vec!["resources.json".to_string()]);
    assert!(
        engine
            .should_block("https://ads.example.net/x.js", PAGE, "script")
            .unwrap()
    );
}

// -------------------------------------------------------------------- FFI

struct Handle(*mut c_void);
impl Drop for Handle {
    fn drop(&mut self) {
        unsafe { vibrium_adblock_destroy(self.0) };
    }
}

fn c(text: &str) -> CString {
    CString::new(text).unwrap()
}

fn handle(rules: &str) -> Handle {
    let rules = c(rules);
    let raw = unsafe { vibrium_adblock_create_with_rules(rules.as_ptr()) };
    assert!(!raw.is_null());
    Handle(raw)
}

/// Takes ownership of a library string, freeing it like the C++ side does.
fn take(ptr: *mut c_char) -> Option<String> {
    if ptr.is_null() {
        return None;
    }
    let text = unsafe { CStr::from_ptr(ptr) }.to_str().unwrap().to_owned();
    unsafe { vibrium_adblock_string_free(ptr) };
    Some(text)
}

fn ffi_generic(h: &Handle, url: &str, classes: &str, ids: &str) -> Option<String> {
    let (u, cl, id) = (c(url), c(classes), c(ids));
    take(unsafe { vibrium_adblock_generic_css(h.0, u.as_ptr(), cl.as_ptr(), id.as_ptr()) })
}

#[test]
fn ffi_generic_css_returns_matching_rules() {
    let h = handle("##.generic-ad\n###sponsor-box");

    let css = ffi_generic(&h, PAGE, r#"["generic-ad","x"]"#, r#"["sponsor-box"]"#).unwrap();

    assert!(css.contains(".generic-ad"));
    assert!(css.contains("#sponsor-box"));
}

#[test]
fn ffi_generic_css_treats_hostile_input_as_no_result() {
    let h = handle("##.generic-ad");

    for bad in ["", "not json", "{\"a\":1}", "[1,2,3]", "[\"unterminated", "null"] {
        assert_eq!(ffi_generic(&h, PAGE, bad, "[]"), None, "classes={bad:?}");
        assert_eq!(ffi_generic(&h, PAGE, "[]", bad), None, "ids={bad:?}");
    }

    let huge = format!("[\"{}\"]", "a".repeat(300 * 1024));
    assert_eq!(ffi_generic(&h, PAGE, &huge, "[]"), None, "oversized request");

    assert_eq!(ffi_generic(&h, "file:///x", r#"["generic-ad"]"#, "[]"), None);
    assert_eq!(ffi_generic(&h, "garbage", r#"["generic-ad"]"#, "[]"), None);
}

#[test]
fn ffi_functions_tolerate_null_arguments() {
    let h = handle("##.generic-ad");
    let url = c(PAGE);
    let json = c("[]");

    unsafe {
        assert!(vibrium_adblock_generic_css(std::ptr::null_mut(), url.as_ptr(), json.as_ptr(), json.as_ptr()).is_null());
        assert!(vibrium_adblock_generic_css(h.0, std::ptr::null(), json.as_ptr(), json.as_ptr()).is_null());
        assert!(vibrium_adblock_generic_css(h.0, url.as_ptr(), std::ptr::null(), json.as_ptr()).is_null());
        assert!(vibrium_adblock_generic_css(h.0, url.as_ptr(), json.as_ptr(), std::ptr::null()).is_null());
        assert!(vibrium_adblock_cosmetic_payload(std::ptr::null_mut(), url.as_ptr()).is_null());
        assert!(vibrium_adblock_cosmetic_payload(h.0, std::ptr::null()).is_null());
        assert!(vibrium_adblock_cosmetic_script(std::ptr::null_mut(), url.as_ptr()).is_null());
        assert!(vibrium_adblock_cosmetic_script(h.0, std::ptr::null()).is_null());
    }
}

#[test]
fn ffi_payload_is_json_and_noop_pages_return_null() {
    let h = handle("example.com##.banner\nexample.com##.promo:has-text(Ad)");

    let payload = take(unsafe { vibrium_adblock_cosmetic_payload(h.0, c(PAGE).as_ptr()) }).unwrap();
    let value: serde_json::Value = serde_json::from_str(&payload).unwrap();
    assert!(value["css"].as_str().unwrap().contains(".banner"));
    assert_eq!(value["procedural"].as_array().unwrap().len(), 1);

    let noop = handle("||ads.example.net^\n@@||example.com^$generichide");
    assert_eq!(
        take(unsafe { vibrium_adblock_cosmetic_payload(noop.0, c(PAGE).as_ptr()) }),
        None
    );
    assert_eq!(
        take(unsafe { vibrium_adblock_cosmetic_payload(h.0, c("file:///x").as_ptr()) }),
        None
    );
}

#[test]
fn ffi_script_is_null_without_resources() {
    let h = handle("example.com##+js(my-scriptlet, hello)");
    assert_eq!(
        take(unsafe { vibrium_adblock_cosmetic_script(h.0, c(PAGE).as_ptr()) }),
        None
    );
}

#[test]
fn ffi_script_comes_from_directory_resources() {
    let dir = temp_dir("ffi-script");
    std::fs::write(dir.join("a.txt"), "example.com##+js(my-scriptlet, via-ffi)\n").unwrap();
    std::fs::write(dir.join("resources.json"), resources_json()).unwrap();

    let dir_c = c(dir.to_str().unwrap());
    let raw = unsafe { vibrium_adblock_create_from_directory(dir_c.as_ptr()) };
    assert!(!raw.is_null());
    let h = Handle(raw);

    let script = take(unsafe { vibrium_adblock_cosmetic_script(h.0, c(PAGE).as_ptr()) }).unwrap();
    assert!(script.contains("via-ffi"), "{script}");
}

#[test]
fn many_threads_query_generic_css_concurrently() {
    use std::sync::Arc;

    struct Shared(Handle);
    unsafe impl Send for Shared {}
    unsafe impl Sync for Shared {}

    let shared = Arc::new(Shared(handle("##.generic-ad\nexample.com##.banner")));

    let workers: Vec<_> = (0..8)
        .map(|_| {
            let shared = shared.clone();
            std::thread::spawn(move || {
                for _ in 0..300 {
                    let css = ffi_generic(&shared.0, PAGE, r#"["generic-ad"]"#, "[]");
                    assert!(css.unwrap().contains(".generic-ad"));
                    let payload =
                        take(unsafe { vibrium_adblock_cosmetic_payload(shared.0.0, c(PAGE).as_ptr()) });
                    assert!(payload.unwrap().contains("banner"));
                }
            })
        })
        .collect();

    for worker in workers {
        worker.join().unwrap();
    }
}
