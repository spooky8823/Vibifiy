use crate::api::{AiApi, ApiMessage, ApiResponse};
use crate::api::openai::OpenAiApi;
use crate::api::gemini::GeminiApi;
use crate::vibi_lang;
use regex::Regex;
use std::rc::Rc;
use std::cell::RefCell;

pub struct ApiChat {
    openai: Option<OpenAiApi>,
    gemini: Option<GeminiApi>,
    pub messages: Rc<RefCell<Vec<ApiMessage>>>,
    model: Rc<RefCell<String>>,
}

impl ApiChat {
    pub fn new(openai_key: Option<String>, gemini_key: Option<String>) -> Self {
        ApiChat {
            openai: openai_key.map(OpenAiApi::new),
            gemini: gemini_key.map(GeminiApi::new),
            messages: Rc::new(RefCell::new(Vec::new())),
            model: Rc::new(RefCell::new(String::new())),
        }
    }

    pub fn set_model(&self, model: &str) {
        *self.model.borrow_mut() = model.to_string();
    }

    pub fn send_message(&self, text: &str) -> Result<ApiResponse, String> {
        self.messages.borrow_mut().push(ApiMessage {
            role: "user".to_string(),
            content: text.to_string(),
        });

        let model = self.model.borrow().clone();
        let messages = self.messages.borrow().clone();

        let response = if model.starts_with("gpt") || model == "chatgpt" {
            self.openai
                .as_ref()
                .ok_or("OpenAI API key not configured")?
                .send_message(&messages, &model)
        } else if model.starts_with("gemini") || model == "gemini" {
            self.gemini
                .as_ref()
                .ok_or("Gemini API key not configured")?
                .send_message(&messages, &model)
        } else {
            // Fallback to OpenAI
            self.openai
                .as_ref()
                .ok_or("No API key configured")?
                .send_message(&messages, "gpt-4o")
        }?;

        self.messages.borrow_mut().push(ApiMessage {
            role: "assistant".to_string(),
            content: response.content.clone(),
        });

        Ok(response)
    }

    /// Extract VibiClaw `.v` blocks from response text
    pub fn extract_vibi_blocks(text: &str) -> Vec<String> {
        let re = Regex::new(r"```vibi\s*([\s\S]*?)```").unwrap();
        let mut blocks = Vec::new();
        for cap in re.captures_iter(text) {
            blocks.push(cap[1].trim().to_string());
        }
        // Also detect raw <vibi.claw> blocks
        let re2 = Regex::new(r"(<vibi\.claw>[\s\S]*?</vibi\.claw>)").unwrap();
        for cap in re2.captures_iter(text) {
            blocks.push(cap[1].trim().to_string());
        }
        blocks
    }

    /// Compile and execute VibiClaw blocks from response
    pub fn execute_vibi_blocks(text: &str) -> Vec<String> {
        let blocks = Self::extract_vibi_blocks(text);
        let mut results = Vec::new();

        for block in &blocks {
            match vibi_lang::compile(block) {
                Ok(commands) => {
                    let sandbox_path = dirs::config_dir()
                        .unwrap_or_else(|| std::path::PathBuf::from("."))
                        .join("vibi-ai")
                        .join("sandbox");

                    if let Ok(executor) = crate::executor::Executor::new(
                        sandbox_path.to_str().unwrap(),
                        true,
                    ) {
                        for result in crate::vibi_lang::runtime::execute(commands, &executor, true) {
                            results.push(result);
                        }
                    }
                }
                Err(errors) => {
                    results.push(format!("Compilation failed: {:?}", errors));
                }
            }
        }

        results
    }

    pub fn clear_history(&self) {
        self.messages.borrow_mut().clear();
    }
}