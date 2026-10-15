use std::path::{Path, PathBuf};


pub fn icons_dir() -> PathBuf {
    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            for candidate in [dir.join("icons"), dir.join("src").join("icons")] {
                if candidate.is_dir() {
                    return candidate;
                }
            }
        }
    }
    PathBuf::from("src/icons")
}


#[cfg(not(target_os = "linux"))]
pub fn dir_signature(root: &Path) -> (u64, u64, u128) {
    fn walk(path: &Path, count: &mut u64, size: &mut u64, newest: &mut u128) {
        if let Ok(entries) = std::fs::read_dir(path) {
            for entry in entries.flatten() {
                *count += 1;
                if let Ok(meta) = entry.metadata() {
                    *size += meta.len();
                    if let Ok(modified) = meta.modified() {
                        if let Ok(d) = modified.duration_since(std::time::UNIX_EPOCH) {
                            *newest = (*newest).max(d.as_nanos());
                        }
                    }
                    if meta.is_dir() {
                        walk(&entry.path(), count, size, newest);
                    }
                }
            }
        }
    }
    let (mut count, mut size, mut newest) = (0u64, 0u64, 0u128);
    walk(root, &mut count, &mut size, &mut newest);
    (count, size, newest)
}
