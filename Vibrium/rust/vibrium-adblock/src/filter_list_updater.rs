use std::time::Duration;

use crate::error::VibriumAdblockError;
use crate::filter_lists::FilterList;

const MAX_FILTER_LIST_SIZE: u64 = 10 * 1024 * 1024;

pub struct FilterListUpdater {
    timeout: Duration,
}

impl Default for FilterListUpdater {
    fn default() -> Self {
        Self {
            timeout: Duration::from_secs(30),
        }
    }
}

impl FilterListUpdater {
    pub fn new(timeout: Duration) -> Self {
        Self { timeout }
    }

    pub fn download(&self, list: &FilterList) -> Result<String, VibriumAdblockError> {
        if list.name.trim().is_empty() {
            return Err(VibriumAdblockError::InvalidConfiguration(
                "Filter list name cannot be empty".into(),
            ));
        }

        if list.source_url.trim().is_empty() {
            return Err(VibriumAdblockError::InvalidConfiguration(
                "Filter list source cannot be empty".into(),
            ));
        }

        if !list.source_url.starts_with("https://") {
            return Err(VibriumAdblockError::FilterListError(
                "Filter lists must use HTTPS".into(),
            ));
        }

        let config = ureq::Agent::config_builder()
            .timeout_global(Some(self.timeout))
            .build();

        let agent: ureq::Agent = config.into();

        let response = agent.get(&list.source_url).call().map_err(|error| {
            VibriumAdblockError::FilterListError(format!(
                "Failed to download '{}': {error}",
                list.name
            ))
        })?;

        if let Some(length) = response
            .headers()
            .get("content-length")
            .and_then(|value| value.to_str().ok())
            .and_then(|value| value.parse::<u64>().ok())
            && length > MAX_FILTER_LIST_SIZE
        {
            return Err(VibriumAdblockError::FilterListError(
                "Filter list exceeds the 10 MiB size limit".into(),
            ));
        }

        let bytes = response
            .into_body()
            .with_config()
            .limit(MAX_FILTER_LIST_SIZE)
            .read_to_vec()
            .map_err(|error| {
                VibriumAdblockError::FilterListError(format!(
                    "Failed to read '{}': {error}",
                    list.name
                ))
            })?;

        let text = String::from_utf8(bytes).map_err(|_| {
            VibriumAdblockError::FilterListError("Filter list is not valid UTF-8".into())
        })?;

        if !text.lines().any(|line| {
            let line = line.trim();
            !line.is_empty() && !line.starts_with('!') && !line.starts_with('[')
        }) {
            return Err(VibriumAdblockError::FilterListError(
                "Downloaded file contains no filter rules".into(),
            ));
        }

        Ok(text)
    }
}
