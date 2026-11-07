//! Replays the checks made by https://adblock.turtlecute.org against the real
//! Vibrium engine, so filter-list changes can be scored without a browser.
//!
//! usage: cargo run --release --example turtlecute_bench -- <filters-dir> <adblock_data.json>
//!
//! The site's own method (see its src/js/index.js):
//!   * each host:  fetch("https://<host>/fakepage.html", HEAD, no-cors) from the
//!                 first-party page; a network error counts as "blocked"
//!   * scripts:    <script src="./js/widget/ads.js"> and ./js/pagead.js (first party)
//!   * cosmetic:   #cts_test (static) and #ad_ctd (dynamic, added after load) must
//!                 end up with zero height
use std::collections::BTreeMap;
use vibrium_adblock::VibriumAdblock;

const PAGE: &str = "https://adblock.turtlecute.org/";

fn main() {
    let mut args = std::env::args().skip(1);
    let dir = args.next().expect("filters dir");
    let json_path = args.next().expect("adblock_data.json");

    let (engine, skipped) =
        VibriumAdblock::from_directory(std::path::Path::new(&dir)).expect("engine");
    if !skipped.is_empty() {
        eprintln!("skipped lists: {skipped:?}");
    }

    let data: BTreeMap<String, BTreeMap<String, Vec<String>>> =
        serde_json::from_str(&std::fs::read_to_string(json_path).unwrap()).unwrap();

    let (mut blocked, mut total) = (0usize, 0usize);
    let mut missed: BTreeMap<String, Vec<String>> = BTreeMap::new();
    for (cat, groups) in &data {
        for hosts in groups.values() {
            for h in hosts {
                total += 1;
                let url = format!("https://{h}/fakepage.html");
                if engine.should_block(&url, PAGE, "xhr").unwrap_or(false) {
                    blocked += 1;
                } else {
                    missed.entry(cat.clone()).or_default().push(h.clone());
                }
            }
        }
    }
    let host_blocked = blocked;

    // Ad-script checks (first-party URLs, type=script).
    let mut script_ok = 0;
    for p in ["js/widget/ads.js", "js/pagead.js"] {
        let b = engine
            .should_block(&format!("{PAGE}{p}"), PAGE, "script")
            .unwrap_or(false);
        println!("script {p:<18} -> {}", if b { "blocked" } else { "NOT blocked" });
        script_ok += b as usize;
    }

    // Cosmetic checks. Static element: the page's classes/ids, reported to
    // the generic-selector lookup. Dynamic element: same lookup, other classes.
    let css_static = engine.cosmetic_css(PAGE).ok().flatten().unwrap_or_default();
    let v = |xs: &[&str]| -> Vec<String> { xs.iter().map(|s| s.to_string()).collect() };
    let classes_static = v(&["textads", "adsbox", "banner_ads", "adbox", "ADBox", "AdBox", "adbox-wrapper", "adSocial"]);
    let classes_dyn = v(&["textads", "banner-ads", "banner_ads", "ad-unit", "afs_ads", "ad-zone", "ad-space", "adsbox"]);
    let g1 = engine.generic_css(PAGE, &classes_static, &v(&["cts_test"])).ok().flatten();
    let g2 = engine.generic_css(PAGE, &classes_dyn, &v(&["ad_ctd"])).ok().flatten();
    let cos_static = g1.as_deref().is_some_and(|c| !c.is_empty())
        || css_static.contains("textads");
    let cos_dyn = g2.as_deref().is_some_and(|c| !c.is_empty());
    println!("cosmetic static  -> {}", if cos_static { "hidden" } else { "NOT hidden" });
    println!("cosmetic dynamic -> {}", if cos_dyn { "hidden" } else { "NOT hidden" });

    let all_total = total + 2 + 2;
    let all_blocked = host_blocked + script_ok + cos_static as usize + cos_dyn as usize;
    println!("\nhosts: {host_blocked}/{total} blocked");
    for (cat, v) in &missed {
        println!("  missed [{cat}] ({}): {}", v.len(), v.join(", "));
    }
    println!(
        "\nESTIMATED SCORE: {all_blocked}/{all_total} = {:.0}%",
        100.0 * all_blocked as f64 / all_total as f64
    );
}
