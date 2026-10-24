use gtk::prelude::*;
use gtk::{Align, Box as GtkBox, Label, Orientation};
use std::cell::RefCell;

pub struct AiBridge {
    pub container: GtkBox,
    notice: Label,
    model: RefCell<String>,
}

impl AiBridge {
    pub fn new() -> Self {
        let container = GtkBox::new(Orientation::Vertical, 0);
        container.set_hexpand(true);
        container.set_vexpand(true);

        let notice = Label::new(Some(
            "WebView models (ChatGPT / Gemini in an embedded browser) are only available \
             on Linux builds of VibiClaw.\n\nUse an API model from the dashboard instead.",
        ));
        notice.set_wrap(true);
        notice.set_justify(gtk::Justification::Center);
        notice.set_halign(Align::Center);
        notice.set_valign(Align::Center);
        notice.set_hexpand(true);
        notice.set_vexpand(true);
        notice.set_max_width_chars(60);
        container.pack_start(&notice, true, true, 0);

        AiBridge {
            container,
            notice,
            model: RefCell::new(String::new()),
        }
    }

    pub fn load_model(&self, new_model: &str) {
        *self.model.borrow_mut() = new_model.to_string();
        self.notice.set_text(&format!(
            "\"{}\" runs in an embedded WebKit browser, which is only available on Linux builds.\n\n\
             Use an API model from the dashboard instead.",
            new_model
        ));
    }
}
