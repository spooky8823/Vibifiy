use std::fs::{self, File};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};

use crate::error::VibriumAdblockError;

const MAX_STORED_LIST_SIZE: u64 = 10 * 1024 * 1024;

pub struct FilterStorage {
    directory: PathBuf,
}

impl FilterStorage {
    pub fn new(directory: impl Into<PathBuf>) -> Self {
        Self {
            directory: directory.into(),
        }
    }

    pub fn save(&self, name: &str, contents: &str) -> Result<(), VibriumAdblockError> {
        validate_name(name)?;

        if contents.trim().is_empty() {
            return Err(storage_error("Cannot save an empty filter list"));
        }

        if contents.len() as u64 > MAX_STORED_LIST_SIZE {
            return Err(storage_error(
                "Filter list exceeds the 10 MiB storage limit",
            ));
        }

        fs::create_dir_all(&self.directory)
            .map_err(|error| io_error("create storage directory", error))?;

        let destination = self.directory.join(format!("{name}.txt"));
        let temporary = self.directory.join(format!(".{name}.tmp"));

        let result = (|| {
            let mut file = File::create(&temporary)
                .map_err(|error| io_error("create temporary file", error))?;

            file.write_all(contents.as_bytes())
                .map_err(|error| io_error("write filter list", error))?;

            file.sync_all()
                .map_err(|error| io_error("sync filter list", error))?;

            drop(file);

            fs::rename(&temporary, &destination)
                .map_err(|error| io_error("replace stored filter list", error))?;

            Ok(())
        })();

        if result.is_err() {
            let _ = fs::remove_file(&temporary);
        }

        result
    }

    pub fn load(&self, name: &str) -> Result<String, VibriumAdblockError> {
        validate_name(name)?;

        let path = self.directory.join(format!("{name}.txt"));

        let metadata =
            fs::metadata(&path).map_err(|error| io_error("inspect stored filter list", error))?;

        if metadata.len() > MAX_STORED_LIST_SIZE {
            return Err(storage_error(
                "Stored filter list exceeds the 10 MiB size limit",
            ));
        }

        let file = File::open(&path).map_err(|error| io_error("open stored filter list", error))?;

        let mut contents = String::new();

        file.take(MAX_STORED_LIST_SIZE + 1)
            .read_to_string(&mut contents)
            .map_err(|error| io_error("read stored filter list", error))?;

        if contents.len() as u64 > MAX_STORED_LIST_SIZE {
            return Err(storage_error(
                "Stored filter list exceeds the 10 MiB size limit",
            ));
        }

        if contents.trim().is_empty() {
            return Err(storage_error("Stored filter list is empty"));
        }

        Ok(contents)
    }

    pub fn delete(&self, name: &str) -> Result<(), VibriumAdblockError> {
        validate_name(name)?;

        let path = self.directory.join(format!("{name}.txt"));

        fs::remove_file(&path).map_err(|error| io_error("delete stored filter list", error))
    }

    pub fn directory(&self) -> &Path {
        &self.directory
    }
}

fn validate_name(name: &str) -> Result<(), VibriumAdblockError> {
    if name.is_empty()
        || name.len() > 100
        || !name
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || byte == b'-' || byte == b'_')
    {
        return Err(storage_error(
            "Filter list names may contain only letters, numbers, hyphens, and underscores",
        ));
    }

    Ok(())
}

fn storage_error(message: impl Into<String>) -> VibriumAdblockError {
    VibriumAdblockError::FilterListError(message.into())
}

fn io_error(operation: &str, error: std::io::Error) -> VibriumAdblockError {
    storage_error(format!("Failed to {operation}: {error}"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn temporary_directory() -> PathBuf {
        let unique = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();

        std::env::temp_dir().join(format!("vibrium-filter-storage-{unique}"))
    }

    #[test]
    fn saves_and_loads_filter_list() {
        let directory = temporary_directory();
        let storage = FilterStorage::new(&directory);
        let rules = "||ads.example.com^\n";

        storage.save("test-list", rules).unwrap();

        assert_eq!(storage.load("test-list").unwrap(), rules);

        fs::remove_dir_all(directory).unwrap();
    }

    #[test]
    fn rejects_path_traversal_names() {
        let storage = FilterStorage::new(temporary_directory());

        assert!(storage.save("../outside", "||ads.example.com^\n").is_err());
        assert!(storage.load("../outside").is_err());
    }

    #[test]
    fn rejects_empty_filter_lists() {
        let storage = FilterStorage::new(temporary_directory());

        assert!(storage.save("empty-list", " \n").is_err());
    }

    #[test]
    fn deletes_stored_filter_list() {
        let directory = temporary_directory();
        let storage = FilterStorage::new(&directory);

        storage.save("test-list", "||ads.example.com^\n").unwrap();
        storage.delete("test-list").unwrap();

        assert!(storage.load("test-list").is_err());

        fs::remove_dir_all(directory).unwrap();
    }
}
