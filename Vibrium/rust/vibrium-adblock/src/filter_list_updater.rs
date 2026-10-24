use std::time::Duration;

use crate::error::VibriumAdblockError;
use crate::filter_lists::FilterList;

const MAX_FILTER_LIST_SIZE: usize = 10 * 1024 * 1024;

pub trait FilterListDownloader {
    fn download(&self, list: &FilterList) -> Result<String, VibriumAdblockError>;
}

#[derive(Debug)]
struct DownloadResponse {
    status: u16,
    content_length: Option<usize>,
    body: Vec<u8>,
}

trait FilterListTransport {
    fn get(&self, url: &str, timeout: Duration) -> Result<DownloadResponse, String>;
}

struct UreqTransport;

impl FilterListTransport for UreqTransport {
    fn get(&self, url: &str, timeout: Duration) -> Result<DownloadResponse, String> {
        let config = ureq::Agent::config_builder()
            .timeout_global(Some(timeout))
            .max_redirects(0)
            .build();

        let agent: ureq::Agent = config.into();

        let response = agent.get(url).call().map_err(|error| error.to_string())?;

        let status = response.status().as_u16();

        let content_length = response
            .headers()
            .get("content-length")
            .and_then(|value| value.to_str().ok())
            .and_then(|value| value.parse::<usize>().ok());

        if content_length.is_some_and(|length| length > MAX_FILTER_LIST_SIZE) {
            return Err("Filter list exceeds the 10 MiB size limit".into());
        }

        let body = response
            .into_body()
            .with_config()
            .limit(MAX_FILTER_LIST_SIZE as u64)
            .read_to_vec()
            .map_err(|error| error.to_string())?;

        Ok(DownloadResponse {
            status,
            content_length,
            body,
        })
    }
}

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
        download_with_transport(list, self.timeout, &UreqTransport)
    }
}

impl FilterListDownloader for FilterListUpdater {
    fn download(&self, list: &FilterList) -> Result<String, VibriumAdblockError> {
        FilterListUpdater::download(self, list)
    }
}

fn download_with_transport<T: FilterListTransport>(
    list: &FilterList,
    timeout: Duration,
    transport: &T,
) -> Result<String, VibriumAdblockError> {
    validate_list_metadata(list)?;

    let url = ureq::http::Uri::try_from(list.source_url.as_str()).map_err(|error| {
        VibriumAdblockError::FilterListError(format!("Invalid filter list URL: {error}"))
    })?;

    if url.scheme_str() != Some("https") || url.authority().is_none() {
        return Err(VibriumAdblockError::FilterListError(
            "Filter lists must use a valid HTTPS URL".into(),
        ));
    }

    let response = transport.get(&list.source_url, timeout).map_err(|error| {
        VibriumAdblockError::FilterListError(format!("Failed to download '{}': {error}", list.name))
    })?;

    validate_response(response)
}

