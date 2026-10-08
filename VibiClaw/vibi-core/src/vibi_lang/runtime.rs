use crate::executor::Executor;
use crate::types::{Command, CommandStatus};

pub fn execute(mut commands: Vec<Command>, executor: &Executor, require_approval: bool) -> Vec<String> {
    if commands.is_empty() {
        return vec!["No commands to execute.".to_string()];
    }
    
    // Format summary for approval
    let summary = format_commands_summary(&commands);
    
    // Log the detected commands
    println!("[VibiClaw] {} commands pending approval:\n{}", commands.len(), summary);
    
    if require_approval {
        // Queue commands to notification panel for user approval
        crate::notification_panel::queue_for_approval(commands.clone());
        return vec!["⏳ Commands queued for approval. Check the right panel.".to_string()];
    }
    
    let mut results = Vec::new();
    
    for cmd in &mut commands {
        executor.run(cmd);
        match &cmd.status {
            CommandStatus::Done => {
                let output = cmd.output.as_deref().unwrap_or("Done");
                results.push(format!("✅ {}", output));
            }
            CommandStatus::Failed(e) => {
                results.push(format!("❌ {}", e));
            }
            _ => {
                results.push(format!("⚠️ {:?}", cmd.status));
            }
        }
    }
    
    results
}

fn format_commands_summary(commands: &[Command]) -> String {
    let mut lines = Vec::new();
    for (i, cmd) in commands.iter().enumerate() {
        let kind = match cmd.kind {
            crate::types::CommandKind::CreateFile => "Create File",
            crate::types::CommandKind::CreateFolder => "Create Folder",
            crate::types::CommandKind::EditFile => "Edit File",
            crate::types::CommandKind::DeleteFile => "Delete File",
            crate::types::CommandKind::DeleteFolder => "Delete Folder",
            crate::types::CommandKind::RunShell => "Run Command",
            crate::types::CommandKind::RenameFile => "Rename File",
            crate::types::CommandKind::RenameFolder => "Rename Folder",
            crate::types::CommandKind::MoveFile => "Move File",
            crate::types::CommandKind::CopyFile => "Copy File",
            crate::types::CommandKind::ReadFile => "Read File",
            crate::types::CommandKind::OpenFolder => "Open Folder",
            crate::types::CommandKind::OpenApp => "Open App",
            crate::types::CommandKind::DownloadRepo => "Download Repo",
            crate::types::CommandKind::DownloadPrivateRepo => "Download Private Repo",
            crate::types::CommandKind::PathTree => "Path Tree",
            _ => "Other",
        };
        let path = cmd.path.as_deref().unwrap_or("-");
        let detail = if let Some(ref c) = cmd.content {
            let short: String = c.chars().take(60).collect();
            format!("{} → {}", path, short)
        } else {
            path.to_string()
        };
        lines.push(format!("{}. {} | {}", i + 1, kind, detail));
    }
    lines.join("\n")
}