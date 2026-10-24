#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CosmeticFilter {
    pub selector: String,
}

impl CosmeticFilter {
    pub fn new(selector: impl Into<String>) -> Self {
        Self {
            selector: selector.into(),
        }
    }
}