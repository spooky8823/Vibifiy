use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

use vibrium_adblock::engine::VibriumAdblock;
use vibrium_adblock::filter_list_updater::FilterListUpdater;
use vibrium_adblock::filter_lists::{FilterList, FilterListCollection};
use vibrium_adblock::filter_manager::FilterManager;
use vibrium_adblock::filter_storage::FilterStorage;

fn temporary_directory() -> PathBuf {
    let unique = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_nanos();

    std::env::temp_dir().join(format!("vibrium-manager-tests-{unique}"))
}

#[test]
fn rebuilds_engine_from_existing_filter_rules() {
    let mut lists = FilterListCollection::new();

    lists
        .add(FilterList::from_text(
            "Test Ads",
            "https://example.org/filters.txt",
            "||ads.example.com^\n",
        ))
        .unwrap();

    let manager = FilterManager::new(
        lists,
        FilterListUpdater::default(),
        FilterStorage::new(temporary_directory()),
    );

    let engine = manager.rebuild_engine().unwrap();

    assert!(
        engine
            .should_block(
                "https://ads.example.com/banner.js",
                "https://example.org/",
                "script",
            )
            .unwrap()
    );
}

#[test]
fn rebuilt_engine_allows_unmatched_requests() {
    let mut lists = FilterListCollection::new();

    lists
        .add(FilterList::from_text(
            "Test Ads",
            "https://example.org/filters.txt",
            "||ads.example.com^\n",
        ))
        .unwrap();

    let manager = FilterManager::new(
        lists,
        FilterListUpdater::default(),
        FilterStorage::new(temporary_directory()),
    );

    let engine: VibriumAdblock = manager.rebuild_engine().unwrap();

    assert!(
        !engine
            .should_block(
                "https://cdn.example.org/app.js",
                "https://example.org/",
                "script",
            )
            .unwrap()
    );
}
