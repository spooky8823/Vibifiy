use vibrium_adblock::error::VibriumAdblockError;
use vibrium_adblock::filter_list_updater::FilterListUpdater;
use vibrium_adblock::filter_lists::FilterList;

fn make_list(url: String) -> FilterList {
    FilterList::new("test-list", url)
}

#[test]
fn rejects_http_urls_before_connecting() {
    let updater = FilterListUpdater::default();
    let list = make_list("http://127.0.0.1:1/filters.txt".to_string());

    assert!(matches!(
        updater.download(&list),
        Err(VibriumAdblockError::FilterListError(_))
    ));
}

#[test]
fn rejects_malformed_urls() {
    let updater = FilterListUpdater::default();
    let list = make_list("not a valid URL".to_string());

    assert!(updater.download(&list).is_err());
}

#[test]
fn rejects_empty_list_names() {
    let updater = FilterListUpdater::default();
    let list = FilterList::new("", "https://example.com/filters.txt");

    assert!(matches!(
        updater.download(&list),
        Err(VibriumAdblockError::InvalidConfiguration(_))
    ));
}

#[test]
fn rejects_empty_source_urls() {
    let updater = FilterListUpdater::default();
    let list = FilterList::new("test-list", "");

    assert!(matches!(
        updater.download(&list),
        Err(VibriumAdblockError::InvalidConfiguration(_))
    ));
}
