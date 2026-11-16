//! Tests for the scriptlet-only engine used inside renderer processes.
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU32, Ordering};

use vibrium_adblock::ScriptletEngine;

static COUNTER: AtomicU32 = AtomicU32::new(0);

/// A scratch directory that is removed when dropped.
struct TempDir(PathBuf);

impl TempDir {
    fn new() -> Self {
        let path = std::env::temp_dir().join(format!(
            "vibrium-scriptlets-{}-{}",
            std::process::id(),
            COUNTER.fetch_add(1, Ordering::SeqCst)
        ));
        std::fs::create_dir_all(&path).unwrap();
        Self(path)
    }
    fn path(&self) -> &Path { &self.0 }
    fn write(&self, name: &str, contents: &str) {
        std::fs::write(self.0.join(name), contents).unwrap();
    }
}

impl Drop for TempDir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

/// `plain.js` (needs `safe.fn`) and `trusty.js` (requires the trusted permission).
const RESOURCES: &str = r#"[
  {"name":"safe.fn","aliases":[],"kind":{"mime":"fn/javascript"},"content":"ZnVuY3Rpb24gc2FmZUhlbHBlcigpe3JldHVybiAxO30="},
  {"name":"plain.js","aliases":["pl.js"],"kind":{"mime":"application/javascript"},
    "content":"ZnVuY3Rpb24gcGxhaW4oYSl7IHNlbGYuX19wbGFpbiA9IGE7IH0=","dependencies":["safe.fn"]},
  {"name":"trusty.js","aliases":[],"kind":{"mime":"application/javascript"},
    "content":"ZnVuY3Rpb24gdHJ1c3R5KGEpeyBzZWxmLl9fdHJ1c3R5ID0gYTsgfQ==","permission":1}
]"#;

fn dir_with(lists: &[(&str, &str)]) -> TempDir {
    let dir = TempDir::new();
    dir.write("resources.json", RESOURCES);
    for (name, text) in lists {
        dir.write(name, text);
    }
    dir
}

fn script(engine: &ScriptletEngine, url: &str) -> String {
    engine.injected_script(url).unwrap()
}

#[test]
fn scriptlet_is_injected_for_matching_host_only() {
    let dir = dir_with(&[("ubo-a.txt", "example.com##+js(plain, hello)\n")]);
    let engine = ScriptletEngine::from_directory(dir.path()).unwrap();

    let matched = script(&engine, "https://example.com/page");
    assert!(matched.contains("plain(\"hello\")"), "{matched}");
    // The dependency must be emitted too, or the scriptlet would throw.
    assert!(matched.contains("function safeHelper"), "{matched}");

    assert!(script(&engine, "https://other.org/").is_empty());
}

#[test]
fn aliases_resolve() {
    let dir = dir_with(&[("ubo-a.txt", "example.com##+js(pl, hi)\n")]);
    let engine = ScriptletEngine::from_directory(dir.path()).unwrap();
    assert!(script(&engine, "https://example.com/").contains("plain(\"hi\")"));
}

#[test]
fn trusted_scriptlets_need_a_trusted_list() {
    let rule = "example.com##+js(trusty, x)\n";

    let trusted = dir_with(&[("ubo-a.txt", rule)]);
    let engine = ScriptletEngine::from_directory(trusted.path()).unwrap();
    assert!(script(&engine, "https://example.com/").contains("trusty(\"x\")"));

    // Same rule from a list that is not trusted: it must not run.
    let untrusted = dir_with(&[("easylist.txt", rule)]);
    let engine = ScriptletEngine::from_directory(untrusted.path()).unwrap();
    assert!(script(&engine, "https://example.com/").is_empty());
}

#[test]
fn untrusted_lists_can_still_use_ordinary_scriptlets() {
    let dir = dir_with(&[("hagezi.txt", "example.com##+js(plain, ok)\n")]);
    let engine = ScriptletEngine::from_directory(dir.path()).unwrap();
    assert!(script(&engine, "https://example.com/").contains("plain(\"ok\")"));
}

#[test]
fn exception_rule_cancels_a_scriptlet() {
    let dir = dir_with(&[(
        "ubo-a.txt",
        "example.com,other.org##+js(plain, hello)\nexample.com#@#+js(plain, hello)\n",
    )]);
    let engine = ScriptletEngine::from_directory(dir.path()).unwrap();
    assert!(script(&engine, "https://other.org/").contains("plain(\"hello\")"));
    assert!(script(&engine, "https://example.com/").is_empty());
}

#[test]
fn generic_scriptlet_rules_are_not_supported() {
    // adblock-rust refuses `##+js(...)` without a hostname on purpose: a
    // scriptlet that runs on every site is too risky.
    let dir = dir_with(&[("ubo-a.txt", "##+js(plain, hello)\nexample.com##+js(plain, a)\n")]);
    let engine = ScriptletEngine::from_directory(dir.path()).unwrap();
    assert!(script(&engine, "https://other.org/").is_empty());
}

#[test]
fn only_scriptlet_rules_are_loaded() {
    let dir = dir_with(&[(
        "ubo-a.txt",
        "! comment\n||ads.example^\nexample.com##.banner\nexample.com##+js(plain, a)\n",
    )]);
    let engine = ScriptletEngine::from_directory(dir.path()).unwrap();
    assert_eq!(engine.rule_count(), 1);
}

#[test]
fn fails_without_resources() {
    let dir = TempDir::new();
    dir.write("ubo-a.txt", "example.com##+js(plain, a)\n");
    assert!(ScriptletEngine::from_directory(dir.path()).is_err());
}

#[test]
fn fails_without_scriptlet_rules() {
    let dir = dir_with(&[("ubo-a.txt", "||ads.example^\n")]);
    assert!(ScriptletEngine::from_directory(dir.path()).is_err());
}

#[test]
fn malformed_resources_fail_cleanly() {
    let dir = TempDir::new();
    dir.write("resources.json", "{ not json");
    dir.write("ubo-a.txt", "example.com##+js(plain, a)\n");
    assert!(ScriptletEngine::from_directory(dir.path()).is_err());
}

#[test]
fn only_http_urls_are_accepted() {
    let dir = dir_with(&[("ubo-a.txt", "example.com##+js(plain, a)\n")]);
    let engine = ScriptletEngine::from_directory(dir.path()).unwrap();
    assert!(engine.injected_script("file:///etc/hosts").is_err());
    assert!(engine.injected_script("about:blank").is_err());
    assert!(engine.injected_script("not a url").is_err());
}

#[test]
fn temp_files_and_non_txt_files_are_ignored() {
    let dir = dir_with(&[
        ("ubo-a.txt", "example.com##+js(plain, a)\n"),
        (".ubo-b.txt", "example.org##+js(plain, b)\n"),
        ("ubo-a.etag", "example.net##+js(plain, c)\n"),
    ]);
    let engine = ScriptletEngine::from_directory(dir.path()).unwrap();
    assert!(script(&engine, "https://example.org/").is_empty());
    assert!(script(&engine, "https://example.net/").is_empty());
    assert!(!script(&engine, "https://example.com/").is_empty());
}
