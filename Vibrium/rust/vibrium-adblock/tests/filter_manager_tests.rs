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

#[test]
fn invalid_download_preserves_existing_rules_and_saved_file() {
    let directory = unique_test_directory("invalid-download");

    let storage = FilterStorage::new(directory.clone());
    storage
        .save("test-list", "||previously-saved-ads.example^")
        .expect("Existing valid rules should be saved");

    let mut manager = manager_with_fake_downloader(
        directory.clone(),
        "||current-ads.example^",
        Ok("! Only a comment\n[Adblock Plus 2.0]\n".to_string()),
    );

    let result = manager.update_list("test-list");

    assert!(result.is_err(), "Invalid filter content must be rejected");

    assert_eq!(
        manager
            .lists()
            .get("test-list")
            .expect("Filter list should exist")
            .rules,
        "||current-ads.example^",
        "In-memory rules must remain unchanged after an invalid update"
    );

    assert_eq!(
        storage
            .load("test-list")
            .expect("Previously saved rules should remain available"),
        "||previously-saved-ads.example^",
        "Invalid content must not overwrite the saved filter list"
    );

    let _ = fs::remove_dir_all(directory);
}

#[test]
fn mixed_valid_and_invalid_download_saves_only_after_validation() {
    let directory = unique_test_directory("mixed-download");

    let mut manager = manager_with_fake_downloader(
        directory.clone(),
        "||current-ads.example^",
        Ok("||new-ads.example^".to_string()),
    );

    let result = manager.update_list("test-list");

    assert!(
        result.is_ok(),
        "A list containing valid rules should be accepted even if some lines are rejected"
    );

    assert_eq!(
        manager
            .lists()
            .get("test-list")
            .expect("Filter list should exist")
            .rules,
        "||new-ads.example^"
    );

    assert_eq!(
        manager
            .storage()
            .load("test-list")
            .expect("Validated list should be saved"),
        "||new-ads.example^"
    );

    let _ = fs::remove_dir_all(directory);
}

#[test]
fn storage_failure_preserves_existing_in_memory_rules() {
    let directory = unique_test_directory("storage-failure");

    // A regular file at this path prevents the storage layer from creating
    // the directory it needs for saving the updated filter list.
    fs::write(&directory, "not a directory").expect("Test setup should create a blocking file");

    let mut manager = manager_with_fake_downloader(
        directory.clone(),
        "||current-ads.example^",
        Ok("||new-ads.example^".to_string()),
    );

    let result = manager.update_list("test-list");

    assert!(
        result.is_err(),
        "The update should fail when storage cannot be created"
    );

    assert_eq!(
        manager
            .lists()
            .get("test-list")
            .expect("Filter list should exist")
            .rules,
        "||current-ads.example^",
        "Existing in-memory rules must remain unchanged after a storage failure"
    );

    let _ = fs::remove_file(directory);
}

#[test]
fn failed_download_preserves_existing_saved_rules() {
    let directory = unique_test_directory("failed-download-saved-rules");

    let mut manager = manager_with_fake_downloader(
        directory.clone(),
        "||current-ads.example^",
        Err("simulated network failure".to_string()),
    );

    // Save the existing rules before attempting the update.
    manager
        .storage()
        .save("test-list", "||current-ads.example^")
        .expect("Existing rules should be saved");

    let result = manager.update_list("test-list");

    assert!(result.is_err(), "The download should fail");

    let saved_rules = manager
        .storage()
        .load("test-list")
        .expect("Existing saved rules should remain readable");

    assert_eq!(
        saved_rules, "||current-ads.example^",
        "A failed download must not overwrite the saved rules"
    );

    let _ = fs::remove_dir_all(directory);
}

