use std::time::Duration;

use crate::error::VibriumAdblockError;
use crate::filter_lists::FilterList;

pub const MAX_FILTER_LIST_SIZE: usize = 10 * 1024 * 1024;

/// Result returned by the host browser's network layer.
#[derive(Debug)]
pub struct DownloadResponse {
    pub status: u16,
    pub content_length: Option<usize>,
    pub body: Vec<u8>,
}

/// Supplies downloaded filter-list data to the Rust validator.
///
/// Chromium should implement this boundary using its own network stack.
/// The Rust crate does not perform network requests itself.
pub trait FilterListTransport {
    fn get(&self, url: &str, timeout: Duration) -> Result<DownloadResponse, String>;
}

pub trait FilterListDownloader {
    fn download(&self, list: &FilterList) -> Result<String, VibriumAdblockError>;
}

pub struct FilterListUpdater<T> {
    timeout: Duration,
    transport: T,
}

impl<T> FilterListUpdater<T> {
    pub fn with_transport(timeout: Duration, transport: T) -> Self {
        Self { timeout, transport }
    }

    pub fn download(&self, list: &FilterList) -> Result<String, VibriumAdblockError>
    where
        T: FilterListTransport,
    {
        let response = {
            validate_list_metadata(list)?;

            let url = url::Url::parse(&list.source_url).map_err(|error| {
                VibriumAdblockError::FilterListError(format!("Invalid filter list URL: {error}"))
            })?;

            if url.scheme() != "https" || url.host_str().is_none() {
                return Err(VibriumAdblockError::FilterListError(
                    "Filter lists must use a valid HTTPS URL".into(),
                ));
            }

            self.transport
                .get(&list.source_url, self.timeout)
                .map_err(|error| {
                    VibriumAdblockError::FilterListError(format!(
                        "Failed to download '{}': {error}",
                        list.name
                    ))
                })?
        };

        validate_downloaded_response(
            list,
            response.status,
            response.content_length,
            response.body,
        )
    }
}

impl<T> FilterListDownloader for FilterListUpdater<T>
where
    T: FilterListTransport,
{
    fn download(&self, list: &FilterList) -> Result<String, VibriumAdblockError> {
        FilterListUpdater::download(self, list)
    }
}

/// Validates a filter-list response already downloaded by the host browser.
///
/// This is the entry point for Chromium integration. The caller is responsible
/// for performing the network request, enforcing its timeout, and limiting
/// memory use while reading the response body.
pub fn validate_downloaded_response(
    list: &FilterList,
    status: u16,
    content_length: Option<usize>,
    body: Vec<u8>,
) -> Result<String, VibriumAdblockError> {
    validate_list_metadata(list)?;

    let url = url::Url::parse(&list.source_url).map_err(|error| {
        VibriumAdblockError::FilterListError(format!("Invalid filter list URL: {error}"))
    })?;

    if url.scheme() != "https" || url.host_str().is_none() {
        return Err(VibriumAdblockError::FilterListError(
            "Filter lists must use a valid HTTPS URL".into(),
        ));
    }

    validate_response(DownloadResponse {
        status,
        content_length,
        body,
    })
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

    // Perform full rule validation, rather than accepting arbitrary text
    // merely because it contains a non-comment line.
    let (cleaned_rules, _) = crate::filter_validation::validate_and_clean_filter_list(&text)?;

    if cleaned_rules.trim().is_empty() {
        return Err(VibriumAdblockError::FilterListError(
            "Downloaded file contains no usable filter rules".into(),
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
        let result =
            validate_downloaded_response(&list(), 200, Some(16), b"||ads.example^\n".to_vec());

        assert!(result.is_ok());
    }

    #[test]
    fn rejects_http_urls_before_transport_call() {
        let mut invalid_list = list();
        invalid_list.source_url = "http://example.com/filters.txt".into();

        let transport = fake_response(200, b"||ads.example^\n");

        assert!(
            FilterListUpdater::with_transport(Duration::from_secs(5), transport)
                .download(&invalid_list)
                .is_err()
        );
    }

    #[test]
    fn rejects_malformed_urls() {
        let mut invalid_list = list();
        invalid_list.source_url = "not a URL".into();

        assert!(
            validate_downloaded_response(&invalid_list, 200, None, b"||ads.example^\n".to_vec())
                .is_err()
        );
    }

    #[test]
    fn rejects_redirect_responses() {
        assert!(validate_downloaded_response(&list(), 302, Some(0), vec![]).is_err());
    }

    #[test]
    fn rejects_server_errors() {
        assert!(
            validate_downloaded_response(&list(), 500, Some(21), b"Internal server error".to_vec())
                .is_err()
        );
    }

    #[test]
    fn rejects_invalid_utf8() {
        assert!(validate_downloaded_response(&list(), 200, Some(2), vec![0xff, 0xfe]).is_err());
    }

    #[test]
    fn rejects_content_without_filter_rules() {
        assert!(
            validate_downloaded_response(
                &list(),
                200,
                None,
                b"! comment\n[Adblock Plus 2.0]\n".to_vec()
            )
            .is_err()
        );
    }

    #[test]
    fn rejects_oversized_content() {
        let bytes = vec![b'a'; MAX_FILTER_LIST_SIZE + 1];

        assert!(validate_downloaded_response(&list(), 200, Some(bytes.len()), bytes).is_err());
    }

    #[test]
    fn rejects_empty_list_names() {
        let invalid_list = FilterList::new("", "https://example.com/filters.txt");

        assert!(
            validate_downloaded_response(&invalid_list, 200, None, b"||ads.example^\n".to_vec())
                .is_err()
        );
    }

    #[test]
    fn rejects_empty_source_urls() {
        let invalid_list = FilterList::new("Test filters", "");

        assert!(
            validate_downloaded_response(&invalid_list, 200, None, b"||ads.example^\n".to_vec())
                .is_err()
        );
    }

    #[test]
    fn reports_transport_errors() {
        let transport = FakeTransport {
            response: Err("simulated connection failure".into()),
        };

        let error = FilterListUpdater::with_transport(Duration::from_secs(5), transport)
            .download(&list())
            .unwrap_err();

        assert!(error.to_string().contains("simulated connection failure"));
    }
}
