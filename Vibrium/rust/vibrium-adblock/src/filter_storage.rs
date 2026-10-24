use std::fs::{self, File, OpenOptions};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

use crate::error::VibriumAdblockError;

const MAX_STORED_LIST_SIZE: u64 = 10 * 1024 * 1024;
static TEMP_FILE_COUNTER: AtomicU64 = AtomicU64::new(0);

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
        let temporary = self.create_temporary_file(name, contents)?;

        // Persist the temporary file before publishing it.
        let result = replace_file(&temporary, &destination);

        if result.is_err() {
            let _ = fs::remove_file(&temporary);
        }

        result
    }

    fn create_temporary_file(
        &self,
        name: &str,
        contents: &str,
    ) -> Result<PathBuf, VibriumAdblockError> {
        for _ in 0..100 {
            let id = TEMP_FILE_COUNTER.fetch_add(1, Ordering::Relaxed);
            let temporary = self.directory.join(format!(".{name}.{id}.tmp"));

            let opened = OpenOptions::new()
                .write(true)
                .create_new(true)
                .open(&temporary);

            match opened {
                Ok(mut file) => {
                    let write_result = file
                        .write_all(contents.as_bytes())
                        .and_then(|()| file.sync_all());

                    if let Err(error) = write_result {
                        drop(file);
                        let _ = fs::remove_file(&temporary);
                        return Err(io_error("write temporary filter list", error));
                    }

                    drop(file);
                    return Ok(temporary);
                }
                Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => continue,
                Err(error) => {
                    return Err(io_error("create temporary filter list", error));
                }
            }
        }

        Err(storage_error(
            "Could not allocate a unique temporary filter-list file",
        ))
    }

    pub fn load(&self, name: &str) -> Result<String, VibriumAdblockError> {
        validate_name(name)?;

        let path = self.directory.join(format!("{name}.txt"));
        let metadata =
            fs::metadata(&path).map_err(|error| io_error("inspect stored filter list", error))?;

        if !metadata.is_file() {
            return Err(storage_error("Stored filter list is not a regular file"));
        }

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

    pub fn exists(&self, name: &str) -> Result<bool, VibriumAdblockError> {
        validate_name(name)?;

        match fs::metadata(self.directory.join(format!("{name}.txt"))) {
            Ok(metadata) => {
                if !metadata.is_file() {
                    return Err(storage_error("Stored filter list is not a regular file"));
                }

                Ok(true)
            }
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(false),
            Err(error) => Err(io_error("inspect stored filter list", error)),
        }
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

#[cfg(windows)]
fn replace_file(temporary: &Path, destination: &Path) -> Result<(), VibriumAdblockError> {
    // Windows rename does not reliably replace an existing destination.
    // Use ReplaceFileW when the destination already exists so a failed
    // replacement does not require deleting the previous valid file.
    use std::os::windows::ffi::OsStrExt;
    use windows_sys::Win32::Storage::FileSystem::ReplaceFileW;

    if destination.exists() {
        let destination_wide: Vec<u16> = destination
            .as_os_str()
            .encode_wide()
            .chain(std::iter::once(0))
            .collect();

        let temporary_wide: Vec<u16> = temporary
            .as_os_str()
            .encode_wide()
            .chain(std::iter::once(0))
            .collect();

        // SAFETY: Both paths are valid, null-terminated UTF-16 strings.
        // The pointers remain alive for the duration of the call.
        let replaced = unsafe {
            ReplaceFileW(
                destination_wide.as_ptr(),
                temporary_wide.as_ptr(),
                std::ptr::null(),
                0,
                std::ptr::null(),
                std::ptr::null(),
            )
        };

        if replaced == 0 {
            return Err(storage_error(format!(
                "Failed to replace stored filter list: {}",
                std::io::Error::last_os_error()
            )));
        }

        Ok(())
    } else {
        fs::rename(temporary, destination)
            .map_err(|error| io_error("publish stored filter list", error))
    }
}

#[cfg(not(windows))]
fn replace_file(temporary: &Path, destination: &Path) -> Result<(), VibriumAdblockError> {
    fs::rename(temporary, destination)
        .map_err(|error| io_error("replace stored filter list", error))
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
    fn replaces_existing_filter_list() {
        let directory = temporary_directory();
        let storage = FilterStorage::new(&directory);

        storage.save("test-list", "||old.example^\n").unwrap();
        storage.save("test-list", "||new.example^\n").unwrap();

        assert_eq!(storage.load("test-list").unwrap(), "||new.example^\n");

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
    fn rejects_oversized_filter_lists() {
        let storage = FilterStorage::new(temporary_directory());
        let contents = "a".repeat(MAX_STORED_LIST_SIZE as usize + 1);

        assert!(storage.save("large-list", &contents).is_err());
    }

    #[test]
    fn rejects_corrupted_utf8_when_loading() {
        let directory = temporary_directory();
        fs::create_dir_all(&directory).unwrap();
        fs::write(directory.join("broken.txt"), [0xff, 0xfe]).unwrap();

        let storage = FilterStorage::new(&directory);

        assert!(storage.load("broken").is_err());

        fs::remove_dir_all(directory).unwrap();
    }

    #[test]
    fn reports_missing_lists_as_not_existing() {
        let directory = temporary_directory();
        let storage = FilterStorage::new(&directory);

        assert!(!storage.exists("missing").unwrap());

        fs::create_dir_all(directory).unwrap();
    }

    #[test]
    fn deletes_stored_filter_list() {
        let directory = temporary_directory();
        let storage = FilterStorage::new(&directory);

        storage.save("test-list", "||ads.example.com^\n").unwrap();
        storage.delete("test-list").unwrap();

        assert!(!storage.exists("test-list").unwrap());

        fs::remove_dir_all(directory).unwrap();
    }
}
