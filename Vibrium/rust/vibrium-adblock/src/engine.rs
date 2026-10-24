use adblock::engine::Engine;
use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;

use crate::error::VibriumAdblockError;
use crate::filter_lists::FilterListCollection;

pub struct VibriumAdblock {
    engine: Engine,
}

impl VibriumAdblock {
    pub fn new(rules: &[&str]) -> Self {
        let mut filters = FilterSet::new(true);

        filters.add_filter_list(
            rules.join("\n"),
            ParseOptions::default(),
        );

        Self {
            engine: Engine::new_with_filter_set(filters),
        }
    }

    pub fn from_filter_list(list: &str) -> Self {
        Self {
            engine: Engine::new_with_list_text(list),
        }
    }

    pub fn from_filter_lists(
        lists: &FilterListCollection,
    ) -> Result<Self, VibriumAdblockError> {
        let combined_rules = lists.combined_rules();

        if combined_rules.trim().is_empty() {
            return Err(VibriumAdblockError::FilterListError(
                "No enabled filter rules are available".into(),
            ));
        }

        Ok(Self::from_filter_list(&combined_rules))
    }

    pub fn should_block(
        &self,
        url: &str,
        source_url: &str,
        request_type: &str,
    ) -> Result<bool, VibriumAdblockError> {
        let request = Request::new(
            url,
            source_url,
            request_type,
            "",
        )
        .map_err(|error| {
            VibriumAdblockError::InvalidRequest(error.to_string())
        })?;

        let result = self.engine.check_network_request(&request);

        Ok(result.exception.is_none() && result.filter.is_some())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::filter_lists::FilterList;

    #[test]
    fn blocks_matching_request() {
        let engine = VibriumAdblock::new(&[
            "||ads.example.com^",
        ]);

        assert!(
            engine.should_block(
                "https://ads.example.com/banner.js",
                "https://example.org/",
                "script",
            ).unwrap()
        );
    }

    #[test]
    fn allows_unmatched_request() {
        let engine = VibriumAdblock::new(&[
            "||ads.example.com^",
        ]);

        assert!(
            !engine.should_block(
                "https://cdn.example.org/app.js",
                "https://example.org/",
                "script",
            ).unwrap()
        );
    }

    #[test]
    fn blocks_using_managed_filter_lists() {
        let mut lists = FilterListCollection::new();

        lists.add(
            FilterList::from_text(
                "Test Ads",
                "https://example.org/filters.txt",
                "||ads.example.com^\n",
            ),
        ).unwrap();

        let engine = VibriumAdblock::from_filter_lists(&lists).unwrap();

        assert!(
            engine.should_block(
                "https://ads.example.com/banner.js",
                "https://example.org/",
                "script",
            ).unwrap()
        );
    }

    #[test]
    fn rejects_empty_enabled_filter_lists() {
        let mut lists = FilterListCollection::new();

        lists.add(
            FilterList::from_text(
                "Test Ads",
                "https://example.org/filters.txt",
                "||ads.example.com^\n",
            ),
        ).unwrap();

        lists.set_enabled("Test Ads", false).unwrap();

        assert!(
            VibriumAdblock::from_filter_lists(&lists).is_err()
        );
    }
}