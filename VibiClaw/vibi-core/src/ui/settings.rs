use gtk::prelude::*;
use gtk::{Box as GtkBox, Label, Orientation, Align, ScrolledWindow, PolicyType, Switch, Button, Separator};

pub fn build_settings_view() -> GtkBox {
    let root = GtkBox::new(Orientation::Vertical, 0);
    root.set_hexpand(true);
    root.set_vexpand(true);

    let topbar = GtkBox::new(Orientation::Horizontal, 0);
    topbar.style_context().add_class("topbar");
    topbar.set_margin_start(24);
    topbar.set_margin_end(24);
    topbar.set_margin_top(14);
    topbar.set_margin_bottom(14);

    let title = Label::new(Some("Settings"));
    title.style_context().add_class("topbar-title");
    topbar.pack_start(&title, false, false, 0);

    let divider = Separator::new(Orientation::Horizontal);
    divider.style_context().add_class("topbar-divider");

    root.pack_start(&topbar, false, false, 0);
    root.pack_start(&divider, false, false, 0);

    let scroll = ScrolledWindow::new(None::<&gtk::Adjustment>, None::<&gtk::Adjustment>);
    scroll.set_vexpand(true);
    scroll.set_policy(PolicyType::Never, PolicyType::Automatic);

    let content = GtkBox::new(Orientation::Vertical, 16);
    content.set_margin_start(24);
    content.set_margin_end(24);
    content.set_margin_top(20);
    content.set_margin_bottom(20);

    // Browser section
    let browser_section = settings_section("Browser");
    let browser_row = settings_row("Auto-start browser", "Browser loads on app start when enabled");
    let browser_switch = Switch::new();
    browser_switch.set_active(false);
    browser_switch.set_valign(Align::Center);
    browser_row.pack_start(&browser_switch, false, false, 0);
    browser_section.pack_start(&browser_row, false, false, 0);
    content.pack_start(&browser_section, false, false, 0);

    // General section
    let general_section = settings_section("General");
    let theme_row = settings_row("Dark mode", "Toggle between light and dark theme");
    let theme_switch = Switch::new();
    theme_switch.set_valign(Align::Center);
    theme_row.pack_start(&theme_switch, false, false, 0);
    general_section.pack_start(&theme_row, false, false, 0);
    content.pack_start(&general_section, false, false, 0);

    // Storage section
    let storage_section = settings_section("Storage");
    let clear_logs_btn = Button::with_label("Clear All Logs");
    clear_logs_btn.style_context().add_class("settings-btn-danger");
    clear_logs_btn.set_halign(Align::Start);
    storage_section.pack_start(&clear_logs_btn, false, false, 0);
    content.pack_start(&storage_section, false, false, 0);

    // About section
    let about_section = settings_section("About");
    let version_row = settings_row("Version", "0.1.0");
    about_section.pack_start(&version_row, false, false, 0);
    content.pack_start(&about_section, false, false, 0);

    scroll.add(&content);
    root.pack_start(&scroll, true, true, 0);
    root
}

fn settings_section(title: &str) -> GtkBox {
    let section = GtkBox::new(Orientation::Vertical, 8);
    
    let label = Label::new(Some(title));
    label.style_context().add_class("settings-section-title");
    label.set_halign(Align::Start);
    section.pack_start(&label, false, false, 0);

    let card = GtkBox::new(Orientation::Vertical, 0);
    card.style_context().add_class("settings-card");
    section.pack_start(&card, false, false, 0);
    section
}

fn settings_row(label: &str, description: &str) -> GtkBox {
    let row = GtkBox::new(Orientation::Horizontal, 12);
    row.style_context().add_class("settings-row");
    row.set_margin_start(16);
    row.set_margin_end(16);
    row.set_margin_top(10);
    row.set_margin_bottom(10);

    let info = GtkBox::new(Orientation::Vertical, 2);
    let title = Label::new(Some(label));
    title.style_context().add_class("settings-row-title");
    title.set_halign(Align::Start);
    info.pack_start(&title, false, false, 0);

    let desc = Label::new(Some(description));
    desc.style_context().add_class("settings-row-desc");
    desc.set_halign(Align::Start);
    info.pack_start(&desc, false, false, 0);

    row.pack_start(&info, true, true, 0);
    row
}