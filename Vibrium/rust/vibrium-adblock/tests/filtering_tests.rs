//! Behaviour of the existing adblock-rust engine as wrapped by Vibrium.
//! These pin the semantics the CEF layer relies on.

use vibrium_adblock::VibriumAdblock;

fn engine(rules: &[&str]) -> VibriumAdblock {
    VibriumAdblock::from_filter_list(&rules.join("\n"))
}

#[test]
fn blocks_matching_url_and_allows_others() {
    let e = engine(&["||ads.example.net^"]);

    assert!(
        e.should_block(
            "https://ads.example.net/banner.js",
            "https://site.test/",
            "script"
        )
        .unwrap()
    );
    assert!(
        !e.should_block(
            "https://cdn.example.net/app.js",
            "https://site.test/",
            "script"
        )
        .unwrap()
    );
}

#[test]
fn third_party_option_respects_source_origin() {
    let e = engine(&["||tracker.test^$third-party"]);

    assert!(
        e.should_block("https://tracker.test/p.gif", "https://news.test/", "image")
            .unwrap()
    );
    // Same registrable domain as the page: first-party, must be allowed.
    assert!(
        !e.should_block(
            "https://tracker.test/p.gif",
            "https://www.tracker.test/",
            "image"
        )
        .unwrap()
    );
}

#[test]
fn unknown_source_is_treated_as_third_party() {
    // Upstream behaviour the CEF layer must account for: an empty or
    // unparseable source makes every request third-party.
    let e = engine(&["||tracker.test^$third-party"]);
    assert!(
        e.should_block("https://tracker.test/p.gif", "", "image")
            .unwrap()
    );
}

#[test]
fn resource_type_options_apply() {
    let e = engine(&["||cdn.test^$script"]);

    assert!(
        e.should_block("https://cdn.test/a.js", "https://site.test/", "script")
            .unwrap()
    );
    assert!(
        !e.should_block("https://cdn.test/a.png", "https://site.test/", "image")
            .unwrap()
    );
}

#[test]
fn exception_rules_override_blocking_rules() {
    let e = engine(&["||ads.test^", "@@||ads.test/allowed.js^"]);

    assert!(
        e.should_block("https://ads.test/other.js", "https://site.test/", "script")
            .unwrap()
    );
    assert!(
        !e.should_block(
            "https://ads.test/allowed.js",
            "https://site.test/",
            "script"
        )
        .unwrap()
    );
}

#[test]
fn unparseable_request_url_is_an_error_not_a_block() {
    let e = engine(&["||ads.test^"]);
    assert!(
        e.should_block("not a url", "https://site.test/", "script")
            .is_err()
    );
    assert!(e.should_block("", "https://site.test/", "script").is_err());
}

#[test]
fn engine_is_sync_so_it_can_be_shared_across_cef_threads() {
    fn assert_send_sync<T: Send + Sync>() {}
    // Fails to compile if adblock's `single-thread` feature is re-enabled.
    assert_send_sync::<VibriumAdblock>();
}
