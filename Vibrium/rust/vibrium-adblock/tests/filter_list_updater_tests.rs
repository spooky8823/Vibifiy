use std::time::Duration;

use vibrium_adblock::error::VibriumAdblockError;
use vibrium_adblock::filter_list_updater::{
    DownloadResponse, FilterListTransport, FilterListUpdater, MAX_FILTER_LIST_SIZE,
};
use vibrium_adblock::filter_lists::FilterList;

struct FakeTransport {
    response: Result<DownloadResponse, String>,
}

impl FakeTransport {
    fn success(body: &[u8]) -> Self {
        Self {
            response: Ok(DownloadResponse {
                status: 200,
                content_length: Some(body.len()),
                body: body.to_vec(),
            }),
        }
    }

    fn status(status: u16, body: &[u8]) -> Self {
        Self {
            response: Ok(DownloadResponse {
                status,
                content_length: Some(body.len()),
                body: body.to_vec(),
            }),
        }
    }
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

fn make_updater(transport: FakeTransport) -> FilterListUpdater<FakeTransport> {
    FilterListUpdater::with_transport(Duration::from_secs(5), transport)
}

fn test_list() -> FilterList {
    FilterList::new("Test filters", "https://example.com/filters.txt")
}

#[test]
fn downloads_valid_filter_list() {
    let updater = make_updater(FakeTransport::success(b"||ads.example^\n"));

    let result = updater.download(&test_list());

    assert_eq!(result.unwrap(), "||ads.example^\n");
}

#[test]
fn rejects_http_status_errors() {
    let updater = make_updater(FakeTransport::status(500, b"Server error"));

    let result = updater.download(&test_list());

    assert!(matches!(
        result,
        Err(VibriumAdblockError::FilterListError(_))
    ));
}

#[test]
fn rejects_non_https_urls() {
    let updater = make_updater(FakeTransport::success(b"||ads.example^\n"));
    let mut list = test_list();
    list.source_url = "http://example.com/filters.txt".into();

    assert!(updater.download(&list).is_err());
}

#[test]
fn rejects_oversized_filter_lists() {
    let body = vec![b'a'; MAX_FILTER_LIST_SIZE + 1];
    let updater = make_updater(FakeTransport {
        response: Ok(DownloadResponse {
            status: 200,
            content_length: Some(body.len()),
            body,
        }),
    });

    assert!(updater.download(&test_list()).is_err());
}
