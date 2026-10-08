use gtk::prelude::*;
use gtk::{Box as GtkBox, Label, Orientation, Align, ScrolledWindow, PolicyType, Button, Stack, Separator};
use gtk::glib;
use pango;
use std::rc::Rc;
use std::cell::RefCell;
use std::fs;
use crate::logger::{Logger, LogEntry, LogLevel};

pub fn build_logs_view(logger: Rc<RefCell<Logger>>) -> GtkBox {
    let root = GtkBox::new(Orientation::Vertical, 0);
    root.set_hexpand(true);
    root.set_vexpand(true);

    let topbar = GtkBox::new(Orientation::Horizontal, 0);
    topbar.style_context().add_class("topbar");
    topbar.set_margin_start(24);
    topbar.set_margin_end(24);
    topbar.set_margin_top(14);
    topbar.set_margin_bottom(14);

    let title = Label::new(Some("Logs"));
    title.style_context().add_class("topbar-title");
    topbar.pack_start(&title, false, false, 0);

    let back_btn = Button::with_label("← Back");
    back_btn.style_context().add_class("log-back-btn");
    back_btn.set_visible(false);
    topbar.pack_start(&back_btn, false, false, 0);

    let divider = Separator::new(Orientation::Horizontal);
    divider.style_context().add_class("topbar-divider");

    root.pack_start(&topbar, false, false, 0);
    root.pack_start(&divider, false, false, 0);

    let logs_stack = Stack::new();
    logs_stack.set_hexpand(true);
    logs_stack.set_vexpand(true);

    let sessions_page = build_sessions_page(logger.clone(), logs_stack.clone(), back_btn.clone(), title.clone());
    logs_stack.add_titled(&sessions_page, "sessions", "Sessions");

    root.pack_start(&logs_stack, true, true, 0);
    root
}

