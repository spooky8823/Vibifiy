use std::collections::HashSet;

use crate::error::VibriumAdblockError;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FilterList {
    pub name: String,
    pub source_url: String,
    pub enabled: bool,
    pub rules: String,
}

impl FilterList {
    pub fn new(
        name: impl Into<String>,
        source_url: impl Into<String>,
    ) -> Self {
        Self {
            name: name.into(),
            source_url: source_url.into(),
            enabled: true,
            rules: String::new(),
        }
    }

    pub fn from_text(
        name: impl Into<String>,
        source_url: impl Into<String>,
        text: impl Into<String>,
    ) -> Self {
        Self {
            name: name.into(),
            source_url: source_url.into(),
            enabled: true,
            rules: text.into(),
        }
    }

    pub fn validate(&self) -> Result<(), VibriumAdblockError> {
        if self.name.trim().is_empty() {
            return Err(VibriumAdblockError::InvalidConfiguration(
                "Filter list name cannot be empty".into(),
            ));
        }

        if self.source_url.trim().is_empty() {
            return Err(VibriumAdblockError::InvalidConfiguration(
                "Filter list source cannot be empty".into(),
            ));
        }

        if self.rules.trim().is_empty() {
            return Err(VibriumAdblockError::FilterListError(
                format!("Filter list '{}' contains no rules", self.name),
            ));
        }

        Ok(())
    }
}

#[derive(Debug, Default)]
pub struct FilterListCollection {
    lists: Vec<FilterList>,
}

impl FilterListCollection {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn add(
        &mut self,
        list: FilterList,
    ) -> Result<(), VibriumAdblockError> {
        list.validate()?;

        if self
            .lists
            .iter()
            .any(|existing| existing.name == list.name)
        {
            return Err(VibriumAdblockError::InvalidConfiguration(
                format!("Filter list '{}' already exists", list.name),
            ));
        }

        self.lists.push(list);
        Ok(())
    }

    pub fn set_enabled(
        &mut self,
        name: &str,
        enabled: bool,
    ) -> Result<(), VibriumAdblockError> {
        let list = self
            .lists
            .iter_mut()
            .find(|list| list.name == name)
            .ok_or_else(|| {
                VibriumAdblockError::InvalidConfiguration(
                    format!("Unknown filter list '{name}'"),
                )
            })?;

        list.enabled = enabled;
        Ok(())
    }

    pub fn get(&self, name: &str) -> Option<&FilterList> {
        self.lists.iter().find(|list| list.name == name)
    }

    pub fn iter(&self) -> impl Iterator<Item = &FilterList> {
        self.lists.iter()
    }

    pub fn combined_rules(&self) -> String {
        let mut seen = HashSet::new();
        let mut combined = String::new();

        for list in self.lists.iter().filter(|list| list.enabled) {
            for line in list.rules.lines() {
                let rule = line.trim();

                if rule.is_empty()
                    || rule.starts_with('!')
                    || rule.starts_with('[')
                {
                    continue;
                }

                if seen.insert(rule.to_owned()) {
                    combined.push_str(rule);
                    combined.push('\n');
                }
            }
        }

        combined
    }
}