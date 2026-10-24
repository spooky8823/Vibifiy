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

        let downloaded_rules = self.updater.download(&list)?;

        let validated_list = FilterList::from_text(
            list.name.clone(),
            list.source_url.clone(),
            downloaded_rules.clone(),
        );

        validated_list.validate()?;

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

        for (name, contents) in stored_lists {
            self.lists.update_rules(&name, contents)?;
        }

        Ok(())
    }
}
