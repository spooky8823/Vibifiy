use std::process::Command as ShellCommand;
use crate::types::{Command, CommandKind, CommandStatus};
use crate::sandbox::Sandbox;

pub struct Executor {
    pub sandbox: Sandbox,
}

impl Executor {
    pub fn new(sandbox_path: &str, _auto_execute: bool) -> std::io::Result<Self> {
        Ok(Self {
            sandbox: Sandbox::new(sandbox_path)?,
        })
    }

    pub fn run(&self, cmd: &mut Command) {
        cmd.status = CommandStatus::Running;

        let result = match cmd.kind {
            CommandKind::CreateFile | CommandKind::EditFile => {
                self.run_file_write(cmd)
            }
            CommandKind::CreateFolder => {
                self.run_folder_create(cmd)
            }
            CommandKind::DeleteFile => {
                self.run_file_delete(cmd)
            }
            CommandKind::DeleteFolder => {
                self.run_folder_delete(cmd)
            }
            CommandKind::RunShell | CommandKind::InstallDep => {
                self.run_shell(cmd)
            }
            CommandKind::MoveFile | CommandKind::RenameFile => {
                self.run_move_file(cmd)
            }
            CommandKind::CopyFile => {
                self.run_copy_file(cmd)
            }
            CommandKind::RenameFolder => {
                self.run_move_folder(cmd)
            }
            CommandKind::OpenFolder | CommandKind::OpenApp => {
                self.run_open(cmd)
            }
            CommandKind::ReadFile => {
                self.run_read_file(cmd)
            }
            CommandKind::PathTree => {
                self.run_path_tree(cmd)
            }
            CommandKind::DownloadRepo => {
                self.run_download_repo(cmd)
            }
            CommandKind::DownloadPrivateRepo => {
                self.run_download_private_repo(cmd)
            }

        };

        match result {
            Ok(output) => {
                cmd.status = CommandStatus::Done;
                cmd.output = Some(output);
            }
            Err(e) => {
                cmd.status = CommandStatus::Failed(e.clone());
                cmd.output = Some(e);
            }
        }
    }

    fn run_file_write(&self, cmd: &Command) -> Result<String, String> {
        let path = cmd.path.as_deref()
            .ok_or("No path specified")?;
        let content = cmd.content.as_deref()
            .unwrap_or("");

        self.sandbox.write_file(path, content)
            .map(|_| format!("Written: {}", path))
            .map_err(|e| e.to_string())
    }

    fn run_file_delete(&self, cmd: &Command) -> Result<String, String> {
        let path = cmd.path.as_deref()
            .ok_or("No path specified")?;

        self.sandbox.delete_file(path)
            .map(|_| format!("Deleted: {}", path))
            .map_err(|e| e.to_string())
    }

    fn run_shell(&self, cmd: &Command) -> Result<String, String> {
        let command = cmd.content.as_deref()
            .ok_or("No command specified")?;

        let output = ShellCommand::new("sh")
            .arg("-c")
            .arg(command)
            .current_dir(self.sandbox.working_dir())
            .output()
            .map_err(|e| e.to_string())?;

        let stdout = String::from_utf8_lossy(&output.stdout).to_string();
        let stderr = String::from_utf8_lossy(&output.stderr).to_string();

        if output.status.success() {
            Ok(if stdout.is_empty() { "Done".to_string() } else { stdout })
        } else {
            Err(if stderr.is_empty() { "Command failed".to_string() } else { stderr })
        }
    }

        fn run_folder_create(&self, cmd: &Command) -> Result<String, String> {
        let path = cmd.path.as_deref()
            .ok_or("No path specified")?;
        self.sandbox.create_folder(path)
            .map(|_| format!("Created folder: {}", path))
            .map_err(|e| e.to_string())
    }

    fn run_folder_delete(&self, cmd: &Command) -> Result<String, String> {
        let path = cmd.path.as_deref()
            .ok_or("No path specified")?;
        self.sandbox.delete_folder(path)
            .map(|_| format!("Deleted folder: {}", path))
            .map_err(|e| e.to_string())
    }

    fn run_move_file(&self, cmd: &Command) -> Result<String, String> {
        let src = cmd.path.as_deref().ok_or("No source path")?;
        let dst = cmd.content.as_deref().ok_or("No destination")?;
        let dst_path = if dst.starts_with('/') || dst.contains(":/") {
            dst.to_string()
        } else {
            let src_dir = std::path::Path::new(src).parent().unwrap_or(std::path::Path::new("."));
            src_dir.join(dst).to_string_lossy().to_string()
        };
        let resolved = self.sandbox.resolve(&dst_path).ok_or("Destination escapes sandbox")?;
        std::fs::rename(
            self.sandbox.resolve(src).ok_or("Source escapes sandbox")?,
            &resolved,
        ).map_err(|e| e.to_string())?;
        Ok(format!("Moved: {} -> {}", src, dst_path))
    }

