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