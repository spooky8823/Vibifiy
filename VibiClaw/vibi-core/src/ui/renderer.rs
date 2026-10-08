use gtk::prelude::*;
use gtk::{Box as GtkBox, Button, Label, Orientation, Align, Stack};
use std::rc::Rc;
use std::cell::RefCell;
use crate::api::api_chat::ApiChat;
use crate::chat_store::ChatStore;

pub fn build_chat_renderer(
    view_stack: Stack,
    chat_title: Label,
    api_chat: Option<Rc<ApiChat>>,
    logger: Rc<RefCell<crate::logger::Logger>>,
    chat_store: Rc<RefCell<ChatStore>>,
    ai_bridge: Rc<crate::ai_bridge::AiBridge>,
) -> (GtkBox, Rc<RefCell<String>>) {
    let container = GtkBox::new(Orientation::Vertical, 0);
    container.set_hexpand(true);
    container.set_vexpand(true);

    let inner_stack = Stack::new();
    inner_stack.set_hexpand(true);
    inner_stack.set_vexpand(true);

    // Native chat page
    let native_chat = build_native_chat(api_chat, logger, chat_store);
    inner_stack.add_titled(&native_chat, "native", "Native");

    // Webview page
    let webview_page = GtkBox::new(Orientation::Vertical, 0);
    inner_stack.add_titled(&webview_page, "webview", "WebView");

    // Back button
    let back_btn = Button::with_label("← Back to Dashboard");
    back_btn.style_context().add_class("floating-back-btn");
    back_btn.set_halign(Align::Start);
    back_btn.set_margin_start(16);
    back_btn.set_margin_top(8);

    let stack_back = view_stack.clone();
    let title_back = chat_title.clone();
    back_btn.connect_clicked(move |_| {
        stack_back.set_visible_child_name("dashboard");
        title_back.set_text("VibiClaw");
    });

    container.pack_start(&back_btn, false, false, 0);
    container.pack_start(&inner_stack, true, true, 0);

    webview_page.pack_start(&ai_bridge.container, true, true, 0);

    let current_mode: Rc<RefCell<String>> = Rc::new(RefCell::new(String::from("native")));
    
    // Set default visible child
    inner_stack.set_visible_child_name("native");

    (container, current_mode)
}

fn build_native_chat(
    api_chat: Option<Rc<ApiChat>>,
    logger: Rc<RefCell<crate::logger::Logger>>,
    _chat_store: Rc<RefCell<ChatStore>>,
) -> GtkBox {
    let container = GtkBox::new(Orientation::Vertical, 0);
    container.set_hexpand(true);
    container.set_vexpand(true);

    let scroll = gtk::ScrolledWindow::new(None::<&gtk::Adjustment>, None::<&gtk::Adjustment>);
    scroll.set_vexpand(true);
    scroll.set_policy(gtk::PolicyType::Never, gtk::PolicyType::Automatic);

    let message_area = GtkBox::new(Orientation::Vertical, 8);
    message_area.set_vexpand(true);
    message_area.set_valign(Align::Start);
    message_area.set_margin_start(24);
    message_area.set_margin_end(24);
    message_area.set_margin_top(16);
    message_area.set_margin_bottom(16);
    scroll.add(&message_area);
    container.pack_start(&scroll, true, true, 0);

    let input_area = GtkBox::new(Orientation::Horizontal, 8);
    input_area.set_margin_start(24);
    input_area.set_margin_end(24);
    input_area.set_margin_bottom(16);
    input_area.style_context().add_class("input-box");

    let entry = gtk::Entry::new();
    entry.set_placeholder_text(Some("Type your message..."));
    entry.style_context().add_class("chat-entry");
    entry.set_hexpand(true);
    input_area.pack_start(&entry, true, true, 0);

    let send_btn = Button::with_label("➤");
    send_btn.style_context().add_class("send-btn");
    input_area.pack_start(&send_btn, false, false, 0);

    container.pack_start(&input_area, false, false, 0);

    if let Some(ref api) = api_chat {
        let api = api.clone();
        let msg_area = message_area.clone();
        let entry_clone = entry.clone();
        let logger = logger.clone();
        let scroll_clone = scroll.clone();

        send_btn.connect_clicked(move |_| {
            let text = entry_clone.text().to_string();
            if text.trim().is_empty() { return; }

            let user_row = GtkBox::new(Orientation::Horizontal, 0);
            user_row.set_halign(Align::End);
            user_row.set_margin_bottom(4);
            let user_bubble = Label::new(Some(&text));
            user_bubble.set_wrap(true);
            user_bubble.set_max_width_chars(50);
            user_bubble.style_context().add_class("user-bubble");
            user_row.pack_start(&user_bubble, false, false, 0);
            msg_area.pack_start(&user_row, false, false, 0);
            user_row.show_all();
            entry_clone.set_text("");

            let api_clone = api.clone();
            let msg_clone = msg_area.clone();
            let msg_clone2 = msg_area.clone();
            let logger_clone = logger.clone();
            let scroll2 = scroll_clone.clone();
            let scroll3 = scroll.clone();
            let text_clone = text.clone();

            gtk::glib::spawn_future_local(async move {
                let result = api_clone.send_message(&text_clone);
                gtk::glib::idle_add_local(move || {
                    match &result {
                        Ok(response) => {
                            logger_clone.borrow_mut().log(
                                crate::logger::LogLevel::Info, "API",
                                &format!("Response: {} tokens", response.tokens_used),
                            );
                            let vibi_results = ApiChat::execute_vibi_blocks(&response.content);
                            let content = response.content.clone();
                            let ai_row = GtkBox::new(Orientation::Horizontal, 0);
                            ai_row.set_halign(Align::Start);
                            ai_row.set_margin_bottom(4);
                            let ai_bubble = Label::new(Some(&content));
                            ai_bubble.set_wrap(true);
                            ai_bubble.set_max_width_chars(50);
                            ai_bubble.style_context().add_class("ai-bubble");
                            ai_row.pack_start(&ai_bubble, false, false, 0);
                            msg_clone2.pack_start(&ai_row, false, false, 0);
                            ai_row.show_all();
                            for r in &vibi_results {
                                let r_row = GtkBox::new(Orientation::Horizontal, 0);
                                r_row.set_halign(Align::Start);
                                let r_label = Label::new(Some(r));
                                r_label.style_context().add_class("vibi-result");
                                r_row.pack_start(&r_label, false, false, 0);
                                msg_clone2.pack_start(&r_row, false, false, 0);
                                r_row.show_all();
                            }
                            let adj = scroll3.vadjustment();
                            adj.set_value(adj.upper());
                        }
                        Err(e) => {
                            let err_label = Label::new(Some(&format!("Error: {}", e)));
                            err_label.style_context().add_class("vibi-error");
                            msg_clone.pack_start(&err_label, false, false, 0);
                            err_label.show_all();
                        }
                    }
                    let adj = scroll2.vadjustment();
                    adj.set_value(adj.upper());
                    gtk::glib::ControlFlow::Break
                });
            });
        });

        entry.connect_activate({
            let btn = send_btn.clone();
            move |_| btn.emit_clicked()
        });
    }

    container
}