fn build_sessions_page(logger: Rc<RefCell<Logger>>, stack: Stack, back_btn: Button, title_label: Label) -> GtkBox {
    let root = GtkBox::new(Orientation::Vertical, 0);
    root.set_hexpand(true);
    root.set_vexpand(true);

    let scroll = ScrolledWindow::new(None::<&gtk::Adjustment>, None::<&gtk::Adjustment>);
    scroll.set_vexpand(true);
    scroll.set_policy(PolicyType::Never, PolicyType::Automatic);

    let content = GtkBox::new(Orientation::Vertical, 16);
    content.set_margin_start(24);
    content.set_margin_end(24);
    content.set_margin_top(20);
    content.set_margin_bottom(20);

    let live_label = Label::new(Some("LIVE SESSION"));
    live_label.style_context().add_class("agentic-section-title");
    live_label.set_halign(Align::Start);
    content.pack_start(&live_label, false, false, 0);

    let live_flow = gtk::FlowBox::new();
    live_flow.set_selection_mode(gtk::SelectionMode::None);
    live_flow.set_min_children_per_line(1);
    live_flow.set_max_children_per_line(4);
    live_flow.set_homogeneous(true);
    live_flow.set_row_spacing(12);
    live_flow.set_column_spacing(12);

    let live_card = session_card("Live Session", "Currently recording...", true);
    let logger_live = logger.clone();
    let stack_live = stack.clone();
    let back_btn_live = back_btn.clone();
    let title_live = title_label.clone();
    live_card.connect_button_press_event(move |_, _| {
        let live_view = build_log_detail_view(logger_live.clone(), true);
        stack_live.add_titled(&live_view, "live", "Live Log");
        stack_live.set_visible_child_name("live");
        back_btn_live.set_visible(true);
        title_live.set_text("Live Log");
        false.into()
    });
    live_flow.insert(&live_card, -1);
    content.pack_start(&live_flow, false, false, 0);

    let past_label = Label::new(Some("PAST SESSIONS"));
    past_label.style_context().add_class("agentic-section-title");
    past_label.set_halign(Align::Start);
    past_label.set_margin_top(8);
    content.pack_start(&past_label, false, false, 0);

    let past_flow = gtk::FlowBox::new();
    past_flow.set_selection_mode(gtk::SelectionMode::None);
    past_flow.set_min_children_per_line(1);
    past_flow.set_max_children_per_line(4);
    past_flow.set_homogeneous(true);
    past_flow.set_row_spacing(12);
    past_flow.set_column_spacing(12);

    let logs_dir = dirs::config_dir().unwrap_or_else(|| std::path::PathBuf::from(".")).join("vibi-ai").join("logs");
    if logs_dir.exists() {
        if let Ok(entries) = fs::read_dir(&logs_dir) {
            let mut files: Vec<_> = entries.filter_map(|e| e.ok())
                .filter(|f| f.file_name().to_string_lossy().ends_with(".sessiondata"))
                .collect();
            files.sort_by(|a, b| b.file_name().cmp(&a.file_name()));
            for file in files.iter().take(20) {
                let name = file.file_name().to_string_lossy().to_string();
                let display = name.replace(".sessiondata", "");
                let card = session_card(&display, "Tap to view", false);
                let stack_clone = stack.clone();
                let path = file.path();
                let path_del = file.path();
                let back_btn_clone = back_btn.clone();
                let title_clone = title_label.clone();
                let display_clone = display.clone();
                card.connect_button_press_event(move |_, _| {
                    let past_view = build_log_detail_view_from_file(&path);
                    stack_clone.add_titled(&past_view, &display_clone, &display_clone);
                    stack_clone.set_visible_child_name(&display_clone);
                    back_btn_clone.set_visible(true);
                    title_clone.set_text(&display_clone);
                    false.into()
                });

                let delete_btn = Button::with_label("🗑");
                delete_btn.style_context().add_class("log-delete-btn");
                delete_btn.set_tooltip_text(Some("Delete this session log"));
                card.pack_start(&delete_btn, false, false, 0);

                let stack_delete = stack.clone();
                let _flow_clone = past_flow.clone();
                let _back_del = back_btn.clone();
                let _title_del = title_label.clone();
                let _logger_del = logger.clone();
                let card_clone = card.clone();
                let path_del2 = path_del.clone();
                delete_btn.connect_clicked(move |_| {
                    let path_del3 = path_del2.clone();
                    let stack_del3 = stack_delete.clone();
                    let card_clone2 = card_clone.clone();
                    show_delete_log_confirm(&path_del3, stack_del3, move || {
                        animate_card_out(&card_clone2, || {});
                    });
                });

                past_flow.insert(&card, -1);
            }
        }
    }
    content.pack_start(&past_flow, false, false, 0);

    scroll.add(&content);

    let stack_back = stack.clone();
    let back_btn_back = back_btn.clone();
    let title_back = title_label.clone();
    back_btn.connect_clicked(move |_| {
        stack_back.set_visible_child_name("sessions");
        back_btn_back.set_visible(false);
        title_back.set_text("Logs");
    });
    root.pack_start(&scroll, true, true, 0);
    root
}

fn session_card(title: &str, subtitle: &str, is_live: bool) -> GtkBox {
    let card = GtkBox::new(Orientation::Horizontal, 12);
    card.style_context().add_class("log-session-card");
    card.set_size_request(280, 70);
    card.set_hexpand(true);

    let indicator = Label::new(Some(if is_live { "🟢" } else { "📄" }));
    indicator.style_context().add_class("log-card-icon");
    card.pack_start(&indicator, false, false, 0);

    let info = GtkBox::new(Orientation::Vertical, 2);
    let title_label = Label::new(Some(title));
    title_label.style_context().add_class("log-card-title");
    title_label.set_halign(Align::Start);
    info.pack_start(&title_label, false, false, 0);

    let sub_label = Label::new(Some(subtitle));
    sub_label.style_context().add_class("log-card-subtitle");
    sub_label.set_halign(Align::Start);
    info.pack_start(&sub_label, false, false, 0);

    card.pack_start(&info, true, true, 0);
    card
}

