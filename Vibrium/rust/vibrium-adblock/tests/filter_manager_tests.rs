use std::fs;
use std::time::{SystemTime, UNIX_EPOCH};

use vibrium_adblock::engine::VibriumAdblock;
use vibrium_adblock::error::VibriumAdblockError;
use vibrium_adblock::filter_list_updater::FilterListDownloader;
use vibrium_adblock::filter_lists::{FilterList, FilterListCollection};
use vibrium_adblock::filter_manager::FilterManager;
use vibrium_adblock::filter_storage::FilterStorage;

struct FakeDownloader {
    result: Result<String, String>,
}

impl FilterListDownloader for FakeDownloader {
    fn download(&self, _list: &FilterList) -> Result<String, VibriumAdblockError> {
        self.result
            .clone()
            .map_err(VibriumAdblockError::FilterListError)
    }
}

fn unique_test_directory(label: &str) -> std::path::PathBuf {
    let timestamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("System clock should be after the Unix epoch")
        .as_nanos();

    std::env::temp_dir().join(format!(
        "vibrium-{label}-{}-{timestamp}",
        std::process::id()
    ))
}

fn manager_with_fake_downloader(
    directory: std::path::PathBuf,
    initial_rules: &str,
    download_result: Result<String, String>,
) -> FilterManager<FakeDownloader> {
    let mut lists = FilterListCollection::new();

    lists
        .add(FilterList::from_text(
            "test-list",
            "https://example.com/filters.txt",
            initial_rules,
        ))
        .expect("Filter list should be valid");

    FilterManager::new(
        lists,
        FakeDownloader {
            result: download_result,
        },
        FilterStorage::new(directory),
    )
}

#[test]
fn failed_download_preserves_existing_rules() {
    let directory = unique_test_directory("failed-download");

    let mut manager = manager_with_fake_downloader(
        directory.clone(),
        "||ads.example^",
        Err("Simulated download failure".to_string()),
    );

    let result = manager.update_list("test-list");

    assert!(result.is_err());

    assert_eq!(
        manager
            .lists()
            .get("test-list")
            .expect("Filter list should exist")
            .rules,
        "||ads.example^"
    );

    assert!(
        !manager
            .storage()
            .exists("test-list")
            .expect("Storage existence check should succeed")
    );

    let _ = fs::remove_dir_all(directory);
}

#[test]
fn successful_download_replaces_rules_and_saves_them() {
    let directory = unique_test_directory("successful-download");

    let mut manager = manager_with_fake_downloader(
        directory.clone(),
        "||old-ads.example^",
        Ok("||new-ads.example^".to_string()),
    );

    manager
        .update_list("test-list")
        .expect("Updating the filter list should succeed");

    let list = manager
        .lists()
        .get("test-list")
        .expect("Filter list should exist");

    assert_eq!(list.rules, "||new-ads.example^");

    let stored_rules = manager
        .storage()
        .load("test-list")
        .expect("Updated rules should be saved");

    assert_eq!(stored_rules, "||new-ads.example^");

    let engine = manager
        .rebuild_engine()
        .expect("Engine should rebuild successfully");

    assert!(
        !engine
            .should_block(
                "https://old-ads.example/banner.js",
                "https://site.example/",
                "script",
            )
            .expect("Old-rule request should be evaluated")
    );

    assert!(
        engine
            .should_block(
                "https://new-ads.example/banner.js",
                "https://site.example/",
                "script",
            )
            .expect("New-rule request should be evaluated")
    );

    let _ = fs::remove_dir_all(directory);
}

#[test]
fn restores_saved_rules_before_rebuilding_engine() {
    let directory = unique_test_directory("restore-rules");

    let storage = FilterStorage::new(directory.clone());

    storage
        .save("test-list", "||saved-ads.example^")
        .expect("Saved rules should be written");

    let mut manager = manager_with_fake_downloader(
        directory.clone(),
        "||original-ads.example^",
        Err("Downloader should not be called".to_string()),
    );

    manager
        .load_stored_lists()
        .expect("Stored rules should load successfully");

    assert_eq!(
        manager
            .lists()
            .get("test-list")
            .expect("Filter list should exist")
            .rules,
        "||saved-ads.example^"
    );

    let engine: VibriumAdblock = manager
        .rebuild_engine()
        .expect("Engine should rebuild from restored rules");

    assert!(
        engine
            .should_block(
                "https://saved-ads.example/banner.js",
                "https://site.example/",
                "script",
            )
            .expect("Saved-rule request should be evaluated")
    );

    let _ = fs::remove_dir_all(directory);
}

#[test]
fn missing_stored_file_keeps_existing_rules() {
    let directory = unique_test_directory("missing-storage");

    let mut manager = manager_with_fake_downloader(
        directory.clone(),
        "||current-ads.example^",
        Err("Downloader should not be called".to_string()),
    );

    manager
        .load_stored_lists()
        .expect("Missing stored files should not cause an error");

    assert_eq!(
        manager
            .lists()
            .get("test-list")
            .expect("Filter list should exist")
            .rules,
        "||current-ads.example^"
    );

    let _ = fs::remove_dir_all(directory);
}
