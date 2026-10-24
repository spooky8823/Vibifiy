use vibrium_adblock::filter_lists::{
    FilterList,
    FilterListCollection,
};

#[test]
fn adds_filter_list() {
    let mut lists = FilterListCollection::new();

    let list = FilterList::from_text(
        "Test Ads",
        "https://example.org/filters.txt",
        "||ads.example.com^\n",
    );

    assert!(lists.add(list).is_ok());
    assert!(lists.get("Test Ads").is_some());
}

#[test]
fn rejects_duplicate_list_names() {
    let mut lists = FilterListCollection::new();

    let first = FilterList::from_text(
        "Test Ads",
        "https://example.org/first.txt",
        "||ads.example.com^\n",
    );

    let second = FilterList::from_text(
        "Test Ads",
        "https://example.org/second.txt",
        "||tracker.example.com^\n",
    );

    assert!(lists.add(first).is_ok());
    assert!(lists.add(second).is_err());
}

#[test]
fn combines_enabled_lists_and_removes_duplicate_rules() {
    let mut lists = FilterListCollection::new();

    lists
        .add(FilterList::from_text(
            "Ads",
            "https://example.org/ads.txt",
            "||ads.example.com^\n||shared.example.com^\n",
        ))
        .unwrap();

    lists
        .add(FilterList::from_text(
            "Trackers",
            "https://example.org/trackers.txt",
            "||shared.example.com^\n||tracker.example.com^\n",
        ))
        .unwrap();

    let combined = lists.combined_rules();

    assert!(combined.contains("||ads.example.com^"));
    assert!(combined.contains("||shared.example.com^"));
    assert!(combined.contains("||tracker.example.com^"));

    assert_eq!(
        combined.matches("||shared.example.com^").count(),
        1
    );
}

#[test]
fn disabled_lists_are_not_combined() {
    let mut lists = FilterListCollection::new();

    lists
        .add(FilterList::from_text(
            "Ads",
            "https://example.org/ads.txt",
            "||ads.example.com^\n",
        ))
        .unwrap();

    lists.set_enabled("Ads", false).unwrap();

    assert!(lists.combined_rules().is_empty());
}

#[test]
fn rejects_empty_filter_list() {
    let mut lists = FilterListCollection::new();

    let list = FilterList::from_text(
        "Empty",
        "https://example.org/empty.txt",
        "",
    );

    assert!(lists.add(list).is_err());
}