fn build_log_detail_view(logger: Rc<RefCell<Logger>>, is_live: bool) -> GtkBox {
    let root = GtkBox::new(Orientation::Vertical, 0);
    root.set_hexpand(true);
    root.set_vexpand(true);

    let scroll = ScrolledWindow::new(None::<&gtk::Adjustment>, None::<&gtk::Adjustment>);
    scroll.set_vexpand(true);
    scroll.set_policy(PolicyType::Never, PolicyType::Automatic);

    let log_list = GtkBox::new(Orientation::Vertical, 0);
    log_list.style_context().add_class("log-list");
    log_list.set_margin_start(16);
    log_list.set_margin_end(16);
    log_list.set_margin_top(8);
    log_list.set_margin_bottom(8);

    scroll.add(&log_list);
    root.pack_start(&scroll, true, true, 0);

    for entry in logger.borrow().entries.iter() {
        let row = build_log_row(entry);
        log_list.pack_start(&row, false, false, 0);
    }

    if is_live {
        let log_list_rc = Rc::new(RefCell::new(log_list.clone()));
        let scroll_clone = scroll.clone();
        logger.borrow_mut().on_log(move |entry: &LogEntry| {
            let entry_clone = entry.clone();
            let list = log_list_rc.clone();
            let sc = scroll_clone.clone();
            glib::idle_add_local(move || {
                let row = build_log_row(&entry_clone);
                list.borrow().pack_start(&row, false, false, 0);
                list.borrow().show_all();
                let adj = sc.vadjustment();
                adj.set_value(adj.upper());
                glib::ControlFlow::Break
            });
        });
    }

    root
}

fn build_log_detail_view_from_file(path: &std::path::Path) -> GtkBox {
    let root = GtkBox::new(Orientation::Vertical, 0);
    root.set_hexpand(true);
    root.set_vexpand(true);

    let scroll = ScrolledWindow::new(None::<&gtk::Adjustment>, None::<&gtk::Adjustment>);
    scroll.set_vexpand(true);
    scroll.set_policy(PolicyType::Never, PolicyType::Automatic);

    let log_list = GtkBox::new(Orientation::Vertical, 0);
    log_list.style_context().add_class("log-list");
    log_list.set_margin_start(16);
    log_list.set_margin_end(16);
    log_list.set_margin_top(8);
    log_list.set_margin_bottom(8);

    match crate::logger::Logger::load_from_disk(path) {
        Ok(entries) => {
            for entry in &entries {
                let row = build_log_row(entry);
                log_list.pack_start(&row, false, false, 0);
            }
        }
        Err(ref err) => {
            let banner = Label::new(Some(err));
            banner.style_context().add_class("log-tampered-banner");
            banner.set_halign(Align::Start);
            banner.set_wrap(true);
            log_list.pack_start(&banner, false, false, 0);
        }
    }

    scroll.add(&log_list);
    root.pack_start(&scroll, true, true, 0);
    root
}

