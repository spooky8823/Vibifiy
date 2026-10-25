use vibrium_adblock::engine::VibriumAdblock;

#[test]
fn applies_domain_specific_cosmetic_rules() {
    let engine =
        VibriumAdblock::from_filter_list("example.com##.sponsored\nexample.com###advertisement");

    let resources = engine
        .cosmetic_resources("https://example.com/")
        .expect("valid URL should return cosmetic resources");

    assert!(resources.hide_selectors.contains(".sponsored"));
    assert!(resources.hide_selectors.contains("#advertisement"));
}

#[test]
fn does_not_apply_domain_specific_rules_to_other_domains() {
    let engine = VibriumAdblock::from_filter_list("example.com##.sponsored");

    let resources = engine
        .cosmetic_resources("https://other.example/")
        .expect("valid URL should return cosmetic resources");

    assert!(!resources.hide_selectors.contains(".sponsored"));
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
        let classes = vec!["advertisement".to_string()];
        let ids: Vec<String> = Vec::new();

        let selectors = engine.hidden_class_id_selectors(&classes, &ids, &resources);

        assert!(selectors.contains(&".advertisement".to_string()));
    }
}

#[test]
fn generic_exceptions_are_passed_to_selector_matching() {
    let engine = VibriumAdblock::from_filter_list("##.advertisement\nexample.com#@#.advertisement");

    let resources = engine
        .cosmetic_resources("https://example.com/")
        .expect("valid URL should return cosmetic resources");

    assert!(
        resources.exceptions.contains(".advertisement"),
        "the exception should be present in cosmetic resources"
    );

    let classes = vec!["advertisement".to_string()];

    let selectors = engine.hidden_class_id_selectors(&classes, &[], &resources);

    assert!(
        !selectors.contains(&".advertisement".to_string()),
        "an excepted selector must not be returned"
    );
}
