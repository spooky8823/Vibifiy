use regex::Regex;
use crate::types::{Command, CommandKind};

pub struct Analyzer;

impl Analyzer {
    pub fn analyze(response: &str) -> Vec<Command> {
        let mut commands = Vec::new();

        commands.extend(Self::extract_file_operations(response));
        commands.extend(Self::extract_shell_commands(response));
        commands.extend(Self::extract_installs(response));
        commands.extend(Self::extract_unformatted_code(response));

        // Deduplicate — unformatted extractor may overlap with formatted
        commands.dedup_by(|a, b| a.path == b.path && a.path.is_some());

        commands
    }

    fn extract_file_operations(text: &str) -> Vec<Command> {
        let mut commands = Vec::new();

        let block_re = Regex::new(
            r"(?m)```[\w]*\s+([\w./\-]+\.\w+)\n([\s\S]*?)```"
        ).unwrap();

        for cap in block_re.captures_iter(text) {
            let path = cap[1].to_string();
            let content = cap[2].to_string();

            // Get context BEFORE this block to determine intent
            let block_start = cap.get(0).unwrap().start();
            let pre_context = Self::get_pre_context(text, block_start, 120);
            let post_context = Self::get_post_context(text, cap.get(0).unwrap().end(), 60);

            let kind = Self::classify_intent(&path, &pre_context, &post_context);

            commands.push(Command::new(kind, Some(path), Some(content.trim().to_string())));
        }

        commands
    }

    fn classify_intent(path: &str, pre_context: &str, post_context: &str) -> CommandKind {
        let pre = pre_context.to_lowercase();
        let post = post_context.to_lowercase();

        // Delete signals — must be about THIS file specifically
        let delete_signals = ["delete", "remove this file", "clean up", "get rid of"];
        let edit_signals = ["update", "edit", "modify", "change", "replace", "fix", "adjust"];

        // Check if delete signal is close AND references this specific file or "it"/"this"
        let is_delete = delete_signals.iter().any(|s| pre.contains(s) || post.contains(s))
            && (pre.contains(path) || pre.contains("this file") || pre.contains("it"));

        let is_edit = edit_signals.iter().any(|s| pre.contains(s));

        if is_delete {
            CommandKind::DeleteFile
        } else if is_edit {
            CommandKind::EditFile
        } else {
            CommandKind::CreateFile
        }
    }

    fn extract_shell_commands(text: &str) -> Vec<Command> {
        let mut commands = Vec::new();

        let shell_re = Regex::new(r"```(?:bash|sh|shell|zsh)\n([\s\S]*?)```").unwrap();

        for cap in shell_re.captures_iter(text) {
            let raw = cap[1].trim().to_string();
            for line in raw.lines() {
                let line = line.trim();
                if line.is_empty() || line.starts_with('#') {
                    continue;
                }
                commands.push(Command::new(
                    CommandKind::RunShell,
                    None,
                    Some(line.to_string()),
                ));
            }
        }

        commands
    }

    fn extract_installs(text: &str) -> Vec<Command> {
        let mut commands = Vec::new();

        let install_re = Regex::new(
            r"(?m)^\s*(npm install|yarn add|pip install|cargo add|apt install|apt-get install)\s+([\w@/.\-\s]+)"
        ).unwrap();

        for cap in install_re.captures_iter(text) {
            let full_cmd = format!("{} {}", cap[1].trim(), cap[2].trim());
            commands.push(Command::new(
                CommandKind::InstallDep,
                None,
                Some(full_cmd),
            ));
        }

        commands
    }

    // Catches unformatted code blocks — lines that look like code but have no backticks
    fn extract_unformatted_code(text: &str) -> Vec<Command> {
        let mut commands = Vec::new();

        // Look for "create X with:" or "in X put:" or "X should contain:" patterns
        let hint_re = Regex::new(
            r"(?i)(?:create|make|write|add|put in|in)\s+([\w./\-]+\.\w+)\s+(?:with|containing|like|as follows)?:?\s*\n((?:[^\n`]+\n){2,})"
        ).unwrap();

        for cap in hint_re.captures_iter(text) {
            let path = cap[1].to_string();
            let content = cap[2].trim().to_string();

            // Only treat as code if it has code-like characters
            if Self::looks_like_code(&content) {
                commands.push(Command::new(
                    CommandKind::CreateFile,
                    Some(path),
                    Some(content),
                ));
            }
        }

        // Also catch inline shell hints: "run X" or "execute X"
        let run_re = Regex::new(
            r"(?i)(?:^|\.\s+|\n)(?:run|execute|type|do)\s+[`']?([\w][\w\s./\-]{2,40})[`']?"
        ).unwrap();

        for cap in run_re.captures_iter(text) {
            let cmd = cap[1].trim().to_string();
            // Filter out obvious non-commands
            if !cmd.contains(' ') && cmd.len() < 4 { continue; }
            if cmd.split_whitespace().count() > 6 { continue; }
            commands.push(Command::new(
                CommandKind::RunShell,
                None,
                Some(cmd),
            ));
        }

        commands
    }

    fn looks_like_code(text: &str) -> bool {
        let code_chars = ['{', '}', '(', ')', ';', '=', ':', '<', '>'];
        let hits = code_chars.iter().filter(|&&c| text.contains(c)).count();
        hits >= 2
    }

    fn get_pre_context(text: &str, pos: usize, radius: usize) -> String {
        let start = pos.saturating_sub(radius);
        text[start..pos].to_string()
    }

    fn get_post_context(text: &str, pos: usize, radius: usize) -> String {
        let end = (pos + radius).min(text.len());
        text[pos..end].to_string()
    }
}