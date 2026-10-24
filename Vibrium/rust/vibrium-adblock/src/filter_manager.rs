use crate::engine::VibriumAdblock;
use crate::error::VibriumAdblockError;
use crate::filter_list_updater::{FilterListDownloader, FilterListUpdater};
use crate::filter_lists::{FilterList, FilterListCollection};
use crate::filter_storage::FilterStorage;

pub struct FilterManager<D: FilterListDownloader = FilterListUpdater> {
    lists: FilterListCollection,
    updater: D,
    storage: FilterStorage,
}

impl<D: FilterListDownloader> FilterManager<D> {
    pub fn new(lists: FilterListCollection, updater: D, storage: FilterStorage) -> Self {
        Self {
            lists,
            updater,
            storage,
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

        // Download first. A failed download must not modify existing rules.
        let downloaded_rules = self.updater.download(&list)?;

        let validated_list = FilterList::from_text(
            list.name.clone(),
            list.source_url.clone(),
            downloaded_rules.clone(),
        );

        // Validate list metadata and ensure there is usable rule content.
        validated_list.validate()?;

        // Ask the existing engine to parse the downloaded rules before saving.
        // This constructor does not return parse errors, so this is a parsing
        // step rather than a strict guarantee that every rule is valid.
        let _validated_engine = VibriumAdblock::from_filter_list(&downloaded_rules);

        // Save before changing in-memory state. If saving fails, the current
        // list remains unchanged in memory.
        self.storage.save(name, &downloaded_rules)?;

        self.lists.update_rules(name, downloaded_rules)?;

        Ok(())
    }

    pub fn rebuild_engine(&self) -> Result<VibriumAdblock, VibriumAdblockError> {
        VibriumAdblock::from_filter_lists(&self.lists)
    }

    pub fn lists(&self) -> &FilterListCollection {
        &self.lists
    }

    pub fn storage(&self) -> &FilterStorage {
        &self.storage
    }

    pub fn load_stored_lists(&mut self) -> Result<(), VibriumAdblockError> {
        let mut stored_lists = Vec::new();

        for list in self.lists.iter().filter(|list| list.enabled) {
            if self.storage.exists(&list.name)? {
                let contents = self.storage.load(&list.name)?;
                stored_lists.push((list.name.clone(), contents));
            }
        }

        // Load all files successfully before mutating the in-memory collection.
        for (name, contents) in stored_lists {
            self.lists.update_rules(&name, contents)?;
        }

        Ok(())
    }
}
