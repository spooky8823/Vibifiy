pub mod openai;
pub mod gemini;
pub mod api_chat;

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ApiMessage {
    pub role: String,
    pub content: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ApiResponse {
    pub content: String,
    pub model: String,
    pub tokens_used: u32,
}

pub trait AiApi {
    fn send_message(&self, messages: &[ApiMessage], model: &str) -> Result<ApiResponse, String>;
    fn list_models(&self) -> Result<Vec<String>, String>;
}