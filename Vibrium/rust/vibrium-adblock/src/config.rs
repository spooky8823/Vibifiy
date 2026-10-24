#[derive(Debug, Clone)]
pub struct AdblockConfig {
    pub enabled: bool,
    pub cosmetic_filtering_enabled: bool,
    pub network_filtering_enabled: bool,
}

impl Default for AdblockConfig {
    fn default() -> Self {
        Self {
            enabled: true,
            cosmetic_filtering_enabled: true,
            network_filtering_enabled: true,
        }
    }
}