use adblock::engine::Engine;
use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;

use crate::error::VibriumAdblockError;
use crate::filter_lists::FilterListCollection;

pub struct VibriumAdblock {
    engine: Engine,
}

/// Cosmetic filtering operations required for a particular page.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CosmeticResources {
    pub hide_selectors: std::collections::HashSet<String>,
    pub procedural_actions: std::collections::HashSet<String>,
    pub exceptions: std::collections::HashSet<String>,
    pub injected_script: String,
    pub generichide: bool,
}

impl VibriumAdblock {
    pub fn new(rules: &[&str]) -> Self {
        let mut filters = FilterSet::new(false);

        filters.add_filter_list(rules.join("\n"), ParseOptions::default());

        Self {
            engine: Engine::new_with_filter_set(filters),
        }
    }

    /// Returns cosmetic filtering resources for a page URL.
    ///
    /// Includes domain-specific selectors, exceptions, procedural actions,
    /// scriptlet code, and whether generic cosmetic filtering is disabled.
    pub fn cosmetic_resources(&self, url: &str) -> Result<CosmeticResources, VibriumAdblockError> {
        let parsed_url = url::Url::parse(url)
            .map_err(|error| VibriumAdblockError::InvalidRequest(error.to_string()))?;

        if !matches!(parsed_url.scheme(), "http" | "https") || parsed_url.host_str().is_none() {
            return Err(VibriumAdblockError::InvalidRequest(
                "Cosmetic filtering requires a valid HTTP or HTTPS URL".into(),
            ));
        }

        let resources = self.engine.url_cosmetic_resources(url);

        Ok(CosmeticResources {
            hide_selectors: resources.hide_selectors,
            procedural_actions: resources.procedural_actions,
            exceptions: resources.exceptions,
            injected_script: resources.injected_script,
            generichide: resources.generichide,
        })
    }

    /// Returns generic CSS selectors relevant to the classes and IDs
    /// found on a page.
    ///
    /// Pass the exceptions returned by `cosmetic_resources` unchanged.
    pub fn hidden_class_id_selectors(
        &self,
        classes: &[String],
        ids: &[String],
        exceptions: &std::collections::HashSet<String>,
    ) -> Vec<String> {
        self.engine
            .hidden_class_id_selectors(classes.iter(), ids.iter(), exceptions)
    }

    pub fn from_filter_list(list: &str) -> Self {
        let mut filters = FilterSet::new(false);

        filters.add_filter_list(list.to_owned(), ParseOptions::default());

        Self {
            engine: Engine::new_with_filter_set(filters),
        }
    }

    pub fn from_filter_lists(lists: &FilterListCollection) -> Result<Self, VibriumAdblockError> {
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
        let request = Request::new(url, source_url, request_type, "")
            .map_err(|error| VibriumAdblockError::InvalidRequest(error.to_string()))?;

        let result = self.engine.check_network_request(&request);

        Ok(result.should_block())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::filter_lists::FilterList;

    const AD_URL: &str = "https://ads.example.com/banner.js";
    const SOURCE_URL: &str = "https://example.org/";

    #[test]
    fn blocks_matching_request() {
        let engine = VibriumAdblock::new(&["||ads.example.com^"]);

        assert!(engine.should_block(AD_URL, SOURCE_URL, "script").unwrap());
    }

    #[test]
    fn allows_unmatched_request() {
        let engine = VibriumAdblock::new(&["||ads.example.com^"]);

        assert!(
            !engine
                .should_block("https://cdn.example.org/app.js", SOURCE_URL, "script",)
                .unwrap()
        );
    }

    #[test]
    fn allows_request_matching_an_exception() {
        let engine = VibriumAdblock::new(&["||ads.example.com^", "@@||ads.example.com^"]);

        assert!(!engine.should_block(AD_URL, SOURCE_URL, "script").unwrap());
    }

    #[test]
    fn important_rule_overrides_an_exception() {
        let engine = VibriumAdblock::new(&[
            "||ads.example.com^",
            "@@||ads.example.com^",
            "||ads.example.com^$important",
        ]);

        assert!(engine.should_block(AD_URL, SOURCE_URL, "script").unwrap());
    }

    #[test]
    fn blocks_using_managed_filter_lists() {
        let mut lists = FilterListCollection::new();

        lists
            .add(FilterList::from_text(
                "Test Ads",
                "https://example.org/filters.txt",
                "||ads.example.com^\n",
            ))
            .unwrap();

        let engine = VibriumAdblock::from_filter_lists(&lists).unwrap();

        assert!(engine.should_block(AD_URL, SOURCE_URL, "script").unwrap());
    }

    #[test]
    fn rejects_empty_enabled_filter_lists() {
        let mut lists = FilterListCollection::new();

        lists
            .add(FilterList::from_text(
                "Test Ads",
                "https://example.org/filters.txt",
                "||ads.example.com^\n",
            ))
            .unwrap();

        lists.set_enabled("Test Ads", false).unwrap();

        assert!(VibriumAdblock::from_filter_lists(&lists).is_err());
    }
}
