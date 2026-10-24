use gtk::prelude::*;
use gtk::{Box as GtkBox, Label, Orientation, Align};
use std::collections::HashMap;

pub struct CommandRegistry {
    commands: HashMap<String, CommandInfo>,
}

pub struct CommandInfo {
    pub name: &'static str,
    pub description: &'static str,
    pub usage: &'static str,
}

impl CommandRegistry {
    pub fn new() -> Self {
        let mut commands = HashMap::new();
        commands.insert("see".to_string(), CommandInfo {
            name: "/see", description: "View a file from linked codebase", usage: "/see <filename>",
        });
        commands.insert("create".to_string(), CommandInfo {
            name: "/create", description: "Create a new file", usage: "/create <path>",
        });
        commands.insert("edit".to_string(), CommandInfo {
            name: "/edit", description: "Edit an existing file", usage: "/edit <path>",
        });
        commands.insert("delete".to_string(), CommandInfo {
            name: "/delete", description: "Delete a file", usage: "/delete <path>",
        });
        commands.insert("run".to_string(), CommandInfo {
            name: "/run", description: "Run a shell command in sandbox", usage: "/run <command>",
        });
        commands.insert("install".to_string(), CommandInfo {
            name: "/install", description: "Install a dependency", usage: "/install <package>",
        });
        commands.insert("projects".to_string(), CommandInfo {
            name: "/projects", description: "List linked projects", usage: "/projects",
        });
        commands.insert("help".to_string(), CommandInfo {
            name: "/help", description: "Show all available commands", usage: "/help",
        });
        CommandRegistry { commands }
    }

    pub fn find_matches(&self, prefix: &str) -> Vec<&CommandInfo> {
        let lower = prefix.to_lowercase();
        self.commands.values().filter(|c| c.name.to_lowercase().starts_with(&lower)).collect()
    }

    pub fn all_commands(&self) -> Vec<&CommandInfo> {
        self.commands.values().collect()
    }
}

pub struct CommandSuggestionPopover {
    pub container: GtkBox,
    list_box: GtkBox,
}
 
impl CommandSuggestionPopover {
    pub fn new() -> Self {
        let container = GtkBox::new(Orientation::Vertical, 0);
        container.style_context().add_class("command-popover");
        container.set_visible(false);
        container.hide();
        container.set_margin_bottom(4);
        container.set_no_show_all(true);

        let list_box = GtkBox::new(Orientation::Vertical, 2);
        list_box.style_context().add_class("command-list");
        container.pack_start(&list_box, false, false, 0);

        CommandSuggestionPopover { container, list_box }
    }

    pub fn show_suggestions(&self, registry: &CommandRegistry, input: &str, entry: &gtk::Entry) {
        let children = self.list_box.children();
        for child in &children { self.list_box.remove(child); }

        if !input.starts_with('/') {
            self.container.set_visible(false);
            return;
        }

        let matches = if input == "/" {
            registry.all_commands()
        } else {
            registry.find_matches(input)
        };

        if matches.is_empty() {
            self.container.set_visible(false);
            return;
        }

        for cmd in matches.iter() {
            let row = GtkBox::new(Orientation::Horizontal, 10);
            row.style_context().add_class("command-item");

            let name_label = Label::new(Some(cmd.name));
            name_label.style_context().add_class("command-name");
            name_label.set_halign(Align::Start);
            row.pack_start(&name_label, false, false, 0);

            let desc_label = Label::new(Some(cmd.description));
            desc_label.style_context().add_class("command-desc");
            desc_label.set_halign(Align::Start);
            desc_label.set_hexpand(true);
            row.pack_start(&desc_label, true, true, 0);

            let cmd_name = cmd.name.to_string();
            let entry_clone = entry.clone();
            let popover_list = self.list_box.clone();
            row.connect_button_press_event(move |_, _| {
                entry_clone.set_text(&format!("{} ", cmd_name));
                entry_clone.set_position(-1);
                let children = popover_list.children();
                for child in &children { popover_list.remove(child); }
                false.into()
            });

            self.list_box.pack_start(&row, false, false, 0);
        }
        self.container.set_visible(true);
    }
}