#[test]
fn successful_update_changes_active_engine_rules() {
    let directory = unique_test_directory("active-engine-update");

    let mut manager = manager_with_fake_downloader(
        directory.clone(),
        "||old-ads.example^",
        Ok("||new-ads.example^".to_string()),
    );

    // The initial engine should use the original rules.
    assert!(
        manager
            .active_engine()
            .unwrap()
            .should_block(
                "https://old-ads.example/ad.js",
                "https://site.example/",
                "script",
            )
            .unwrap()
    );

    manager
        .update_list("test-list")
        .expect("The valid update should succeed");

    let engine = manager
        .active_engine()
        .expect("An active engine should be available after updating");

    assert!(
        engine
            .should_block(
                "https://new-ads.example/ad.js",
                "https://site.example/",
                "script",
            )
            .unwrap(),
        "The updated engine should block the new rule"
    );

    assert!(
        !engine
            .should_block(
                "https://old-ads.example/ad.js",
                "https://site.example/",
                "script",
            )
            .unwrap(),
        "The updated engine should no longer block the removed rule"
    );

    let _ = fs::remove_dir_all(directory);
}

#[test]
fn failed_update_preserves_active_engine_rules() {
    let directory = unique_test_directory("active-engine-failed-update");

    let mut manager = manager_with_fake_downloader(
        directory.clone(),
        "||current-ads.example^",
        Err("simulated network failure".to_string()),
    );

    let result = manager.update_list("test-list");

    assert!(result.is_err(), "The download should fail");

    let engine = manager
        .active_engine()
        .expect("The previous engine should remain active");

    assert!(
        engine
            .should_block(
                "https://current-ads.example/ad.js",
                "https://site.example/",
                "script",
            )
            .unwrap(),
        "The existing engine should continue blocking its original rule"
    );

    let _ = fs::remove_dir_all(directory);
}

#[test]
fn html_download_preserves_existing_rules_and_saved_file() {
    let directory = unique_test_directory("html-download");

    let storage = FilterStorage::new(directory.clone());
    storage
        .save("test-list", "||previously-saved-ads.example^")
        .expect("Existing valid rules should be saved");

    let mut manager = manager_with_fake_downloader(
        directory.clone(),
        "||current-ads.example^",
        Ok("<!DOCTYPE html>\n<html><body>Access denied</body></html>".to_string()),
    );

    let result = manager.update_list("test-list");

    assert!(
        result.is_err(),
        "HTML error pages must not be accepted as filter lists"
    );

    assert_eq!(
        manager.lists().get("test-list").unwrap().rules,
        "||current-ads.example^",
        "An invalid download must not replace in-memory rules"
    );

    assert_eq!(
        storage.load("test-list").unwrap(),
        "||previously-saved-ads.example^",
        "An invalid download must not overwrite saved rules"
    );

    let engine = manager.active_engine().unwrap();

    assert!(
        engine
            .should_block(
                "https://current-ads.example/ad.js",
                "https://site.example/",
                "script",
            )
            .unwrap(),
        "The old active engine must continue blocking"
    );

    let _ = fs::remove_dir_all(directory);
}

#[test]
fn prose_download_preserves_existing_rules_and_saved_file() {
    let directory = unique_test_directory("prose-download");

    let storage = FilterStorage::new(directory.clone());
    storage
        .save("test-list", "||previously-saved-ads.example^")
        .expect("Existing valid rules should be saved");

    let prose = [
        "Access to this resource has been denied",
        "Please contact your network administrator",
        "Your request cannot be completed today",
        "Try signing in to continue using this service",
    ]
    .join("\n");

    let mut manager =
        manager_with_fake_downloader(directory.clone(), "||current-ads.example^", Ok(prose));

    assert!(
        manager.update_list("test-list").is_err(),
        "Prose must not be accepted as a replacement filter list"
    );

    assert_eq!(
        manager.lists().get("test-list").unwrap().rules,
        "||current-ads.example^"
    );

    assert_eq!(
        storage.load("test-list").unwrap(),
        "||previously-saved-ads.example^"
    );

    assert!(
        manager
            .active_engine()
            .unwrap()
            .should_block(
                "https://current-ads.example/ad.js",
                "https://site.example/",
                "script",
            )
            .unwrap()
    );

    let _ = fs::remove_dir_all(directory);
}
