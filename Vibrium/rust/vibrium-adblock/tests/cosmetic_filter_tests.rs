use std::collections::HashSet;

use vibrium_adblock::engine::VibriumAdblock;

#[test]
fn applies_domain_specific_cosmetic_rules() {
    let engine =
        VibriumAdblock::from_filter_list("example.com##.sponsored\nexample.com###advertisement");

    let resources = engine
        .cosmetic_resources("https://example.com/")
        .expect("valid URL should return cosmetic resources");

    assert!(
        resources.hide_selectors.contains(".sponsored"),
        "expected the domain-specific class selector"
    );

    assert!(
        resources.hide_selectors.contains("#advertisement"),
        "expected the domain-specific ID selector"
    );
}

#[test]
fn does_not_apply_domain_specific_rules_to_other_domains() {
    let engine = VibriumAdblock::from_filter_list("example.com##.sponsored");

    let resources = engine
        .cosmetic_resources("https://other.example/")
        .expect("valid URL should return cosmetic resources");

    assert!(
        !resources.hide_selectors.contains(".sponsored"),
        "domain-specific rule must not leak to unrelated domains"
    );
}

#[test]
fn rejects_non_http_urls_for_cosmetic_filtering() {
    let engine = VibriumAdblock::from_filter_list("##.advertisement");

    assert!(
        engine
            .cosmetic_resources("file:///C:/example.html")
            .is_err()
    );

    assert!(engine.cosmetic_resources("not a URL").is_err());
}

#[test]
fn finds_generic_class_selectors() {
    let engine = VibriumAdblock::from_filter_list("##.advertisement");

    let resources = engine
        .cosmetic_resources("https://example.com/")
        .expect("valid URL should return cosmetic resources");

    if !resources.generichide {
        let selectors = engine.hidden_class_id_selectors(
            &["advertisement".to_string()],
            &[],
            &resources.exceptions,
        );

        assert!(
            selectors.contains(&".advertisement".to_string()),
            "expected the generic class selector"
        );
    }
}

#[test]
fn generic_exceptions_are_passed_to_selector_matching() {
    let engine = VibriumAdblock::from_filter_list("##.advertisement");

    let mut exceptions = HashSet::new();
    exceptions.insert(".advertisement".to_string());

    let selectors =
        engine.hidden_class_id_selectors(&["advertisement".to_string()], &[], &exceptions);

    assert!(
        !selectors.contains(&".advertisement".to_string()),
        "an excepted selector must not be returned"
    );
}
