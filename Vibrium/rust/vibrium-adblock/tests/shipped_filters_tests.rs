//! The filter lists that ship with Vibrium (`Vibrium/filters`) must exist,
//! parse without skipped files, and really block. This is what turns "the
//! build succeeded" into "ad blocking is active in the package".

use std::collections::BTreeSet;
use std::path::PathBuf;

use vibrium_adblock::engine::VibriumAdblock;

fn vibrium_dir() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn shipped() -> (VibriumAdblock, Vec<String>) {
    let dir = vibrium_dir().join("filters");
    assert!(
        dir.is_dir(),
        "{} is missing: packages would ship with ad blocking silently disabled",
        dir.display()
    );
    VibriumAdblock::from_directory(&dir).expect("shipped filters must load")
}

fn rules(path: PathBuf) -> BTreeSet<String> {
    std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("{}: {e}", path.display()))
        .lines()
        .map(str::trim)
        .filter(|l| !l.is_empty() && !l.starts_with('!'))
        .map(str::to_owned)
        .collect()
}

#[test]
fn shipped_lists_load_without_skipped_files() {
    let (_, skipped) = shipped();
    assert!(skipped.is_empty(), "shipped list(s) rejected: {skipped:?}");
}

#[test]
fn every_rule_in_the_baseline_is_accepted() {
    // Per-rule check: a typo in one rule would otherwise be dropped silently.
    let dir = vibrium_dir().join("filters");
    let text = std::fs::read_to_string(dir.join("vibrium-baseline.txt")).unwrap();
    let report = vibrium_adblock::filter_validation::validate_filter_list(&text)
        .expect("baseline must be a valid filter list");
    assert_eq!(report.rejected_rules, 0, "baseline has rules the parser rejects");
    assert!(report.parsed_rules > 15, "baseline looks truncated: {report:?}");
}

#[test]
fn shipped_lists_block_known_ad_networks_and_allow_normal_sites() {
    let (engine, _) = shipped();
    let page = "https://news.example.org/story";

    for ad in [
        "https://securepubads.g.doubleclick.net/gampad/ads",
        "https://pagead2.googlesyndication.com/pagead/js/adsbygoogle.js",
        "https://ib.adnxs.com/ut/v3",
        "https://c.amazon-adsystem.com/aax2/apstag.js",
    ] {
        assert!(engine.should_block(ad, page, "script").unwrap(), "{ad} should be blocked");
    }

    for fine in [
        "https://news.example.org/app.js",
        "https://cdn.jsdelivr.net/npm/react@18.3.1/umd/react.production.min.js",
        "https://www.google.com/search?q=rust",
        "https://github.com/rust-lang/rust",
    ] {
        assert!(!engine.should_block(fine, page, "script").unwrap(), "{fine} must not be blocked");
    }
}

#[test]
fn shipped_lists_include_generic_cosmetic_rules() {
    let (engine, _) = shipped();
    let css = engine
        .generic_css("https://news.example.org/", &["adsbygoogle".to_string()], &[])
        .unwrap();
    assert!(css.is_some_and(|c| c.contains(".adsbygoogle")));
}

#[test]
fn baseline_carries_the_selftest_sentinel_rules() {
    // `vibrium --selftest` on the packaged build has no --filter-dir, so the
    // shipped baseline must contain every rule the fixture list has.
    let fixture = rules(vibrium_dir().join("tests/fixtures/selftest-lists/selftest.txt"));
    let baseline = rules(vibrium_dir().join("filters/vibrium-baseline.txt"));
    assert!(!fixture.is_empty());

    let missing: Vec<_> = fixture.difference(&baseline).collect();
    assert!(missing.is_empty(), "baseline lacks selftest rules: {missing:?}");
}
