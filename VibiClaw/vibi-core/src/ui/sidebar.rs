use gtk::prelude::*;
use gtk::{Box as GtkBox, Button, Label, Orientation, Separator, Align};
use gtk::glib;
use pango;
use std::rc::Rc;
use std::cell::{Cell, RefCell};

pub fn build_sidebar(stack: gtk::Stack, storage: Rc<RefCell<crate::storage::AppStorage>>, chat_store: Rc<RefCell<crate::chat_store::ChatStore>>, clear_handle: crate::ui::dashboard::ChatClearHandle, logger: Rc<RefCell<crate::logger::Logger>>) -> (GtkBox, GtkBox, Rc<RefCell<Option<Label>>>) {
    let sidebar = GtkBox::new(Orientation::Vertical, 0);
    sidebar.style_context().add_class("sidebar");
    sidebar.set_width_request(260);
    sidebar.set_hexpand(false);

    let header = GtkBox::new(Orientation::Horizontal, 8);
    header.set_margin_top(16);
    header.set_margin_bottom(16);
    header.set_margin_start(16);
    header.set_margin_end(16);

    let logo_icon = GtkBox::new(Orientation::Horizontal, 0);
    logo_icon.style_context().add_class("logo-icon");
    logo_icon.set_size_request(32, 32);
    header.pack_start(&logo_icon, false, false, 0);

    let logo_vibi = Label::new(Some("Vibi "));
    logo_vibi.style_context().add_class("logo-text-vibi");
    header.pack_start(&logo_vibi, false, false, 0);

    let logo_ai = Label::new(Some("Claw"));
    logo_ai.style_context().add_class("logo-text-ai");
    header.pack_start(&logo_ai, false, false, 0);

    let header_spacer = GtkBox::new(Orientation::Horizontal, 0);
    header_spacer.set_hexpand(true);
    header.pack_start(&header_spacer, true, true, 0);

    let toggle_btn = Button::with_label("☰");
    toggle_btn.style_context().add_class("footer-btn");
    header.pack_start(&toggle_btn, false, false, 0);

    sidebar.pack_start(&header, false, false, 0);
    sidebar.pack_start(&Separator::new(Orientation::Horizontal), false, false, 0);



    let nav_box = GtkBox::new(Orientation::Vertical, 2);
    nav_box.set_margin_start(8);
    nav_box.set_margin_end(8);

    let chat_btn = nav_item_button_with_icon("Dashboard", "", None);
    chat_btn.style_context().add_class("active");
    nav_box.pack_start(&chat_btn, false, false, 0);

    let chat_btn_label: Rc<RefCell<Option<Label>>> = Rc::new(RefCell::new(None));

    if let Some(child) = chat_btn.child() {
        if let Some(content) = child.downcast_ref::<GtkBox>() {
            for widget in content.children() {
                let is_icon = widget.style_context().list_classes().iter().any(|c| c.as_str() == "nav-icon");
                if is_icon { continue; }
                let is_label = widget.downcast_ref::<Label>();
                if let Some(label) = is_label {
                    label.set_ellipsize(pango::EllipsizeMode::End);
                    label.set_max_width_chars(12);
                    *chat_btn_label.borrow_mut() = Some(label.clone());
                    break;
                }
            }
        }
    }

    let agentic_btn = nav_item_button_with_icon("Code", "", None);
    nav_box.pack_start(&agentic_btn, false, false, 0);

    let notebook_btn = nav_item_button_with_icon("AI Notebook", "📓", Some("WEB"));
    nav_box.pack_start(&notebook_btn, false, false, 0);

    let logs_btn = nav_item_button_with_icon("Logs", "📋", None);
    nav_box.pack_start(&logs_btn, false, false, 0);



    let stack_chat = stack.clone();
    let stack_agentic = stack.clone();
    let stack_notebook = stack.clone();
    let chat_btn_1 = chat_btn.clone();
    let agentic_btn_1 = agentic_btn.clone();
    let notebook_btn_1 = notebook_btn.clone();
    let chat_btn_2 = chat_btn.clone();
    let agentic_btn_2 = agentic_btn.clone();
    let notebook_btn_2 = notebook_btn.clone();
    let chat_btn_3 = chat_btn.clone();
    let agentic_btn_3 = agentic_btn.clone();
    let notebook_btn_3 = notebook_btn.clone();
    let logs_btn_1 = logs_btn.clone();
    let logs_btn_2 = logs_btn.clone();
    let logs_btn_3 = logs_btn.clone();


    chat_btn.connect_clicked(move |_| {
        stack_chat.set_visible_child_name("chat");
        agentic_btn_1.style_context().remove_class("active");
        notebook_btn_1.style_context().remove_class("active");
        logs_btn_1.style_context().remove_class("active");

        chat_btn_1.style_context().add_class("active");
    });

    agentic_btn.connect_clicked(move |_| {
        stack_agentic.set_visible_child_name("agentic");
        chat_btn_2.style_context().remove_class("active");
        notebook_btn_2.style_context().remove_class("active");
        logs_btn_2.style_context().remove_class("active");

        agentic_btn_2.style_context().add_class("active");
    });

    notebook_btn.connect_clicked(move |_| {
        stack_notebook.set_visible_child_name("ai_notebook");
        agentic_btn_3.style_context().remove_class("active");
        chat_btn_3.style_context().remove_class("active");
        logs_btn_3.style_context().remove_class("active");

        notebook_btn_3.style_context().add_class("active");
    });


    let stack_logs = stack.clone();
    let logs_btn_4 = logs_btn.clone();
    let chat_btn_4 = chat_btn.clone();
    let agentic_btn_4 = agentic_btn.clone();
    let notebook_btn_4 = notebook_btn.clone();
    logs_btn.connect_clicked(move |_| {
        stack_logs.set_visible_child_name("logs");
        chat_btn_4.style_context().remove_class("active");
        agentic_btn_4.style_context().remove_class("active");
        notebook_btn_4.style_context().remove_class("active");
        logs_btn_4.style_context().add_class("active");
    });



    sidebar.pack_start(&nav_box, false, false, 0);
    sidebar.pack_start(&Separator::new(Orientation::Horizontal), false, false, 0);

    let recent_label = Label::new(Some("RECENT CHATS"));
    recent_label.style_context().add_class("sidebar-label");
    recent_label.set_halign(Align::Start);
    recent_label.set_margin_start(16);
    recent_label.set_margin_top(12);
    sidebar.pack_start(&recent_label, false, false, 0);

    let chat_scroll = gtk::ScrolledWindow::new(None::<&gtk::Adjustment>, None::<&gtk::Adjustment>);
    chat_scroll.set_policy(gtk::PolicyType::Never, gtk::PolicyType::Automatic);
    chat_scroll.set_vexpand(true);
    chat_scroll.set_margin_top(10);
    chat_scroll.set_margin_bottom(10);

    let chat_list_box = GtkBox::new(Orientation::Vertical, 1);
    chat_list_box.set_margin_start(10);
    chat_list_box.set_margin_end(10);
    chat_scroll.add(&chat_list_box);
    sidebar.pack_start(&chat_scroll, true, true, 0);

    let store_ref = chat_store.borrow();
    crate::ui::refresh_sidebar_chat_list(&chat_list_box, &store_ref, &stack, chat_store.clone(), clear_handle.clone(), logger.clone());

    let spacer = GtkBox::new(Orientation::Vertical, 0);
    spacer.set_vexpand(true);
    sidebar.pack_start(&spacer, true, true, 0);

    sidebar.pack_start(&Separator::new(Orientation::Horizontal), false, false, 0);

    let footer = GtkBox::new(Orientation::Vertical, 6);
    footer.set_margin_top(12);
    footer.set_margin_bottom(12);
    footer.set_margin_start(8);
    footer.set_margin_end(8);

    let theme_btn = footer_button_with_icon("☀", "Toggle theme");
    footer.pack_start(&theme_btn, false, false, 0);

    let storage_theme = storage.clone();
    theme_btn.connect_clicked(move |btn| {
        let window = btn.toplevel().and_then(|w| w.downcast::<gtk::ApplicationWindow>().ok());
        if let Some(win) = window {
            let has_light = win.style_context().list_classes().iter().any(|c| c.as_str() == "light");
            if has_light {
                win.style_context().remove_class("light");
                storage_theme.borrow_mut().set_theme("dark");
            } else {
                win.style_context().add_class("light");
                storage_theme.borrow_mut().set_theme("light");
            }
            if let Some(child) = btn.child() {
                if let Some(content) = child.downcast_ref::<GtkBox>() {
                    if let Some(icon) = content.children().first() {
                        if let Some(label) = icon.downcast_ref::<Label>() {
                            label.set_text(if has_light { "☀" } else { "☾" });
                        }
                    }
                }
            }
        }
    });

    let settings_btn = footer_button_with_icon("⚙", "Settings");
    footer.pack_start(&settings_btn, false, false, 0);

    let settings_stack = stack.clone();
    let chat_s = chat_btn.clone();
    let agentic_s = agentic_btn.clone();
    let notebook_s = notebook_btn.clone();
    let logs_s = logs_btn.clone();
    settings_btn.connect_clicked(move |_| {
        settings_stack.set_visible_child_name("settings");
        chat_s.style_context().remove_class("active");
        agentic_s.style_context().remove_class("active");
        notebook_s.style_context().remove_class("active");
        logs_s.style_context().remove_class("active");

    });

    sidebar.pack_start(&footer, false, false, 0);

    let collapsed = Rc::new(Cell::new(false));

    let sidebar_clone = sidebar.clone();
    let header_clone = header.clone();
    let logo_icon_clone = logo_icon.clone();
    let logo_vibi_clone = logo_vibi.clone();
    let logo_ai_clone = logo_ai.clone();
    let nav_box_clone = nav_box.clone();
    let recent_label_clone = recent_label.clone();
    let chat_list_box_clone = chat_list_box.clone();
    let footer_clone = footer.clone();
    let header_spacer_clone = header_spacer.clone();
    let chat_btn_clone = chat_btn.clone();
    let agentic_btn_clone = agentic_btn.clone();
    let notebook_btn_clone = notebook_btn.clone();
    let logs_btn_clone = logs_btn.clone();

    let theme_btn_clone_anim = theme_btn.clone();
    let settings_btn_clone = settings_btn.clone();

    let counter = Rc::new(RefCell::new(0));

    toggle_btn.connect_clicked(move |_| {
        let new_state = !collapsed.get();
        collapsed.set(new_state);

        let start_width = if new_state { 260 } else { 52 };
        let end_width = if new_state { 52 } else { 260 };
        let start_margin = if new_state { 16 } else { 10 };
        let end_margin = if new_state { 10 } else { 16 };

        logo_icon_clone.set_visible(!new_state);
        logo_vibi_clone.set_visible(!new_state);
        logo_ai_clone.set_visible(!new_state);
        nav_box_clone.set_visible(!new_state);
        recent_label_clone.set_visible(!new_state);
        chat_list_box_clone.set_visible(!new_state);
        footer_clone.set_visible(!new_state);
        header_spacer_clone.set_visible(!new_state);

        hide_nav_labels(&chat_btn_clone, new_state);
        hide_nav_labels(&agentic_btn_clone, new_state);
        hide_nav_labels(&notebook_btn_clone, new_state);
        hide_nav_labels(&logs_btn_clone, new_state);

        hide_footer_labels(&theme_btn_clone_anim, new_state);
        hide_footer_labels(&settings_btn_clone, new_state);

        let sidebar_anim = sidebar_clone.clone();
        let header_anim = header_clone.clone();
        let counter_clone = counter.clone();
        let steps = 20;
        let step_ms = 10;

        *counter_clone.borrow_mut() = 0;

        glib::timeout_add_local(std::time::Duration::from_millis(step_ms), move || {
            let mut count = counter_clone.borrow_mut();
            *count += 1;

            let progress = *count as f64 / steps as f64;
            let current_width = start_width as f64 + (end_width as f64 - start_width as f64) * progress;
            let current_margin = start_margin as f64 + (end_margin as f64 - start_margin as f64) * progress;

            sidebar_anim.set_width_request(current_width as i32);
            header_anim.set_margin_start(current_margin as i32);
            header_anim.set_margin_end(current_margin as i32);

            if *count >= steps {
                sidebar_anim.set_width_request(end_width);
                header_anim.set_margin_start(end_margin);
                header_anim.set_margin_end(end_margin);
                return glib::ControlFlow::Break;
            }
            glib::ControlFlow::Continue
        });
    });

    (sidebar, chat_list_box, chat_btn_label)
}

