pub mod sidebar;
pub mod dashboard;
pub mod code_editor;
pub mod dialog;
pub mod logs;
pub mod settings;
pub mod model_selector;
pub mod approval_panel;
pub mod ai_notebook;
pub mod renderer;

use gtk::prelude::*;
use gtk::{Application, ApplicationWindow, Box as GtkBox, Orientation, Align, Button, Label, Stack};
use std::rc::Rc;
use std::cell::RefCell;
use std::time::Duration;

pub fn refresh_sidebar_chat_list(container: &GtkBox, store: &crate::chat_store::ChatStore, stack: &Stack, chat_store: Rc<RefCell<crate::chat_store::ChatStore>>, clear_handle: crate::ui::dashboard::ChatClearHandle, logger: Rc<RefCell<crate::logger::Logger>>) {
    let children = container.children();
    for child in &children {
        container.remove(child);
    }
    for chat in store.all_chats() {
        let row = GtkBox::new(Orientation::Horizontal, 4);
        row.style_context().add_class("chat-row");
        row.set_hexpand(true);

        let item_label = Label::new(Some(&chat.title));
        item_label.set_ellipsize(pango::EllipsizeMode::End);
        item_label.set_max_width_chars(20);
        item_label.set_halign(Align::Start);
        item_label.set_hexpand(true);
        item_label.style_context().add_class("chat-item-label");

        let item = Button::new();
        item.style_context().add_class("chat-item");
        item.set_halign(Align::Start);
        item.set_hexpand(true);
        item.add(&item_label);
        let s = stack.clone();
        let chat_id_load = chat.id.clone();
        let store_load = chat_store.clone();
        let clear_load = clear_handle.clone();
        item.connect_clicked(move |_| {
            store_load.borrow_mut().set_active(&chat_id_load);
            clear_load.clear();
            s.set_visible_child_name("chat");
        });
        row.pack_start(&item, true, true, 0);

        let edit_btn = chat_action_btn("✏️");
        let delete_btn = chat_action_btn("🗑");
        let share_btn = chat_action_btn("📤");

        row.pack_start(&edit_btn, false, false, 0);
        row.pack_start(&delete_btn, false, false, 0);
        row.pack_start(&share_btn, false, false, 0);

        let item_edit = item.clone();
        edit_btn.connect_clicked(move |_| {
            show_rename_dialog(&item_edit);
        });

        let row_delete = row.clone();
        let container_delete = container.clone();
        let chat_id_delete = chat.id.clone();
        let store_delete = chat_store.clone();
        let stack_refresh = stack.clone();
        let clear_delete = clear_handle.clone();
        let logger_delete = logger.clone();
        delete_btn.connect_clicked(move |_| {
            show_delete_confirm(&row_delete, &container_delete, chat_id_delete.clone(), store_delete.clone(), stack_refresh.clone(), clear_delete.clone(), logger_delete.clone());
        });

        share_btn.connect_clicked(move |_| {
            open_share_link();
        });

        container.pack_start(&row, false, false, 0);
    }
    container.show_all();
}

fn show_rename_dialog(item: &Button) {
    let item_clone = item.clone();
    crate::ui::dialog::show_rename_dialog(move |name: String| {
        if let Some(label) = item_clone.child().and_then(|c| c.downcast::<Label>().ok()) {
            label.set_text(&name);
        }
    });
}

fn show_delete_confirm(row: &GtkBox, container: &GtkBox, chat_id: String, chat_store: Rc<RefCell<crate::chat_store::ChatStore>>, stack: Stack, clear_handle: crate::ui::dashboard::ChatClearHandle, logger: Rc<RefCell<crate::logger::Logger>>) {
    let row_clone = row.clone();
    let container_clone = container.clone();
    let store_clone = chat_store.clone();
    let clear_clone = clear_handle.clone();
    let stack_clone = stack.clone();
    let logger_clone = logger.clone();
    let cid = chat_id.clone();
    crate::ui::dialog::show_delete_dialog(move || {
        {
            let mut store = store_clone.borrow_mut();
            let was_active = store.get_active().map(|c| c.id == cid).unwrap_or(false);
            store.delete_chat(&cid);
            logger_clone.borrow_mut().log(crate::logger::LogLevel::Warning, "Chat", &format!("Deleted chat: {}", cid));
            if was_active {
                drop(store);
                clear_clone.clear();
            }
        }
        container_clone.remove(&row_clone);
        refresh_sidebar_chat_list(&container_clone, &store_clone.borrow(), &stack_clone, store_clone.clone(), clear_clone.clone(), logger_clone.clone());
    });
}

fn open_share_link() {
    crate::ui::dialog::show_share_dialog();
}

