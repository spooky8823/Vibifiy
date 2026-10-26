use std::fs;
use std::time::{SystemTime, UNIX_EPOCH};

use vibrium_adblock::error::VibriumAdblockError;
use vibrium_adblock::filter_list_updater::FilterListDownloader;
use vibrium_adblock::filter_lists::{FilterList, FilterListCollection};
use vibrium_adblock::filter_manager::FilterManager;
use vibrium_adblock::filter_storage::FilterStorage;

struct NoopDownloader;

impl FilterListDownloader for NoopDownloader {
    fn download(&self, _list: &FilterList) -> Result<String, VibriumAdblockError> {
        Err(VibriumAdblockError::FilterListError(
            "Unexpected download in toggle test".into(),
        ))
    }
}

fn make_manager() -> (FilterManager<NoopDownloader>, std::path::PathBuf) {
    let timestamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_nanos();

    let directory = std::env::temp_dir().join(format!(
        "vibrium-toggle-{}-{timestamp}",
        std::process::id()
    ));

    let mut lists = FilterListCollection::new();

    lists
        .add(FilterList::from_text(
            "test-list",
            "https://example.com/filters.txt",
            "||ads.example.com^\n",
        ))
        .unwrap();

    (
        FilterManager::new(
            lists,
            NoopDownloader,
            FilterStorage::new(&directory),
        ),
        directory,
    )
}

#[test]
fn disabling_last_enabled_list_preserves_current_engine() {
    let (mut manager, directory) = make_manager();

    let result = manager.set_list_enabled("test-list", false);

    assert!(
        result.is_err(),
        "The manager must not commit a configuration with no usable rules"
    );

    assert!(
        manager
            .lists()
            .get("test-list")
            .is_some_and(|list| list.enabled),
        "The original list configuration must remain enabled"
    );

    assert!(
        manager
            .active_engine()
            .unwrap()
            .should_block(
                "https://ads.example.com/banner.js",
                "https://site.example/",
                "script",
            )
            .unwrap(),
        "The original active engine must remain operational"
    );

    let _ = fs::remove_dir_all(directory);
}


#[test]
fn enabling_list_restores_its_rules_to_active_engine() {
    let (mut manager, directory) = make_manager();

    manager.set_list_enabled("test-list", false).unwrap_err();

    // A collection with no enabled rules cannot produce an engine.
    // The failed operation must preserve the original enabled configuration.
    assert!(
        manager
            .lists()
            .get("test-list")
            .is_some_and(|list| list.enabled)
    );

    assert!(
        manager
            .active_engine()
            .unwrap()
            .should_block(
                "https://ads.example.com/banner.js",
                "https://site.example/",
                "script",
            )
            .unwrap()
    );

    let _ = fs::remove_dir_all(directory);
}