fn nav_item_button_with_icon(label_text: &str, icon: &str, badge: Option<&str>) -> Button {
    let btn = Button::new();
    btn.style_context().add_class("nav-item");

    let content = GtkBox::new(Orientation::Horizontal, 8);
    let icon_label = Label::new(Some(icon));
    icon_label.style_context().add_class("nav-icon");
    content.pack_start(&icon_label, false, false, 0);

    let label = Label::new(Some(label_text));
    label.set_halign(Align::Start);
    label.set_hexpand(true);
    label.set_ellipsize(pango::EllipsizeMode::End);
    label.set_max_width_chars(12);
    content.pack_start(&label, true, true, 0);

    if let Some(b) = badge {
        let badge_label = Label::new(Some(b));
        badge_label.style_context().add_class("badge-web");
        content.pack_start(&badge_label, false, false, 0);
    }

    btn.add(&content);
    btn
}

fn hide_nav_labels(btn: &Button, collapsed: bool) {
    if let Some(child) = btn.child() {
        if let Some(content) = child.downcast_ref::<GtkBox>() {
            let children = content.children();
            for (i, widget) in children.iter().enumerate() {
                if i == 0 {
                    widget.set_visible(true);
                } else {
                    widget.set_visible(!collapsed);
                }
            }
        }
    }
    if collapsed {
        btn.set_halign(Align::Center);
        btn.set_width_request(44);
    } else {
        btn.set_halign(Align::Fill);
        btn.set_width_request(-1);
    }
}

fn hide_footer_labels(btn: &Button, collapsed: bool) {
    if let Some(child) = btn.child() {
        if let Some(content) = child.downcast_ref::<GtkBox>() {
            let children = content.children();
            for (i, widget) in children.iter().enumerate() {
                if i == 0 {
                    widget.set_visible(true);
                } else {
                    widget.set_visible(!collapsed);
                }
            }
        }
    }
    if collapsed {
        btn.set_halign(Align::Center);
        btn.set_width_request(44);
    } else {
        btn.set_halign(Align::Fill);
        btn.set_width_request(-1);
    }
}

fn footer_button_with_icon(icon: &str, label_text: &str) -> Button {
    let btn = Button::new();
    btn.style_context().add_class("footer-btn");

    let content = GtkBox::new(Orientation::Horizontal, 8);
    let icon_label = Label::new(Some(icon));
    icon_label.style_context().add_class("footer-icon");
    content.pack_start(&icon_label, false, false, 0);

    let label = Label::new(Some(label_text));
    content.pack_start(&label, false, false, 0);

    btn.add(&content);
    btn
}