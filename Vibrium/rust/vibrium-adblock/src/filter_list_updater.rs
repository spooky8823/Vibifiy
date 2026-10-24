use std::time::Duration;

use crate::error::VibriumAdblockError;
use crate::filter_lists::FilterList;

pub trait FilterListDownloader {
    fn download(&self, list: &FilterList) -> Result<String, VibriumAdblockError>;
}

const MAX_FILTER_LIST_SIZE: usize = 10 * 1024 * 1024;

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
        validate_list_metadata(list)?;

        let url = ureq::http::Uri::try_from(list.source_url.as_str()).map_err(|error| {
            VibriumAdblockError::FilterListError(format!("Invalid filter list URL: {error}"))
        })?;

        if url.scheme_str() != Some("https") || url.authority().is_none() {
            return Err(VibriumAdblockError::FilterListError(
                "Filter lists must use a valid HTTPS URL".into(),
            ));
        }

        let config = ureq::Agent::config_builder()
            .timeout_global(Some(self.timeout))
            .max_redirects(0)
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
            .and_then(|value| value.parse::<usize>().ok())
            && length > MAX_FILTER_LIST_SIZE
        {
            return Err(VibriumAdblockError::FilterListError(
                "Filter list exceeds the 10 MiB size limit".into(),
            ));
        }

        let bytes = response
            .into_body()
            .with_config()
            .limit(MAX_FILTER_LIST_SIZE as u64)
            .read_to_vec()
            .map_err(|error| {
                VibriumAdblockError::FilterListError(format!(
                    "Failed to read '{}': {error}",
                    list.name
                ))
            })?;

        validate_filter_list_bytes(&bytes)
    }
}

impl FilterListDownloader for FilterListUpdater {
    fn download(&self, list: &FilterList) -> Result<String, VibriumAdblockError> {
        FilterListUpdater::download(self, list)
    }
}

fn validate_list_metadata(list: &FilterList) -> Result<(), VibriumAdblockError> {
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

    Ok(())
}

fn validate_filter_list_bytes(bytes: &[u8]) -> Result<String, VibriumAdblockError> {
    if bytes.len() > MAX_FILTER_LIST_SIZE {
        return Err(VibriumAdblockError::FilterListError(
            "Filter list exceeds the 10 MiB size limit".into(),
        ));
    }

    let text = String::from_utf8(bytes.to_vec()).map_err(|_| {
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn accepts_valid_filter_rules() {
        let result = validate_filter_list_bytes(b"||ads.example^\n");

        assert_eq!(
            result.expect("Valid rules should be accepted"),
            "||ads.example^\n"
        );
    }

    #[test]
    fn rejects_invalid_utf8() {
        assert!(validate_filter_list_bytes(&[0xff, 0xfe]).is_err());
    }

    #[test]
    fn rejects_comment_only_content() {
        assert!(validate_filter_list_bytes(b"! comment\n[Adblock Plus 2.0]\n").is_err());
    }

    #[test]
    fn rejects_oversized_content() {
        let bytes = vec![b'a'; MAX_FILTER_LIST_SIZE + 1];

        assert!(validate_filter_list_bytes(&bytes).is_err());
    }

    #[test]
    fn rejects_empty_metadata() {
        let list = FilterList::new("", "https://example.com/filters.txt");

        assert!(validate_list_metadata(&list).is_err());
    }
}
