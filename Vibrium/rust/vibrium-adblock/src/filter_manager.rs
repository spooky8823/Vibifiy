use crate::engine::VibriumAdblock;
use crate::error::VibriumAdblockError;
use crate::filter_list_updater::FilterListDownloader;
use crate::filter_lists::{FilterList, FilterListCollection};
use crate::filter_storage::FilterStorage;
use crate::filter_validation::validate_and_clean_filter_list;

pub struct FilterManager<D: FilterListDownloader> {
    lists: FilterListCollection,
    updater: D,
    storage: FilterStorage,
    active_engine: Option<VibriumAdblock>,
}

impl<D: FilterListDownloader> FilterManager<D> {
    pub fn new(lists: FilterListCollection, updater: D, storage: FilterStorage) -> Self {
        let active_engine = VibriumAdblock::from_filter_lists(&lists).ok();

        Self {
            lists,
            updater,
            storage,
            active_engine,
        }
    }

    pub fn update_list(&mut self, name: &str) -> Result<(), VibriumAdblockError> {
        let list = self
            .lists
            .get(name)
            .ok_or_else(|| {
                VibriumAdblockError::InvalidConfiguration(format!("Unknown filter list '{name}'"))
            })?
            .clone();

        // Download and validate without changing active state.
        let downloaded_rules = self.updater.download(&list)?;
        let (cleaned_rules, _report) = validate_and_clean_filter_list(&downloaded_rules)?;

        let validated_list = FilterList::from_text(
            list.name.clone(),
            list.source_url.clone(),
            cleaned_rules.clone(),
        );
        validated_list.validate()?;

        // Prepare candidate state independently of the currently active state.
        let mut candidate_lists = self.lists.clone();
        candidate_lists.update_rules(name, cleaned_rules.clone())?;

        let candidate_engine = VibriumAdblock::from_filter_lists(&candidate_lists)?;

        // Persist before committing the candidate state.
        self.storage.save(name, &cleaned_rules)?;

        // Commit both in-memory components only after preparation and storage succeed.
        self.lists = candidate_lists;
        self.active_engine = Some(candidate_engine);

        Ok(())
    }

    pub fn rebuild_engine(&self) -> Result<VibriumAdblock, VibriumAdblockError> {
        VibriumAdblock::from_filter_lists(&self.lists)
    }

    pub fn active_engine(&self) -> Result<&VibriumAdblock, VibriumAdblockError> {
        self.active_engine.as_ref().ok_or_else(|| {
            VibriumAdblockError::FilterListError("No active ad-blocking engine is available".into())
        })
    }

    pub fn lists(&self) -> &FilterListCollection {
        &self.lists
    }

    pub fn storage(&self) -> &FilterStorage {
        &self.storage
    }

    pub fn load_stored_lists(&mut self) -> Result<(), VibriumAdblockError> {
        // Read all stored content before modifying any in-memory state.
        let mut stored_lists = Vec::new();

        for list in self.lists.iter().filter(|list| list.enabled) {
            if self.storage.exists(&list.name)? {
                let contents = self.storage.load(&list.name)?;
                let (cleaned_rules, _report) = validate_and_clean_filter_list(&contents)?;

                stored_lists.push((list.name.clone(), cleaned_rules));
            }
        }

        // Prepare candidate state using the restored rules.
        let mut candidate_lists = self.lists.clone();

        for (name, contents) in stored_lists {
            candidate_lists.update_rules(&name, contents)?;
        }

        // Prepare the engine before committing the restored state.
        let candidate_engine = VibriumAdblock::from_filter_lists(&candidate_lists)?;

        self.lists = candidate_lists;
        self.active_engine = Some(candidate_engine);

        Ok(())
    }
}
