use adblock::engine::Engine;
use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;

use crate::cosmetic_filter::is_valid_css_selector;
use crate::error::VibriumAdblockError;
use crate::filter_lists::{FilterList, FilterListCollection};
use crate::filter_storage::FilterStorage;
use crate::filter_validation::validate_and_clean_filter_list;

pub struct VibriumAdblock {
    engine: Engine,
}

/// Upper bounds for data that crosses the C ABI or is handed to a page.
/// They keep a hostile page or a huge filter list from making the browser
/// allocate without limit.
pub mod limits {
    /// Total size of generated CSS, in bytes.
    pub const MAX_CSS_BYTES: usize = 1024 * 1024;
    /// Number of procedural/action filters sent to one page.
    pub const MAX_PROCEDURAL_FILTERS: usize = 5_000;
    /// Size of one JSON-encoded procedural filter, in bytes.
    pub const MAX_PROCEDURAL_FILTER_BYTES: usize = 4 * 1024;
    /// Size of the scriptlet code injected into one page, in bytes.
    pub const MAX_SCRIPT_BYTES: usize = 1024 * 1024;
    /// Class names or ids a page may report in one request.
    pub const MAX_NAMES_PER_REQUEST: usize = 10_000;
    /// Length of one reported class name or id, in bytes.
    pub const MAX_NAME_BYTES: usize = 256;
    /// Size of a resources.json file, in bytes.
    pub const MAX_RESOURCES_BYTES: usize = 8 * 1024 * 1024;
}

/// Page-specific cosmetic data in the form the in-page content script needs.
///
/// `css` hides plain-selector matches. `procedural` holds the filters that
/// need JavaScript (`:has-text()`, `:upward()`, `:remove()`, `:style()`, ...)
/// as the JSON strings produced by adblock-rust. `generichide` tells the page
/// not to ask for generic class/id selectors.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct CosmeticPayload {
    pub css: String,
    pub procedural: Vec<String>,
    pub generichide: bool,
}

impl CosmeticPayload {
    /// True when the page has nothing to do: no CSS, no procedural filters,
    /// and generic selectors are disabled so there is nothing to look up.
    pub fn is_noop(&self) -> bool {
        self.css.is_empty() && self.procedural.is_empty() && self.generichide
    }

    /// Serializes to the JSON object read by `adblock/cosmetic_content.js`.
    pub fn to_json(&self) -> String {
        serde_json::json!({
            "css": self.css,
            "procedural": self.procedural,
            "generichide": self.generichide,
        })
        .to_string()
    }
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

        let resources = self.engine.url_cosmetic_resources(parsed_url.as_str());

        let hide_selectors = resources
            .hide_selectors
            .into_iter()
            .filter(|selector| is_valid_css_selector(selector))
            .collect();