fn show_delete_log_confirm<F: FnOnce() + 'static>(path: &std::path::Path, stack: Stack, on_delete: F) {
    let dialog = gtk::Window::new(gtk::WindowType::Toplevel);
    dialog.set_modal(true);
    dialog.set_default_size(380, 200);
    dialog.set_decorated(false);
    dialog.set_resizable(false);
    dialog.style_context().add_class("html-dialog");

    let content = GtkBox::new(Orientation::Vertical, 0);
    content.style_context().add_class("dialog-content");

    let title = Label::new(Some("Delete Session Log"));
    title.style_context().add_class("dialog-title");
    title.set_margin_start(20);
    title.set_margin_end(20);
    title.set_margin_top(20);
    title.set_margin_bottom(8);
    content.pack_start(&title, false, false, 0);
    content.pack_start(&Separator::new(Orientation::Horizontal), false, false, 0);

    let body = GtkBox::new(Orientation::Vertical, 8);
    body.set_margin_start(20);
    body.set_margin_end(20);
    body.set_margin_top(14);
    body.set_margin_bottom(14);
    let label = Label::new(Some("This session log will be permanently deleted.\nThe app remembers, but you can choose to forget."));
    label.set_wrap(true);
    label.style_context().add_class("dialog-body-text");
    body.pack_start(&label, false, false, 0);
    content.pack_start(&body, false, false, 0);
    content.pack_start(&Separator::new(Orientation::Horizontal), false, false, 0);

    let buttons = GtkBox::new(Orientation::Horizontal, 8);
    buttons.set_halign(Align::End);
    buttons.set_margin_start(20);
    buttons.set_margin_end(20);
    buttons.set_margin_top(12);
    buttons.set_margin_bottom(16);

    let cancel = Button::with_label("Keep");
    cancel.style_context().add_class("dialog-btn-secondary");
    let delete = Button::with_label("Forget");
    delete.style_context().add_class("dialog-btn-danger");
    buttons.pack_start(&cancel, false, false, 0);
    buttons.pack_start(&delete, false, false, 0);
    content.pack_start(&buttons, false, false, 0);

    dialog.add(&content);

    let dlg = dialog.clone();
    let path_clone = path.to_path_buf();
    let stack_clone = stack.clone();
    let on_delete = Rc::new(RefCell::new(Some(on_delete)));
    delete.connect_clicked(move |_| {
        dlg.close();
        if let Some(anim) = on_delete.borrow_mut().take() {
            anim();
        }
        std::fs::remove_file(&path_clone).ok();
        stack_clone.set_visible_child_name("sessions");
    });

    let dlg2 = dialog.clone();
    cancel.connect_clicked(move |_| dlg2.close());

    dialog.show_all();
}

fn animate_card_out<F: FnOnce() + 'static>(card: &GtkBox, on_done: F) {
    let card_clone = card.clone();
    let steps = 15;
    let step_ms = 16;
    let mut count = 0;
    let mut on_done = Some(on_done);
    glib::timeout_add_local(std::time::Duration::from_millis(step_ms), move || {
        count += 1;
        let progress = count as f64 / steps as f64;
        let opacity = 1.0 - progress;
        let translate_x = progress * 100.0;
        card_clone.set_opacity(opacity);
        card_clone.set_margin_start(translate_x as i32);
        if count >= steps {
            if let Some(f) = on_done.take() { f(); }
            return glib::ControlFlow::Break;
        }
        glib::ControlFlow::Continue
    });
}

fn build_log_row(entry: &LogEntry) -> GtkBox {
    let row = GtkBox::new(Orientation::Horizontal, 6);
    row.style_context().add_class("log-row");
    row.set_margin_top(1);
    row.set_margin_bottom(1);

    let level_class = match entry.level {
        LogLevel::Info => "log-info",
        LogLevel::Success => "log-success",
        LogLevel::Warning => "log-warning",
        LogLevel::Error => "log-error",
        LogLevel::Debug => "log-debug",
    };

    let level_label = Label::new(Some(&format!("[{:?}]", entry.level)));
    level_label.style_context().add_class(level_class);
    level_label.style_context().add_class("log-level");
    row.pack_start(&level_label, false, false, 0);

    let ts_label = Label::new(Some(&entry.timestamp));
    ts_label.style_context().add_class("log-timestamp");
    row.pack_start(&ts_label, false, false, 0);

    let cat_label = Label::new(Some(&format!("[{}]", entry.category)));
    cat_label.style_context().add_class("log-category");
    row.pack_start(&cat_label, false, false, 0);

    let msg_label = Label::new(Some(&entry.message));
    msg_label.style_context().add_class("log-message");
    msg_label.set_hexpand(true);
    msg_label.set_halign(Align::Start);
    msg_label.set_ellipsize(pango::EllipsizeMode::End);
    row.pack_start(&msg_label, true, true, 0);

    row
}