fn build_save_screen() -> GtkBox {
    let screen = GtkBox::new(Orientation::Vertical, 16);
    screen.set_halign(Align::Center);
    screen.set_valign(Align::Center);
    screen.set_hexpand(true);
    screen.set_vexpand(true);
    screen.style_context().add_class("save-screen");

    let icon = Label::new(Some("💾"));
    icon.style_context().add_class("save-icon");
    screen.pack_start(&icon, false, false, 0);

    let title = Label::new(Some("Saving Session Files..."));
    title.style_context().add_class("save-title");
    screen.pack_start(&title, false, false, 0);

    let subtitle = Label::new(Some("Encrypting and securing your session logs.\nThis will only take a moment."));
    subtitle.style_context().add_class("save-subtitle");
    subtitle.set_justify(gtk::Justification::Center);
    screen.pack_start(&subtitle, false, false, 0);

    screen
}

fn chat_action_btn(label: &str) -> Button {
    let btn = Button::with_label(label);
    btn.style_context().add_class("chat-action-btn");
    btn.set_halign(Align::Center);
    btn
}

pub fn build_window(app: &Application) {
    let storage = Rc::new(RefCell::new(crate::storage::AppStorage::load()));
    let chat_store = Rc::new(RefCell::new(crate::chat_store::ChatStore::load()));
    let root = GtkBox::new(Orientation::Horizontal, 0);
    let main_overlay = gtk::Overlay::new();
    main_overlay.add(&root);

    let main_stack = Stack::new();
    main_stack.set_hexpand(true);
    main_stack.set_vexpand(true);

    let logger = Rc::new(RefCell::new(crate::logger::Logger::new()));
    let projects: Rc<RefCell<Vec<code_editor::Project>>> = Rc::new(RefCell::new(Vec::new()));
    let chat_list_container = Rc::new(RefCell::new(None::<GtkBox>));
    let chat_btn_nav_label: Rc<RefCell<Option<Label>>> = Rc::new(RefCell::new(None));

    let title_updater = {
        let label = chat_btn_nav_label.clone();
        move |text: String| {
            if let Some(ref lbl) = *label.borrow() {
                lbl.set_text(&text);
            }
        }
    };
    let reset_chat_title = {
        let label = chat_btn_nav_label.clone();
        move || {
            if let Some(ref lbl) = *label.borrow() {
                lbl.set_text("Chat");
            }
        }
    };

    let ai_notebook = Rc::new(RefCell::new(ai_notebook::build_ai_notebook(main_stack.clone())));
    let refresh_chats_cell: Rc<RefCell<Option<Box<dyn Fn()>>>> = Rc::new(RefCell::new(None));
    let refresh_chats_placeholder = refresh_chats_cell.clone();
    let (chat_view, preview_panel, clear_handle) = dashboard::build_chat_view(chat_store.clone(), logger.clone(), ai_notebook.clone(), Box::new(move || {
        if let Some(ref f) = *refresh_chats_placeholder.borrow() { f(); }
    }), Box::new(title_updater), Box::new(reset_chat_title));

    let store_for_refresh = chat_store.clone();
    let stack_for_refresh = main_stack.clone();
    let container_for_refresh = chat_list_container.clone();
    let clear_for_refresh = clear_handle.clone();
    let logger_refresh = logger.clone();
    *refresh_chats_cell.borrow_mut() = Some(Box::new(move || {
        if let Some(ref box_) = *container_for_refresh.borrow() {
            refresh_sidebar_chat_list(box_, &store_for_refresh.borrow(), &stack_for_refresh, store_for_refresh.clone(), clear_for_refresh.clone(), logger_refresh.clone());
        }
    }));
    let logs_view = logs::build_logs_view(logger.clone());
    let agentic_view = code_editor::build_agentic_view(projects.clone());

    let settings_view = settings::build_settings_view();

    main_stack.add_titled(&chat_view, "chat", "Chat");
    main_stack.add_titled(&agentic_view, "agentic", "Code Editor");
    main_stack.add_titled(&ai_notebook.borrow().container.clone(), "ai_notebook", "AI Notebook");
    main_stack.add_titled(&logs_view, "logs", "Logs");

    main_stack.add_titled(&settings_view, "settings", "Settings");

    let main_area = GtkBox::new(Orientation::Vertical, 0);
    main_area.set_hexpand(true);
    main_area.set_vexpand(true);
    main_area.style_context().add_class("main-area");
    main_area.pack_start(&main_stack, true, true, 0);

    let (sb, chat_list, chat_nav_label) = sidebar::build_sidebar(main_stack.clone(), storage.clone(), chat_store.clone(), clear_handle, logger.clone());
    *chat_list_container.borrow_mut() = Some(chat_list);
    *chat_btn_nav_label.borrow_mut() = chat_nav_label.borrow().clone();
    root.pack_start(&sb, false, false, 0);

    root.pack_start(&main_area, true, true, 0);
    root.pack_start(&preview_panel, false, false, 0);

    let window = ApplicationWindow::builder()
        .application(app)
        .title("Vibi Claw")
        .default_width(1280)
        .default_height(800)
        .build();

    // Approval panel
    let approval_panel = Rc::new(approval_panel::ApprovalPanel::new());
    approval_panel.revealer.set_halign(Align::End);
    approval_panel.revealer.set_valign(Align::Fill);
    main_overlay.add_overlay(&approval_panel.revealer);

    // Bell button fixed in top-right
    let bell_btn = Button::with_label("🔔");
    bell_btn.style_context().add_class("notification-bell-float");
    bell_btn.set_halign(Align::End);
    bell_btn.set_valign(Align::Start);
    bell_btn.set_margin_top(8);
    bell_btn.set_margin_end(12);
    main_overlay.add_overlay(&bell_btn);

    let ap_toggle = approval_panel.revealer.clone();
    bell_btn.connect_clicked(move |_| {
        let revealed = ap_toggle.reveals_child();
        ap_toggle.set_reveal_child(!revealed);
    });



    // Channel for approval requests from ai_bridge (using async channel)
    let (approval_tx, approval_rx) = async_channel::unbounded::<Vec<crate::types::Command>>();
    
    // Store transmitter globally for ai_bridge to use
    crate::notification_panel::set_approval_channel(approval_tx);

    // Listen for approval requests on the main thread
    let ap_panel = approval_panel.clone();
    gtk::glib::spawn_future_local(async move {
        while let Ok(commands) = approval_rx.recv().await {
            ap_panel.clear();
            for cmd in &commands {
                let tool = format!("{:?}", cmd.kind);
                let path = cmd.path.as_deref().unwrap_or("-");
                let detail = cmd.content.as_deref().unwrap_or("");
                let short: String = detail.chars().take(60).collect();
                ap_panel.add_card(&tool, path, &short, cmd.clone());
            }
            ap_panel.show();
            
            // Poll until all cards are resolved, then execute approved ones
            let panel = ap_panel.clone();
            let cmds = commands.clone();
            gtk::glib::spawn_future_local(async move {
                // Wait for user to resolve all cards
                loop {
                    if panel.all_resolved() && !panel.is_empty() {
                        break;
                    }
                    glib::timeout_future(std::time::Duration::from_millis(500)).await;
                }
                let approved = panel.get_approved_commands();
                let total = commands.len();
                let denied_count = total - approved.len();
                
                if denied_count > 0 && approved.is_empty() {
                    println!("[VibiClaw] ❌ All {} action(s) denied by user.", denied_count);
                }
                
                if !approved.is_empty() {
                    if denied_count > 0 {
                        println!("[VibiClaw] {} denied, executing {} approved.", denied_count, approved.len());
                    }
                    let sandbox_path = dirs::config_dir()
                        .unwrap_or_else(|| std::path::PathBuf::from("."))
                        .join("vibi-ai")
                        .join("sandbox");
                    if let Ok(executor) = crate::executor::Executor::new(
                        sandbox_path.to_str().unwrap(),
                        true,
                    ) {
                        let mut exec_cmds = approved;
                        for cmd in &mut exec_cmds {
                            executor.run(cmd);
                            println!("[VibiClaw] {}", 
                                if let crate::types::CommandStatus::Done = cmd.status {
                                    format!("✅ {}", cmd.output.as_deref().unwrap_or("Done"))
                                } else {
                                    format!("❌ {:?}", cmd.status)
                                }
                            );
                        }
                    }
                }
                panel.hide();
            });
        }
    });

    window.add(&main_overlay);

    let saved_theme = storage.borrow().theme.clone();
    if saved_theme == "dark" {
        window.style_context().remove_class("light");
    } else {
        window.style_context().add_class("light");
    }

    let logger_save = logger.clone();
    let window_save = window.clone();
    window.connect_delete_event(move |_, _| {
        let logger = logger_save.clone();
        logger.borrow().save_to_disk();
        false.into()
    });

    window.connect_key_press_event(move |w, k| {
        if k.keyval() == gdk::keys::constants::F11 {
            if let Some(win) = w.window() {
                if (win.state() & gdk::WindowState::FULLSCREEN).is_empty() {
                    w.fullscreen();
                } else {
                    w.unfullscreen();
                }
            }
        }
        false.into()
    });

    window.show_all();
    main_stack.set_visible_child_name("chat");
    preview_panel.hide();
}