        Ok(CosmeticResources {
            hide_selectors,
            procedural_actions: resources.procedural_actions,
            exceptions: resources.exceptions,
            injected_script: resources.injected_script,
            generichide: resources.generichide,
        })
    }

    /// Returns validated generic CSS selectors relevant to the classes and IDs
    /// found on a page.
    ///
    /// If `$generichide` disables generic cosmetic filtering for this page,
    /// no generic selectors are returned.
    pub fn hidden_class_id_selectors(
        &self,
        classes: &[String],
        ids: &[String],
        resources: &CosmeticResources,
    ) -> Vec<String> {
        if resources.generichide {
            return Vec::new();
        }

        self.engine
            .hidden_class_id_selectors(classes.iter(), ids.iter(), &resources.exceptions)
            .into_iter()
            .filter(|selector| is_valid_css_selector(selector))
            .collect()
    }

    /// Installs scriptlet / redirect resources from a `resources.json` document
    /// (a JSON array of adblock-rust `Resource` objects). Returns how many
    /// resources were installed. Must be called before the engine is shared.
    ///
    /// Scriptlets only run when a filter list references them by name, and
    /// only resources that are installed here can be referenced. Lists are
    /// parsed without any trust permissions, so scriptlets that require trust
    /// stay unavailable to them.
    pub fn use_resources_json(&mut self, json: &str) -> Result<usize, VibriumAdblockError> {
        if json.len() > limits::MAX_RESOURCES_BYTES {
            return Err(VibriumAdblockError::InvalidConfiguration(
                "resources.json is too large".into(),
            ));
        }

        let resources: Vec<adblock::resources::Resource> = serde_json::from_str(json)
            .map_err(|error| {
                VibriumAdblockError::InvalidConfiguration(format!("Invalid resources.json: {error}"))
            })?;

        let count = resources.len();
        self.engine.use_resources(resources);
        Ok(count)
    }

    /// Everything the content script needs for the page at `url`.
    ///
    /// Plain `hide` selectors become CSS. Anything that needs page logic is
    /// passed through as procedural filters. Malformed entries are dropped
    /// rather than failing the whole page.
    pub fn cosmetic_payload(&self, url: &str) -> Result<CosmeticPayload, VibriumAdblockError> {
        let resources = self.cosmetic_resources(url)?;

        let mut selectors: Vec<&String> = resources.hide_selectors.iter().collect();
        selectors.sort();
        let css = hide_css(selectors.into_iter());

        let mut procedural: Vec<String> = resources
            .procedural_actions
            .iter()
            .filter(|filter| filter.len() <= limits::MAX_PROCEDURAL_FILTER_BYTES)
            .filter(|filter| serde_json::from_str::<serde_json::Value>(filter).is_ok())
            .cloned()
            .collect();
        procedural.sort();
        procedural.truncate(limits::MAX_PROCEDURAL_FILTERS);

        Ok(CosmeticPayload {
            css,
            procedural,
            generichide: resources.generichide,
        })
    }

    /// Scriptlet code that filter lists inject into the page at `url`
    /// (empty when no scriptlet rule matches or no resources are installed).
    pub fn injected_script(&self, url: &str) -> Result<String, VibriumAdblockError> {
        let resources = self.cosmetic_resources(url)?;

        if resources.injected_script.len() > limits::MAX_SCRIPT_BYTES {
            return Ok(String::new());
        }

        Ok(resources.injected_script)
    }

    /// CSS hiding the generic (`##.class`, `###id`) matches for the classes
    /// and ids a page reports. Honors `$generichide` and generic-hide
    /// exceptions for `url`. Input beyond the [`limits`] is ignored.
    pub fn generic_css(
        &self,
        url: &str,
        classes: &[String],
        ids: &[String],
    ) -> Result<Option<String>, VibriumAdblockError> {
        let resources = self.cosmetic_resources(url)?;
        let classes = sanitize_names(classes);
        let ids = sanitize_names(ids);

        let mut selectors = self.hidden_class_id_selectors(&classes, &ids, &resources);
        selectors.sort();
        selectors.dedup();

        let css = hide_css(selectors.iter());
        Ok((!css.is_empty()).then_some(css))
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

    /// Builds an engine from every `<name>.txt` list stored in `directory`
    /// (the layout written by [`FilterStorage`]).
    ///
    /// Lists that cannot be read or fail validation are skipped, and their
    /// names are returned so the caller can report them; one bad list never
    /// disables the others. Fails only when no usable list remains.
    /// Lists are processed in name order so the result is deterministic.
    pub fn from_directory(
        directory: &std::path::Path,
    ) -> Result<(Self, Vec<String>), VibriumAdblockError> {
        let entries = std::fs::read_dir(directory).map_err(|error| {
            VibriumAdblockError::FilterListError(format!(
                "Cannot read filter-list directory: {error}"
            ))
        })?;

        let mut names: Vec<String> = entries
            .filter_map(Result::ok)
            .filter(|entry| entry.path().extension().is_some_and(|ext| ext == "txt"))
            .filter_map(|entry| {
                entry
                    .path()
                    .file_stem()
                    .and_then(|stem| stem.to_str().map(str::to_owned))
            })
            // Temporary files written by FilterStorage start with '.'.
            .filter(|name| !name.starts_with('.'))
            .collect();
        names.sort();

        let storage = FilterStorage::new(directory);
        let mut collection = FilterListCollection::new();
        let mut skipped = Vec::new();

        for name in names {
            let loaded = storage
                .load(&name)
                .and_then(|text| validate_and_clean_filter_list(&text).map(|(rules, _)| rules))
                .and_then(|rules| {
                    collection.add(FilterList::from_text(
                        name.clone(),
                        format!("file:{name}"),
                        rules,
                    ))
                });

            if loaded.is_err() {
                skipped.push(name);
            }
        }

        let mut engine = Self::from_filter_lists(&collection)?;

        // Optional scriptlet resources. A missing file is normal; an unreadable
        // or malformed one is reported but never disables blocking.
        let resources_path = directory.join("resources.json");
        if resources_path.is_file() {
            let loaded = std::fs::read_to_string(&resources_path)
                .map_err(|error| {
                    VibriumAdblockError::InvalidConfiguration(format!(
                        "Cannot read resources.json: {error}"
                    ))
                })
                .and_then(|json| engine.use_resources_json(&json));

            if loaded.is_err() {
                skipped.push("resources.json".to_string());
            }
        }

        Ok((engine, skipped))
    }

    /// Returns CSS that hides the elements matched by this page's cosmetic
    /// filters, or `None` when there is nothing to hide.
    ///
    /// Each selector becomes its own rule: in CSS one invalid selector in a
    /// comma list would otherwise discard the whole rule. Only selectors that
    /// passed [`is_valid_css_selector`] reach this point. Class/id-based
    /// generic selectors need page DOM data and are not included.
    pub fn cosmetic_css(&self, url: &str) -> Result<Option<String>, VibriumAdblockError> {
        let resources = self.cosmetic_resources(url)?;
        let mut selectors: Vec<&String> = resources.hide_selectors.iter().collect();
        selectors.sort();

        let css = hide_css(selectors.into_iter());
        Ok((!css.is_empty()).then_some(css))
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

/// One `display:none` rule per selector. In CSS a single invalid selector in
/// a comma list would discard the whole rule, so selectors are never joined.
/// Only selectors that passed [`is_valid_css_selector`] may be passed in.
fn hide_css<'a>(selectors: impl Iterator<Item = &'a String>) -> String {
    let mut css = String::new();

    for selector in selectors {
        let rule = format!("{selector}{{display:none!important}}\n");
        if css.len() + rule.len() > limits::MAX_CSS_BYTES {
            break;
        }
        css.push_str(&rule);
    }

    css
}

/// Drops empty and oversized names, removes duplicates (keeping order) and
/// applies the per-request cap.
fn sanitize_names(names: &[String]) -> Vec<String> {
    let mut seen = std::collections::HashSet::new();

    names
        .iter()
        .filter(|name| !name.is_empty() && name.len() <= limits::MAX_NAME_BYTES)
        .filter(|name| seen.insert(name.as_str()))
        .take(limits::MAX_NAMES_PER_REQUEST)
        .cloned()
        .collect()
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
                .should_block("https://cdn.example.org/app.js", SOURCE_URL, "script")
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

    #[test]
    fn generichide_disables_generic_class_and_id_selectors() {
        let engine = VibriumAdblock::new(&[
            "##.generic-ad",
            "###generic-ad",
            "@@||example.com^$generichide",
        ]);

        let resources = engine.cosmetic_resources("https://example.com/").unwrap();

        assert!(
            resources.generichide,
            "The exception rule should disable generic cosmetic filtering"
        );

        let classes = vec!["generic-ad".to_string()];
        let ids = vec!["generic-ad".to_string()];

        let selectors = engine.hidden_class_id_selectors(&classes, &ids, &resources);

        assert!(
            selectors.is_empty(),
            "Generic selectors must be suppressed when $generichide applies"
        );
    }

    #[test]
    fn generic_class_and_id_selectors_remain_available_without_generichide() {
        let engine = VibriumAdblock::new(&["##.generic-ad", "###generic-ad"]);

        let resources = engine.cosmetic_resources("https://example.com/").unwrap();

        assert!(
            !resources.generichide,
            "Generic cosmetic filtering should remain enabled"
        );

        let classes = vec!["generic-ad".to_string()];
        let ids = vec!["generic-ad".to_string()];

        let selectors = engine.hidden_class_id_selectors(&classes, &ids, &resources);

        assert!(
            !selectors.is_empty(),
            "Generic selectors should remain available when $generichide does not apply"
        );
    }

    #[test]
    fn cosmetic_resources_reject_malformed_css_selectors() {
        let engine = VibriumAdblock::new(&["example.com##.valid-ad", "example.com##.broken-ad{"]);

        let resources = engine.cosmetic_resources("https://example.com/").unwrap();

        assert!(
            resources.hide_selectors.contains(".valid-ad"),
            "Valid cosmetic selectors should be preserved"
        );

        assert!(
            !resources
                .hide_selectors
                .iter()
                .any(|selector| { !is_valid_css_selector(selector) }),
            "Malformed CSS selectors must not be returned for injection"
        );
    }
}
