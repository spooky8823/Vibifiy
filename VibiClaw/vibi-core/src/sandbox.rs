use std::path::{Path, PathBuf};
use std::fs;

pub struct Sandbox {
    pub root: PathBuf,
}

impl Sandbox {
    pub fn new(root: &str) -> std::io::Result<Self> {
        let path = PathBuf::from(root);
        if !path.exists() {
            fs::create_dir_all(&path)?;
        }
        Ok(Self { root: path })
    }

    // Resolve a relative path safely inside the sandbox
    // Prevents path traversal like ../../etc/passwd
    pub fn resolve(&self, relative: &str) -> Option<PathBuf> {
    let joined = self.root.join(relative);
    let canonical_root = fs::canonicalize(&self.root).ok()?;

    // Create parent directories so canonicalize can work
    if let Some(parent) = joined.parent() {
        fs::create_dir_all(parent).ok()?;
    }

    let canonical_joined = if joined.exists() {
        fs::canonicalize(&joined).ok()?
    } else {
        let parent = joined.parent().unwrap_or(&self.root);
        // Try to canonicalize the parent; if it fails (parent doesn't exist), reject
        let canonical_parent = fs::canonicalize(parent).ok()?;
        let resolved = canonical_parent.join(joined.file_name()?);
        // Check that the canonical parent is inside the sandbox
        if !canonical_parent.starts_with(&canonical_root) {
            return None;
        }
        resolved
    };

    if canonical_joined.starts_with(&canonical_root) {
        Some(canonical_joined)
    } else {
        None
    }
}

    pub fn write_file(&self, relative_path: &str, content: &str) -> std::io::Result<()> {
        let path = self.resolve(relative_path)
            .ok_or_else(|| std::io::Error::new(
                std::io::ErrorKind::PermissionDenied,
                "Path escapes sandbox"
            ))?;

        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)?;
        }

        fs::write(path, content)
    }

    pub fn delete_file(&self, relative_path: &str) -> std::io::Result<()> {
        let path = self.resolve(relative_path)
            .ok_or_else(|| std::io::Error::new(
                std::io::ErrorKind::PermissionDenied,
                "Path escapes sandbox"
            ))?;

        fs::remove_file(path)
    }

    pub fn working_dir(&self) -> &Path {
        &self.root
    }

        pub fn create_folder(&self, relative_path: &str) -> std::io::Result<()> {
        let path = self.resolve(relative_path)
            .ok_or_else(|| std::io::Error::new(
                std::io::ErrorKind::PermissionDenied,
                "Path escapes sandbox"
            ))?;
        fs::create_dir_all(path)
    }

    pub fn delete_folder(&self, relative_path: &str) -> std::io::Result<()> {
        let path = self.resolve(relative_path)
            .ok_or_else(|| std::io::Error::new(
                std::io::ErrorKind::PermissionDenied,
                "Path escapes sandbox"
            ))?;
        fs::remove_dir_all(path)
    }
}