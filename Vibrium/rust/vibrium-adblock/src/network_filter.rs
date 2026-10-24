#[derive(Debug, Clone, PartialEq, Eq)]
pub struct NetworkFilterResult {
    pub blocked: bool,
    pub url: String,
}
