use super::{AiApi, ApiMessage, ApiResponse};
use serde::{Deserialize, Serialize};

pub struct OpenAiApi {
    api_key: String,
    client: reqwest::blocking::Client,
}

#[derive(Serialize)]
struct OpenAiRequest {
    model: String,
    messages: Vec<OpenAiMessage>,
}

#[derive(Serialize)]
struct OpenAiMessage {
    role: String,
    content: String,
}

#[derive(Deserialize)]
struct OpenAiResponse {
    choices: Vec<OpenAiChoice>,
    usage: OpenAiUsage,
}

#[derive(Deserialize)]
struct OpenAiChoice {
    message: OpenAiMessageResponse,
}

#[derive(Deserialize)]
struct OpenAiMessageResponse {
    content: String,
}

#[derive(Deserialize)]
struct OpenAiUsage {
    total_tokens: u32,
}

impl OpenAiApi {
    pub fn new(api_key: String) -> Self {
        OpenAiApi {
            api_key,
            client: reqwest::blocking::Client::new(),
        }
    }
}

impl AiApi for OpenAiApi {
    fn send_message(&self, messages: &[ApiMessage], model: &str) -> Result<ApiResponse, String> {
        let openai_messages: Vec<OpenAiMessage> = messages
            .iter()
            .map(|m| OpenAiMessage {
                role: m.role.clone(),
                content: m.content.clone(),
            })
            .collect();

        let request = OpenAiRequest {
            model: if model.is_empty() { "gpt-4o".into() } else { model.to_string() },
            messages: openai_messages,
        };

        let response = self.client
            .post("https://api.openai.com/v1/chat/completions")
            .header("Authorization", format!("Bearer {}", self.api_key))
            .header("Content-Type", "application/json")
            .json(&request)
            .send()
            .map_err(|e| format!("Request failed: {}", e))?;

        let body: OpenAiResponse = response
            .json()
            .map_err(|e| format!("Parse failed: {}", e))?;

        let content = body.choices
            .first()
            .map(|c| c.message.content.clone())
            .unwrap_or_default();

        Ok(ApiResponse {
            content,
            model: model.to_string(),
            tokens_used: body.usage.total_tokens,
        })
    }

    fn list_models(&self) -> Result<Vec<String>, String> {
        Ok(vec![
            "gpt-4o".into(),
            "gpt-4".into(),
            "gpt-3.5-turbo".into(),
        ])
    }
}