fn validate_response(response: DownloadResponse) -> Result<String, VibriumAdblockError> {
    if !(200..300).contains(&response.status) {
        return Err(VibriumAdblockError::FilterListError(format!(
            "Filter server returned HTTP status {}",
            response.status
        )));
    }

    if response
        .content_length
        .is_some_and(|length| length > MAX_FILTER_LIST_SIZE)
        || response.body.len() > MAX_FILTER_LIST_SIZE
    {
        return Err(VibriumAdblockError::FilterListError(
            "Filter list exceeds the 10 MiB size limit".into(),
        ));
    }

    let text = String::from_utf8(response.body).map_err(|_| {
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

#[cfg(test)]
mod tests {
    use super::*;

    struct FakeTransport {
        response: Result<DownloadResponse, String>,
    }

    impl FilterListTransport for FakeTransport {
        fn get(&self, _url: &str, _timeout: Duration) -> Result<DownloadResponse, String> {
            match &self.response {
                Ok(response) => Ok(DownloadResponse {
                    status: response.status,
                    content_length: response.content_length,
                    body: response.body.clone(),
                }),
                Err(error) => Err(error.clone()),
            }
        }
    }

    fn list() -> FilterList {
        FilterList::new("Test filters", "https://example.com/filters.txt")
    }

    fn fake_response(status: u16, body: &[u8]) -> FakeTransport {
        FakeTransport {
            response: Ok(DownloadResponse {
                status,
                content_length: Some(body.len()),
                body: body.to_vec(),
            }),
        }
    }

    #[test]
    fn accepts_valid_filter_rules() {
        let transport = fake_response(200, b"||ads.example^\n");
        let result = download_with_transport(&list(), Duration::from_secs(5), &transport);

        assert_eq!(
            result.expect("Valid filter rules should be accepted"),
            "||ads.example^\n"
        );
    }

    #[test]
    fn rejects_http_urls_before_transport_call() {
        let mut invalid_list = list();
        invalid_list.source_url = "http://example.com/filters.txt".into();

        let transport = fake_response(200, b"||ads.example^\n");
        let result = download_with_transport(&invalid_list, Duration::from_secs(5), &transport);

        assert!(result.is_err());
    }

    #[test]
    fn rejects_malformed_urls() {
        let mut invalid_list = list();
        invalid_list.source_url = "not a URL".into();

        let transport = fake_response(200, b"||ads.example^\n");
        let result = download_with_transport(&invalid_list, Duration::from_secs(5), &transport);

        assert!(result.is_err());
    }

    #[test]
    fn rejects_redirect_responses() {
        let transport = fake_response(302, b"");
        let result = download_with_transport(&list(), Duration::from_secs(5), &transport);

        assert!(result.is_err());
    }

    #[test]
    fn rejects_server_errors() {
        let transport = fake_response(500, b"Internal server error");
        let result = download_with_transport(&list(), Duration::from_secs(5), &transport);

        assert!(result.is_err());
    }

    #[test]
    fn rejects_invalid_utf8() {
        let transport = fake_response(200, &[0xff, 0xfe]);
        let result = download_with_transport(&list(), Duration::from_secs(5), &transport);

        assert!(result.is_err());
    }

    #[test]
    fn rejects_content_without_filter_rules() {
        let transport = fake_response(200, b"! comment\n[Adblock Plus 2.0]\n");
        let result = download_with_transport(&list(), Duration::from_secs(5), &transport);

        assert!(result.is_err());
    }

    #[test]
    fn rejects_oversized_content() {
        let bytes = vec![b'a'; MAX_FILTER_LIST_SIZE + 1];
        let response = DownloadResponse {
            status: 200,
            content_length: Some(bytes.len()),
            body: bytes,
        };
        let transport = FakeTransport {
            response: Ok(response),
        };

        let result = download_with_transport(&list(), Duration::from_secs(5), &transport);

        assert!(result.is_err());
    }

    #[test]
    fn rejects_empty_list_names() {
        let invalid_list = FilterList::new("", "https://example.com/filters.txt");
        let transport = fake_response(200, b"||ads.example^\n");
        let result = download_with_transport(&invalid_list, Duration::from_secs(5), &transport);

        assert!(result.is_err());
    }

    #[test]
    fn rejects_empty_source_urls() {
        let invalid_list = FilterList::new("Test filters", "");
        let transport = fake_response(200, b"||ads.example^\n");
        let result = download_with_transport(&invalid_list, Duration::from_secs(5), &transport);

        assert!(result.is_err());
    }

    #[test]
    fn reports_transport_errors() {
        let transport = FakeTransport {
            response: Err("simulated connection failure".into()),
        };

        let result = download_with_transport(&list(), Duration::from_secs(5), &transport);

        assert!(result.is_err());
        assert!(
            result
                .expect_err("Transport errors should be returned")
                .to_string()
                .contains("simulated connection failure")
        );
    }
}