    fn run_copy_file(&self, cmd: &Command) -> Result<String, String> {
        let src = cmd.path.as_deref().ok_or("No source path")?;
        let dst = cmd.content.as_deref().ok_or("No destination")?;
        let dst_path = if dst.starts_with('/') || dst.contains(":/") {
            dst.to_string()
        } else {
            let src_dir = std::path::Path::new(src).parent().unwrap_or(std::path::Path::new("."));
            src_dir.join(dst).to_string_lossy().to_string()
        };
        let resolved = self.sandbox.resolve(&dst_path).ok_or("Destination escapes sandbox")?;
        std::fs::copy(
            self.sandbox.resolve(src).ok_or("Source escapes sandbox")?,
            &resolved,
        ).map_err(|e| e.to_string())?;
        Ok(format!("Copied: {} -> {}", src, dst_path))
    }

    fn run_move_folder(&self, cmd: &Command) -> Result<String, String> {
        self.run_move_file(cmd)
    }

    fn run_open(&self, cmd: &Command) -> Result<String, String> {
        let target = cmd.path.as_deref().or(cmd.content.as_deref()).ok_or("No target")?;
        let resolved = self.sandbox.resolve(target).unwrap_or_else(|| std::path::PathBuf::from(target));
        std::process::Command::new("xdg-open")
            .arg(&resolved)
            .spawn()
            .map_err(|e| e.to_string())?;
        Ok(format!("Opened: {}", target))
    }

    fn run_read_file(&self, cmd: &Command) -> Result<String, String> {
        let path = cmd.path.as_deref().ok_or("No path")?;
        let resolved = self.sandbox.resolve(path).ok_or("Path escapes sandbox")?;
        std::fs::read_to_string(&resolved)
            .map_err(|e| e.to_string())
    }

    fn run_path_tree(&self, cmd: &Command) -> Result<String, String> {
        let dir = cmd.path.as_deref().unwrap_or(".");
        let resolved = self.sandbox.resolve(dir).ok_or("Path escapes sandbox")?;
        let mut output = String::new();
        fn walk(path: &std::path::Path, prefix: &str, output: &mut String, exclude: &Option<String>) {
            if let Ok(entries) = std::fs::read_dir(path) {
                let mut entries: Vec<_> = entries.filter_map(|e| e.ok()).collect();
                entries.sort_by(|a, b| a.file_name().cmp(&b.file_name()));
                for (i, entry) in entries.iter().enumerate() {
                    let is_last = i == entries.len() - 1;
                    let name = entry.file_name().to_string_lossy().to_string();
                    if let Some(ex) = exclude {
                        if name.contains(ex) { continue; }
                    }
                    let connector = if is_last { "└── " } else { "├── " };
                    output.push_str(&format!("{}{}{}\n", prefix, connector, name));
                    if entry.file_type().map(|f| f.is_dir()).unwrap_or(false) {
                        let new_prefix = format!("{}{}   ", prefix, if is_last { " " } else { "│" });
                        walk(&entry.path(), &new_prefix, output, exclude);
                    }
                }
            }
        }
        walk(&resolved, "", &mut output, &cmd.content);
        Ok(output)
    }

    fn run_download_repo(&self, cmd: &Command) -> Result<String, String> {
        let url = cmd.content.as_deref().ok_or("No URL")?;
        let target = cmd.path.as_deref().unwrap_or(".");
        let resolved = self.sandbox.resolve(target).ok_or("Path escapes sandbox")?;
        let output = std::process::Command::new("git")
            .args(["clone", url, &resolved.to_string_lossy()])
            .output()
            .map_err(|e| e.to_string())?;
        if output.status.success() {
            Ok(format!("Cloned: {} -> {}", url, target))
        } else {
            Err(String::from_utf8_lossy(&output.stderr).to_string())
        }
    }

    fn run_download_private_repo(&self, cmd: &Command) -> Result<String, String> {
        let full = cmd.content.as_deref().ok_or("No URL+token")?;
        let parts: Vec<&str> = full.split("|TOKEN:").collect();
        let url = parts.first().unwrap_or(&"");
        let token = parts.get(1).unwrap_or(&"");
        let target = cmd.path.as_deref().unwrap_or(".");
        let resolved = self.sandbox.resolve(target).ok_or("Path escapes sandbox")?;
        let auth_url = if url.starts_with("https://") {
            url.replace("https://", &format!("https://oauth2:{}@", token))
        } else {
            url.to_string()
        };
        let output = std::process::Command::new("git")
            .args(["clone", &auth_url, &resolved.to_string_lossy()])
            .output()
            .map_err(|e| e.to_string())?;
        if output.status.success() {
            Ok(format!("Cloned private repo: {} -> {}", url, target))
        } else {
            Err(String::from_utf8_lossy(&output.stderr).to_string())
        }
    }
}