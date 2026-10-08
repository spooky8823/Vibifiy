use std::fs;
use std::path::PathBuf;
use chrono::Local;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
struct SessionData {
    session_id: String,
    entries: Vec<LogEntry>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LogEntry {
    pub timestamp: String,
    pub level: LogLevel,
    pub category: String,
    pub message: String,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub enum LogLevel {
    Info,
    Success,
    Warning,
    Error,
    Debug,
}

pub struct Logger {
    pub entries: Vec<LogEntry>,
    session_id: String,
    listeners: Vec<Box<dyn Fn(&LogEntry) + 'static>>,
}

impl Logger {
    pub fn new() -> Self {
        let session_id = format!("session_{}", Local::now().format("%Y%m%d_%H%M%S"));
        Logger {
            entries: Vec::new(),
            session_id,
            listeners: Vec::new(),
        }
    }

    pub fn log(&mut self, level: LogLevel, category: &str, message: &str) {
        let entry = LogEntry {
            timestamp: Local::now().format("%Y-%m-%d %H:%M:%S%.3f").to_string(),
            level,
            category: category.to_string(),
            message: message.to_string(),
        };
        for listener in &self.listeners {
            listener(&entry);
        }
        self.entries.push(entry);
        if self.entries.len() % 5 == 0 {
            self.save_to_disk();
        }
    }

    pub fn on_log<F: Fn(&LogEntry) + 'static>(&mut self, listener: F) {
        self.listeners.push(Box::new(listener));
    }

    pub fn save_to_disk(&self) {
        let path = logs_path(&self.session_id);
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).ok();
        }
        let data = SessionData {
            session_id: self.session_id.clone(),
            entries: self.entries.clone(),
        };
        if let Ok(json) = serde_json::to_string(&data) {
            if let Ok(encrypted) = crate::crypto::encrypt_session_data(&json) {
                fs::write(&path, encrypted).ok();
            }
        }
    }

    pub fn load_from_disk(path: &std::path::Path) -> Result<Vec<LogEntry>, String> {
        let encrypted = fs::read_to_string(path).map_err(|e| format!("Read error: {:?}", e))?;
        let json = crate::crypto::decrypt_session_data(&encrypted)?;
        let data: SessionData = serde_json::from_str(&json).map_err(|e| format!("JSON error: {:?}", e))?;
        Ok(data.entries)
    }
}

fn logs_path(session_id: &str) -> PathBuf {
    let mut path = dirs::config_dir().unwrap_or_else(|| PathBuf::from("."));
    path.push("vibi-ai");
    path.push("logs");
    path.push(format!("{}.sessiondata", session_id));
    path
}