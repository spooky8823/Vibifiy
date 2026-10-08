use gtk::prelude::*;
use gtk::{Box as GtkBox, Button, Label, Orientation, Align, ScrolledWindow, PolicyType, Revealer, Separator};
use std::rc::Rc;
use std::cell::RefCell;
use crate::logger::{Logger, LogEntry, LogLevel};
use crate::types::Command;
use std::sync::Mutex;

static APPROVAL_TX: Mutex<Option<async_channel::Sender<Vec<Command>>>> = Mutex::new(None);

pub fn set_approval_channel(tx: async_channel::Sender<Vec<Command>>) {
    *APPROVAL_TX.lock().unwrap() = Some(tx);
}

pub fn queue_for_approval(commands: Vec<Command>) {
    if let Ok(tx) = APPROVAL_TX.lock() {
        if let Some(ref tx) = *tx {
            let _ = tx.try_send(commands);
        }
    }
}

pub struct NotificationPanel {
    pub container: GtkBox,
    pub revealer: Revealer,
    pub badge: Rc<RefCell<Label>>,
    panel_list: GtkBox,
    count: Rc<RefCell<u32>>,
}

impl NotificationPanel {
    pub fn new(logger: Rc<RefCell<Logger>>) -> Self {
        let revealer = Revealer::new();
        revealer.set_transition_type(gtk::RevealerTransitionType::SlideLeft);
        revealer.set_transition_duration(250);
        revealer.set_reveal_child(false);

        let container = GtkBox::new(Orientation::Vertical, 0);
        container.set_size_request(320, -1);
        container.style_context().add_class("notification-panel");
        container.set_vexpand(true);

        let header = GtkBox::new(Orientation::Horizontal, 8);
        header.style_context().add_class("notification-header");
        header.set_margin_start(16);
        header.set_margin_end(16);
        header.set_margin_top(12);
        header.set_margin_bottom(8);

        let title = Label::new(Some("🔔 Notifications"));
        title.style_context().add_class("notification-title");
        title.set_halign(Align::Start);
        title.set_hexpand(true);
        header.pack_start(&title, true, true, 0);

        let clear_btn = Button::with_label("Clear All");
        clear_btn.style_context().add_class("notification-clear-btn");
        header.pack_start(&clear_btn, false, false, 0);

        container.pack_start(&header, false, false, 0);
        container.pack_start(&Separator::new(Orientation::Horizontal), false, false, 0);

        let scroll = ScrolledWindow::new(None::<&gtk::Adjustment>, None::<&gtk::Adjustment>);
        scroll.set_vexpand(true);
        scroll.set_policy(PolicyType::Never, PolicyType::Automatic);

        let panel_list = GtkBox::new(Orientation::Vertical, 0);
        panel_list.set_vexpand(true);
        panel_list.set_valign(Align::Start);
        scroll.add(&panel_list);
        container.pack_start(&scroll, true, true, 0);

        revealer.add(&container);

        let count = Rc::new(RefCell::new(0u32));
        let badge = Rc::new(RefCell::new(Label::new(Some(""))));

        let panel = NotificationPanel {
            container,
            revealer,
            badge: badge.clone(),
            panel_list: panel_list.clone(),
            count: count.clone(),
        };

        let list = panel_list.clone();
        let cnt = count.clone();
        let badge_label = badge.clone();
        logger.borrow_mut().on_log(move |entry: &LogEntry| {
            *cnt.borrow_mut() += 1;
            let num = *cnt.borrow();
            badge_label.borrow().set_text(&num.to_string());

            let row = build_notification_row(entry);
            list.pack_start(&row, false, false, 0);
            row.show_all();

            // Auto-scroll to bottom
            let adj = scroll.vadjustment();
            gtk::glib::idle_add_local(move || {
                adj.set_value(adj.upper());
                gtk::glib::ControlFlow::Break
            });
        });

        let list_clear = panel_list.clone();
        let cnt_clear = count.clone();
        let badge_clear = badge.clone();
        clear_btn.connect_clicked(move |_| {
            let children = list_clear.children();
            for child in &children {
                list_clear.remove(child);
            }
            *cnt_clear.borrow_mut() = 0;
            badge_clear.borrow().set_text("");
        });

        panel
    }

    pub fn toggle(&self) {
        let revealed = self.revealer.reveals_child();
        self.revealer.set_reveal_child(!revealed);
    }

    pub fn set_badge(&self, text: &str) {
        self.badge.borrow().set_text(text);
    }
}




fn build_notification_row(entry: &LogEntry) -> GtkBox {
    let row = GtkBox::new(Orientation::Vertical, 4);
    row.style_context().add_class("notification-row");
    row.set_margin_start(16);
    row.set_margin_end(16);
    row.set_margin_top(8);
    row.set_margin_bottom(8);

    let top = GtkBox::new(Orientation::Horizontal, 8);
    let icon = match entry.level {
        LogLevel::Success => "🟢",
        LogLevel::Error => "🔴",
        LogLevel::Warning => "🟡",
        LogLevel::Info => "🔵",
        LogLevel::Debug => "⚪",
    };
    let icon_label = Label::new(Some(icon));
    icon_label.style_context().add_class("notification-icon");
    top.pack_start(&icon_label, false, false, 0);

    let category = Label::new(Some(&entry.category));
    category.style_context().add_class("notification-category");
    category.set_halign(Align::Start);
    category.set_hexpand(true);
    top.pack_start(&category, true, true, 0);

    let ts = Label::new(Some(&format_time(&entry.timestamp)));
    ts.style_context().add_class("notification-time");
    top.pack_start(&ts, false, false, 0);

    row.pack_start(&top, false, false, 0);

    let msg = Label::new(Some(&entry.message));
    msg.style_context().add_class("notification-message");
    msg.set_halign(Align::Start);
    msg.set_wrap(true);
    msg.set_max_width_chars(35);
    msg.set_margin_start(24);
    row.pack_start(&msg, false, false, 0);

    row
}

fn format_time(timestamp: &str) -> String {
    // timestamp format: "2025-07-19 22:15:30.123"
    // show just "22:15"
    if timestamp.len() >= 16 {
        timestamp[11..16].to_string()
    } else {
        timestamp.to_string()
    }
}