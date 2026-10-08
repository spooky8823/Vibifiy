use super::{AiApi, ApiMessage, ApiResponse};
use serde::{Deserialize, Serialize};

pub struct GeminiApi {
    api_key: String,
    client: reqwest::blocking::Client,
}

#[derive(Serialize)]
struct GeminiRequest {
    contents: Vec<GeminiContent>,
}

#[derive(Serialize)]
struct GeminiContent {
    role: String,
    parts: Vec<GeminiPart>,
}

#[derive(Serialize)]
#[derive(Deserialize)]
struct GeminiPart {
    text: String,
}

#[derive(Deserialize)]
struct GeminiResponse {
    candidates: Vec<GeminiCandidate>,
    model_version: String,
    usage_metadata: GeminiUsage,
}

#[derive(Deserialize)]
struct GeminiCandidate {
    content: GeminiContentResponse,
}

#[derive(Deserialize)]
struct GeminiContentResponse {
    parts: Vec<GeminiPart>,
}

#[derive(Deserialize)]
struct GeminiUsage {
    total_token_count: u32,
}

impl GeminiApi {
    pub fn new(api_key: String) -> Self {
        GeminiApi {
            api_key,
            client: reqwest::blocking::Client::new(),
        }
    }
}

impl AiApi for GeminiApi {
    fn send_message(&self, messages: &[ApiMessage], model: &str) -> Result<ApiResponse, String> {
        let contents: Vec<GeminiContent> = messages
            .iter()
            .map(|m| GeminiContent {
                role: if m.role == "assistant" { "model".into() } else { "user".into() },
                parts: vec![GeminiPart { text: m.content.clone() }],
            })
            .collect();

        let request = GeminiRequest { contents };
        let model = if model.is_empty() { "gemini-2.5-pro" } else { model };

        let url = format!(
            "https://generativelanguage.googleapis.com/v1beta/models/{}:generateContent?key={}",
            model, self.api_key
        );

        let response = self.client
            .post(&url)
            .header("Content-Type", "application/json")
            .json(&request)
            .send()
            .map_err(|e| format!("Request failed: {}", e))?;

        let body: GeminiResponse = response
            .json()
            .map_err(|e| format!("Parse failed: {}", e))?;

        let content = body.candidates
            .first()
            .and_then(|c| c.content.parts.first())
            .map(|p| p.text.clone())
            .unwrap_or_default();

        Ok(ApiResponse {
            content,
            model: body.model_version,
            tokens_used: body.usage_metadata.total_token_count,
        })
    }

    fn list_models(&self) -> Result<Vec<String>, String> {
        Ok(vec![
            "gemini-2.5-pro".into(),
            "gemini-2.5-flash".into(),
            "gemini-1.5-pro".into(),
        ])
    }
}