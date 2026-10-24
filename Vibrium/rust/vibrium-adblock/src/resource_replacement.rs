#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ResourceReplacement {
    pub url_pattern: String,
    pub replacement: String,
}

impl ResourceReplacement {
    pub fn new(
        url_pattern: impl Into<String>,
        replacement: impl Into<String>,
    ) -> Self {
        Self {
            url_pattern: url_pattern.into(),
            replacement: replacement.into(),
        }
    }
}