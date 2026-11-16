//! A small engine that only knows scriptlet (`+js(...)`) rules.
//!
//! Scriptlets must run in the page before any of the page's own scripts, and
//! that can only be done synchronously from inside the renderer process (see
//! `c++/adblock/scriptlet_engine.h`). The full network engine lives in the
//! browser process and holds hundreds of thousands of rules, which is far too
//! heavy to load into every renderer. A scriptlet-only engine contains just the
//! few thousand `+js(` rules, so it loads in milliseconds and uses a few MB.
//!
//! Trust: some scriptlets (`trusted-*`) can rewrite arbitrary page data, so a
//! list may only use them if it is trusted. Lists whose file name starts with
//! [`TRUSTED_LIST_PREFIX`] are the ones Vibrium downloads from uBlock Origin's
//! own repository; every other list is parsed without trust and its
//! `trusted-*` rules are ignored. Lists are not user-editable through the UI,
//! and anyone who can write files into the filter directory already controls
//! the browser's blocking.

use std::path::Path;

use adblock::engine::Engine;
use adblock::lists::{FilterSet, ParseOptions, RuleTypes};
use adblock::resources::{PermissionMask, Resource};

use crate::engine::limits;
use crate::error::VibriumAdblockError;

/// List names starting with this are parsed with the "trusted" permission.
pub const TRUSTED_LIST_PREFIX: &str = "ubo-";

/// adblock-rust `PermissionMask` bit for "trusted" scriptlets.
const TRUSTED_PERMISSION: u8 = 1;

/// Largest filter list file that will be read, in bytes.
const MAX_LIST_BYTES: u64 = 10 * 1024 * 1024;

pub struct ScriptletEngine {
    engine: Engine,
    rules: usize,
}

impl ScriptletEngine {
    /// Builds the engine from every `<name>.txt` list in `directory` plus an
    /// optional `resources.json` in the same directory.
    ///
    /// Without a usable `resources.json` there are no scriptlets to inject, so
    /// this fails: callers then simply skip scriptlet injection. Unreadable
    /// lists are skipped individually.
    pub fn from_directory(directory: &Path) -> Result<Self, VibriumAdblockError> {
        let resources = read_resources(&directory.join("resources.json"))?;

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
            .filter(|name| !name.starts_with('.'))
            .collect();
        names.sort();

        let mut filter_set = FilterSet::new(false);
        let mut rules = 0usize;

        for name in names {
            let path = directory.join(format!("{name}.txt"));

            let too_big = std::fs::metadata(&path)
                .map(|meta| meta.len() > MAX_LIST_BYTES)
                .unwrap_or(true);
            if too_big {
                continue;
            }
            let Ok(text) = std::fs::read_to_string(&path) else {
                continue;
            };

            let scriptlet_rules: Vec<&str> = text
                .lines()
                .map(str::trim)
                .filter(|line| !line.starts_with('!') && line.contains("+js("))
                .collect();
            if scriptlet_rules.is_empty() {
                continue;
            }
            rules += scriptlet_rules.len();

            let permissions = if name.starts_with(TRUSTED_LIST_PREFIX) {
                PermissionMask::from_bits(TRUSTED_PERMISSION)
            } else {
                PermissionMask::default()
            };

            filter_set.add_filter_list(
                scriptlet_rules.join("\n"),
                ParseOptions {
                    rule_types: RuleTypes::CosmeticOnly,
                    permissions,
                    ..ParseOptions::default()
                },
            );
        }

        if rules == 0 {
            return Err(VibriumAdblockError::FilterListError(
                "No scriptlet rules are available".into(),
            ));
        }

        let mut engine = Engine::new_with_filter_set(filter_set);
        engine.use_resources(resources);

        Ok(Self { engine, rules })
    }

    /// Number of scriptlet rules loaded (diagnostics and tests).
    pub fn rule_count(&self) -> usize {
        self.rules
    }

    /// The JavaScript to run in the page at `url` before its own scripts, or an
    /// empty string when no scriptlet rule applies. Only http(s) URLs qualify.
    pub fn injected_script(&self, url: &str) -> Result<String, VibriumAdblockError> {
        let parsed = url::Url::parse(url)
            .map_err(|error| VibriumAdblockError::InvalidRequest(error.to_string()))?;

        if !matches!(parsed.scheme(), "http" | "https") || parsed.host_str().is_none() {
            return Err(VibriumAdblockError::InvalidRequest(
                "Scriptlets require a valid HTTP or HTTPS URL".into(),
            ));
        }

        let script = self
            .engine
            .url_cosmetic_resources(parsed.as_str())
            .injected_script;

        if script.len() > limits::MAX_SCRIPT_BYTES {
            return Ok(String::new());
        }
        Ok(script)
    }
}

fn read_resources(path: &Path) -> Result<Vec<Resource>, VibriumAdblockError> {
    let too_big = std::fs::metadata(path)
        .map(|meta| meta.len() as usize > limits::MAX_RESOURCES_BYTES)
        .map_err(|error| {
            VibriumAdblockError::InvalidConfiguration(format!("Cannot read resources.json: {error}"))
        })?;
    if too_big {
        return Err(VibriumAdblockError::InvalidConfiguration(
            "resources.json is too large".into(),
        ));
    }

    let json = std::fs::read_to_string(path).map_err(|error| {
        VibriumAdblockError::InvalidConfiguration(format!("Cannot read resources.json: {error}"))
    })?;

    serde_json::from_str(&json).map_err(|error| {
        VibriumAdblockError::InvalidConfiguration(format!("Invalid resources.json: {error}"))
    })
}
