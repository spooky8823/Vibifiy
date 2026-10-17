use serde::{Deserialize, Serialize};
use uuid::Uuid;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum CommandKind {
    CreateFile,
    CreateFolder,
    EditFile,
    DeleteFile,
    DeleteFolder,
    RunShell,
    InstallDep,
    ReadFile,
    MoveFile,
    CopyFile,
    RenameFile,
    RenameFolder,
    OpenFolder,
    OpenApp,
    DownloadRepo,
    DownloadPrivateRepo,
    PathTree,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum CommandStatus {
    Pending,
    Approved,
    Rejected,
    Running,
    Done,
    Failed(String),
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Command {
    pub id: String,
    pub kind: CommandKind,
    pub path: Option<String>,
    pub content: Option<String>,
    pub status: CommandStatus,
    pub output: Option<String>,
}

impl Command {
    pub fn new(kind: CommandKind, path: Option<String>, content: Option<String>) -> Self {
        Self {
            id: Uuid::new_v4().to_string(),
            kind,
            path,
            content,
            status: CommandStatus::Pending,
            output: None,
        }
    }
}

impl CommandKind {
    /// Human-readable name used on approval cards and in the Logs view.
    pub fn label(&self) -> &'static str {
        match self {
            CommandKind::CreateFile => "Create File",
            CommandKind::CreateFolder => "Create Folder",
            CommandKind::EditFile => "Edit File",
            CommandKind::DeleteFile => "Delete File",
            CommandKind::DeleteFolder => "Delete Folder",
            CommandKind::RunShell => "Run Command",
            CommandKind::InstallDep => "Install Dependency",
            CommandKind::ReadFile => "Read File",
            CommandKind::MoveFile => "Move File",
            CommandKind::CopyFile => "Copy File",
            CommandKind::RenameFile => "Rename File",
            CommandKind::RenameFolder => "Rename Folder",
            CommandKind::OpenFolder => "Open Folder",
            CommandKind::OpenApp => "Open App",
            CommandKind::DownloadRepo => "Download Repo",
            CommandKind::DownloadPrivateRepo => "Download Private Repo",
            CommandKind::PathTree => "Path Tree",
        }
    }
}

impl Command {
    /// Text shown under the path on an approval card.
    ///
    /// Shell commands are shown (almost) in full, because the person approving has to
    /// see exactly what will run. Secrets are never shown: a private-repo access token
    /// is masked. Very long file contents are cut with a note saying how much is hidden.
    pub fn display_detail(&self) -> String {
        let raw = match (&self.kind, self.content.as_deref()) {
            (_, None) => return String::new(),
            (CommandKind::DownloadPrivateRepo, Some(c)) => {
                let url = c.split("|TOKEN:").next().unwrap_or("");
                format!("{} (access token hidden)", url)
            }
            (CommandKind::MoveFile, Some(c))
            | (CommandKind::CopyFile, Some(c))
            | (CommandKind::RenameFile, Some(c))
            | (CommandKind::RenameFolder, Some(c)) => format!("\u{2192} {}", c),
            (_, Some(c)) => c.to_string(),
        };

        let limit = match self.kind {
            CommandKind::RunShell | CommandKind::InstallDep => 2000,
            _ => 400,
        };
        let total = raw.chars().count();
        if total <= limit {
            raw
        } else {
            let head: String = raw.chars().take(limit).collect();
            format!("{}\u{2026} (+{} more characters)", head, total - limit)
        }
    }
}