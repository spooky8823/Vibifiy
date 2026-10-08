use gtk::prelude::*;
use gtk::{Box as GtkBox, Button, Label, Orientation, Align, Separator, Stack};
use crate::chat_store::ChatStore;
use crate::ui::ai_notebook::AiNotebook;
use std::rc::Rc;
use std::cell::RefCell;
use crate::api::api_chat::ApiChat;
use chrono::Timelike;
use pango;

#[derive(Clone)]
pub struct ChatClearHandle {
    clear_fn: Rc<RefCell<Option<Box<dyn Fn()>>>>,
}

impl ChatClearHandle {
    pub fn new() -> Self {
        ChatClearHandle { clear_fn: Rc::new(RefCell::new(None)) }
    }
    pub fn set(&self, f: Box<dyn Fn()>) {
        *self.clear_fn.borrow_mut() = Some(f);
    }
    pub fn clear(&self) {
        if let Some(ref f) = *self.clear_fn.borrow() {
            f();
        }
    }
}

pub fn build_chat_view(
    chat_store: Rc<RefCell<ChatStore>>,
    logger: Rc<RefCell<crate::logger::Logger>>,
    _ai_notebook: Rc<RefCell<AiNotebook>>,
    _on_chat_update: Box<dyn Fn()>,
    _on_title_change: Box<dyn Fn(String)>,
    _on_reset_title: Box<dyn Fn()>,
) -> (GtkBox, GtkBox, ChatClearHandle) {
    // Check if API mode is enabled
    let storage = crate::storage::AppStorage::load();
    let api_mode = storage.api_mode;
    let openai_key = storage.openai_api_key.clone();
    let gemini_key = storage.gemini_api_key.clone();

    let api_chat = if api_mode {
        Some(Rc::new(crate::api::api_chat::ApiChat::new(openai_key, gemini_key)))
    } else {
        None
    };

    let root = GtkBox::new(Orientation::Vertical, 0);
    root.set_hexpand(true);
    root.set_vexpand(true);

    let topbar = GtkBox::new(Orientation::Horizontal, 0);
    topbar.style_context().add_class("topbar");
    topbar.set_margin_start(24);
    topbar.set_margin_end(24);
    topbar.set_margin_top(14);
    topbar.set_margin_bottom(14);

    let chat_title = Label::new(Some("VibiClaw"));
    chat_title.style_context().add_class("topbar-title");
    topbar.pack_start(&chat_title, false, false, 0);



    let divider = Separator::new(Orientation::Horizontal);
    divider.style_context().add_class("topbar-divider");

    root.pack_start(&topbar, false, false, 0);
    root.pack_start(&divider, false, false, 0);

    // Stack: dashboard or webview
    let view_stack = Stack::new();
    view_stack.set_hexpand(true);
    view_stack.set_vexpand(true);

    // AI Bridge (webview lives here)
    let ai_bridge = crate::ai_bridge::AiBridge::new();
    let ai_bridge_rc = Rc::new(ai_bridge);
    ai_bridge_rc.container.set_hexpand(true);
    ai_bridge_rc.container.set_vexpand(true);

    // Clone what we need before moving into closures
    let view_stack_dash = view_stack.clone();
    let chat_title_dash = chat_title.clone();
    let api_chat_dash = api_chat.clone();
    let _bridge_send = ai_bridge_rc.clone();
    let bridge_switch = ai_bridge_rc.clone();

    // === RENDERER PAGE (handles both native chat and webview) ===
    let (renderer_view, renderer_mode) = crate::ui::renderer::build_chat_renderer(
        view_stack.clone(),
        chat_title.clone(),
        api_chat.clone(),
        logger.clone(),
        chat_store.clone(),
        ai_bridge_rc.clone(),
    );
    
    // === DASHBOARD PAGE (added first so it shows by default) ===
    let dashboard = build_dashboard(chat_store.clone(), view_stack_dash.clone(), bridge_switch.clone(), chat_title_dash.clone(), api_chat_dash, renderer_mode.clone());
    view_stack.add_titled(&dashboard, "dashboard", "Dashboard");
    view_stack.add_titled(&renderer_view, "renderer", "Renderer");

    root.pack_start(&view_stack, true, true, 0);
    ai_bridge_rc.container.set_hexpand(true);
    ai_bridge_rc.container.set_vexpand(true);





    view_stack.set_visible_child_name("dashboard");

    let clear_handle = ChatClearHandle::new();
    let store_for_clear = chat_store.clone();
    let chat_title_for_clear = chat_title.clone();
    clear_handle.set(Box::new(move || {
        if let Some(active) = store_for_clear.borrow().get_active() {
            chat_title_for_clear.set_text(&active.title);
        } else {
            chat_title_for_clear.set_text("VibiClaw");
        }
    }));

    let preview_panel = GtkBox::new(Orientation::Vertical, 0);
    preview_panel.set_width_request(0);
    preview_panel.set_visible(false);

    (root, preview_panel, clear_handle)
}

fn build_dynamic_greeting() -> Label {
    let hour = chrono::Local::now().hour();
    let text = if hour < 4 { "🌙 Early Night" }
    else if hour < 7 { "🌄 Early Morning" }
    else if hour < 12 { "☀️ Good Morning" }
    else if hour < 17 { "🌤️ Good Afternoon" }
    else if hour < 19 { "🌅 Early Evening" }
    else if hour < 21 { "🌆 Good Evening" }
    else { "🌙 Good Night" };

    let label = Label::new(Some(text));
    label.style_context().add_class("dashboard-greeting");
    label.set_halign(Align::Center);
    label
}

fn build_motivational_quote() -> Label {
    let quotes = vec![
        "The only way to do great work is to love what you do.",
        "Innovation distinguishes between a leader and a follower.",
        "Stay hungry, stay foolish.",
        "Code is like humor. When you have to explain it, it's bad.",
        "First, solve the problem. Then, write the code.",
        "Experience is the name everyone gives to their mistakes.",
        "The best way to predict the future is to invent it.",
        "Simplicity is the soul of efficiency.",
        "Make it work, make it right, make it fast.",
        "Programs must be written for people to read.",
    ];
    let idx = chrono::Local::now().timestamp() as usize % quotes.len();
    let label = Label::new(Some(&format!("\"{}\"", quotes[idx])));
    label.style_context().add_class("dashboard-quote");
    label.set_halign(Align::Center);
    label.set_wrap(true);
    label.set_max_width_chars(60);
    label
}

fn build_date_label() -> Label {
    let now = chrono::Local::now();
    let text = now.format("%A, %B %e, %Y").to_string();
    let label = Label::new(Some(&text));
    label.style_context().add_class("dashboard-date");
    label.set_halign(Align::Center);
    label
}

fn build_live_clock() -> Label {
    let now = chrono::Local::now();
    let text = now.format("%H:%M:%S").to_string();
    let label = Label::new(Some(&text));
    label.style_context().add_class("dashboard-clock");
    label.set_halign(Align::Center);

    let show_seconds = Rc::new(RefCell::new(true));
    let show_sec = show_seconds.clone();
    let clock_label = label.clone();

    gtk::glib::timeout_add_local(std::time::Duration::from_millis(1000), move || {
        let now = chrono::Local::now();
        let text = if *show_sec.borrow() {
            now.format("%H:%M:%S").to_string()
        } else {
            now.format("%H:%M").to_string()
        };
        clock_label.set_text(&text);
        gtk::glib::ControlFlow::Continue
    });

    let show_sec_click = show_seconds.clone();
    label.connect_button_press_event(move |_, _| {
        let current = *show_sec_click.borrow();
        *show_sec_click.borrow_mut() = !current;
        false.into()
    });

    label
}

fn build_model_search() -> gtk::Entry {
    let entry = gtk::Entry::new();
    entry.set_placeholder_text(Some("Search AI models..."));
    entry.style_context().add_class("dashboard-search");
    entry.set_halign(Align::Center);
    entry.set_size_request(400, 36);
    entry
}

fn build_section_header(title: &str) -> Label {
    let label = Label::new(Some(title));
    label.style_context().add_class("dashboard-section-header");
    label.set_halign(Align::Start);
    label.set_margin_top(10);
    label
}

fn build_webview_section(cards_registry: &Rc<RefCell<Vec<(String, Button)>>>, view_stack: Stack, bridge: Rc<crate::ai_bridge::AiBridge>, chat_title: Label, _api_chat: Option<Rc<ApiChat>>, _chat_store: Rc<RefCell<ChatStore>>, renderer_mode: Rc<RefCell<String>>) -> GtkBox {
    let section = GtkBox::new(Orientation::Vertical, 10);
    section.pack_start(&build_section_header("🌐 WebView Models"), false, false, 0);

    let flow = gtk::FlowBox::new();
    flow.set_selection_mode(gtk::SelectionMode::None);
    flow.set_min_children_per_line(3);
    flow.set_max_children_per_line(7);
    flow.set_homogeneous(true);
    flow.set_row_spacing(10);
    flow.set_column_spacing(10);

    let models = vec![
        ("chatgpt", "🟢", "ChatGPT", "openai.com", "OpenAI"),
        ("gemini", "🔵", "Gemini", "gemini.google.com", "Google"),
    ];

    for (id, emoji, name, domain, company) in &models {
        let id_clone = id.to_string();
        let name_clone = name.to_string();
        let bridge_clone = bridge.clone();
        let stack_clone = view_stack.clone();
        let title_clone = chat_title.clone();
        let mode = renderer_mode.clone();
        let card = build_model_card(emoji, name, domain, company, "webview", id, Box::new(move || {
            bridge_clone.load_model(&id_clone);
            *mode.borrow_mut() = String::from("webview");
            title_clone.set_text(&name_clone);
            stack_clone.set_visible_child_name("renderer");
        }));
        cards_registry.borrow_mut().push((name.to_lowercase(), card.clone()));
        flow.insert(&card, -1);
    }

    section.pack_start(&flow, false, false, 0);
    section
}

fn build_official_api_section(cards_registry: &Rc<RefCell<Vec<(String, Button)>>>, view_stack: Stack, _bridge: Rc<crate::ai_bridge::AiBridge>, chat_title: Label, api_chat: Option<Rc<ApiChat>>, chat_store: Rc<RefCell<ChatStore>>, renderer_mode: Rc<RefCell<String>>) -> GtkBox {
    let section = GtkBox::new(Orientation::Vertical, 10);
    section.pack_start(&build_section_header("🔑 Official API Models"), false, false, 0);

    let flow = gtk::FlowBox::new();
    flow.set_selection_mode(gtk::SelectionMode::None);
    flow.set_min_children_per_line(3);
    flow.set_max_children_per_line(7);
    flow.set_homogeneous(true);
    flow.set_row_spacing(10);
    flow.set_column_spacing(10);

    let models = vec![
        ("gpt-4o", "🟢", "GPT-4o", "api.openai.com", "OpenAI"),
        ("gpt-4", "🟢", "GPT-4", "api.openai.com", "OpenAI"),
        ("gpt-3.5-turbo", "🟢", "GPT-3.5", "api.openai.com", "OpenAI"),
        ("gemini-2.5-pro", "🔵", "Gemini 2.5 Pro", "api.google.com", "Google"),
        ("gemini-2.5-flash", "🔵", "Gemini 2.5 Flash", "api.google.com", "Google"),
        ("claude-opus", "🟠", "Claude Opus", "api.anthropic.com", "Anthropic"),
        ("claude-sonnet", "🟠", "Claude Sonnet", "api.anthropic.com", "Anthropic"),
        ("deepseek-v3", "🐋", "DeepSeek V3", "api.deepseek.com", "DeepSeek"),
        ("deepseek-r1", "🐋", "DeepSeek R1", "api.deepseek.com", "DeepSeek"),
    ];

    for (id, emoji, name, domain, company) in &models {
        let id_clone = id.to_string();
        let name_clone = name.to_string();
        let api_clone = api_chat.clone();
        let stack_clone = view_stack.clone();
        let title_clone = chat_title.clone();
        let store_clone = chat_store.clone();
        let mode = renderer_mode.clone();
        let card = build_model_card(emoji, name, domain, company, "official", id, Box::new(move || {
            if let Some(ref api) = api_clone {
                api.set_model(&id_clone);
                api.clear_history();
            }
            *mode.borrow_mut() = String::from("native");
            store_clone.borrow_mut().set_active("");
            title_clone.set_text(&name_clone);
            stack_clone.set_visible_child_name("renderer");
        }));
        cards_registry.borrow_mut().push((name.to_lowercase(), card.clone()));
        flow.insert(&card, -1);
    }

    section.pack_start(&flow, false, false, 0);
    section
}

fn build_unofficial_api_section(cards_registry: &Rc<RefCell<Vec<(String, Button)>>>, view_stack: Stack, _bridge: Rc<crate::ai_bridge::AiBridge>, chat_title: Label, api_chat: Option<Rc<ApiChat>>, chat_store: Rc<RefCell<ChatStore>>, renderer_mode: Rc<RefCell<String>>) -> GtkBox {
    let section = GtkBox::new(Orientation::Vertical, 10);
    section.pack_start(&build_section_header("🔓 Unofficial API Models"), false, false, 0);

    let flow = gtk::FlowBox::new();
    flow.set_selection_mode(gtk::SelectionMode::None);
    flow.set_min_children_per_line(3);
    flow.set_max_children_per_line(7);
    flow.set_homogeneous(true);
    flow.set_row_spacing(10);
    flow.set_column_spacing(10);

    let models = vec![
        ("openrouter", "🔗", "OpenRouter", "openrouter.ai", "Multi-Provider"),
        ("groq", "⚡", "Groq", "api.groq.com", "Groq"),
        ("together", "🤝", "Together AI", "api.together.xyz", "Together"),
    ];

    for (id, emoji, name, domain, company) in &models {
        let id_clone = id.to_string();
        let name_clone = name.to_string();
        let api_clone = api_chat.clone();
        let stack_clone = view_stack.clone();
        let title_clone = chat_title.clone();
        let store_clone = chat_store.clone();
        let mode = renderer_mode.clone();
        let card = build_model_card(emoji, name, domain, company, "unofficial", id, Box::new(move || {
            if let Some(ref api) = api_clone {
                api.set_model(&id_clone);
                api.clear_history();
            }
            *mode.borrow_mut() = String::from("native");
            store_clone.borrow_mut().set_active("");
            title_clone.set_text(&name_clone);
            stack_clone.set_visible_child_name("renderer");
        }));
        cards_registry.borrow_mut().push((name.to_lowercase(), card.clone()));
        flow.insert(&card, -1);
    }

    section.pack_start(&flow, false, false, 0);
    section
}

fn build_local_models_section(cards_registry: &Rc<RefCell<Vec<(String, Button)>>>, view_stack: Stack, _bridge: Rc<crate::ai_bridge::AiBridge>, chat_title: Label, api_chat: Option<Rc<ApiChat>>, chat_store: Rc<RefCell<ChatStore>>, renderer_mode: Rc<RefCell<String>>) -> GtkBox {
    let section = GtkBox::new(Orientation::Vertical, 10);
    section.pack_start(&build_section_header("💻 Local Models"), false, false, 0);

    let flow = gtk::FlowBox::new();
    flow.set_selection_mode(gtk::SelectionMode::None);
    flow.set_min_children_per_line(3);
    flow.set_max_children_per_line(7);
    flow.set_homogeneous(true);
    flow.set_row_spacing(10);
    flow.set_column_spacing(10);

    let models = vec![
        ("ollama", "🦙", "Ollama", "localhost:11434", "Local Runtime"),
        ("lmstudio", "🖥️", "LM Studio", "localhost:1234", "Local Runtime"),
    ];

    for (id, emoji, name, domain, company) in &models {
        let id_clone = id.to_string();
        let name_clone = name.to_string();
        let api_clone = api_chat.clone();
        let stack_clone = view_stack.clone();
        let title_clone = chat_title.clone();
        let store_clone = chat_store.clone();
        let mode = renderer_mode.clone();
        let card = build_model_card(emoji, name, domain, company, "local", id, Box::new(move || {
            if let Some(ref api) = api_clone {
                api.set_model(&id_clone);
                api.clear_history();
            }
            *mode.borrow_mut() = String::from("native");
            store_clone.borrow_mut().set_active("");
            title_clone.set_text(&name_clone);
            stack_clone.set_visible_child_name("renderer");
        }));
        cards_registry.borrow_mut().push((name.to_lowercase(), card.clone()));
        flow.insert(&card, -1);
    }

    let scan_btn = Button::with_label("🔍 Scan for local models");
    scan_btn.style_context().add_class("dashboard-scan-btn");
    scan_btn.set_halign(Align::Center);
    section.pack_start(&scan_btn, false, false, 0);

    section.pack_start(&flow, false, false, 0);
    section
}

fn build_cloud_gpu_section(cards_registry: &Rc<RefCell<Vec<(String, Button)>>>, view_stack: Stack, _bridge: Rc<crate::ai_bridge::AiBridge>, chat_title: Label, api_chat: Option<Rc<ApiChat>>, chat_store: Rc<RefCell<ChatStore>>, renderer_mode: Rc<RefCell<String>>) -> GtkBox {
    let section = GtkBox::new(Orientation::Vertical, 10);
    section.pack_start(&build_section_header("☁️ Cloud GPU Models"), false, false, 0);

    let flow = gtk::FlowBox::new();
    flow.set_selection_mode(gtk::SelectionMode::None);
    flow.set_min_children_per_line(3);
    flow.set_max_children_per_line(7);
    flow.set_homogeneous(true);
    flow.set_row_spacing(10);
    flow.set_column_spacing(10);

    let models = vec![
        ("kaggle", "📊", "Kaggle GPU", "kaggle.com", "Kaggle"),
        ("colab", "🔬", "Google Colab", "colab.research.google.com", "Google"),
        ("runpod", "🚀", "RunPod", "runpod.io", "RunPod"),
        ("lambdalabs", "☁️", "Lambda Labs", "lambdalabs.com", "Lambda"),
    ];

    for (id, emoji, name, domain, company) in &models {
        let id_clone = id.to_string();
        let name_clone = name.to_string();
        let api_clone = api_chat.clone();
        let stack_clone = view_stack.clone();
        let title_clone = chat_title.clone();
        let store_clone = chat_store.clone();
        let mode = renderer_mode.clone();
        let card = build_model_card(emoji, name, domain, company, "cloud", id, Box::new(move || {
            if let Some(ref api) = api_clone {
                api.set_model(&id_clone);
                api.clear_history();
            }
            *mode.borrow_mut() = String::from("native");
            store_clone.borrow_mut().set_active("");
            title_clone.set_text(&name_clone);
            stack_clone.set_visible_child_name("renderer");
        }));
        cards_registry.borrow_mut().push((name.to_lowercase(), card.clone()));
        flow.insert(&card, -1);
    }

    section.pack_start(&flow, false, false, 0);
    section
}

fn build_recent_chats_section(chat_store: Rc<RefCell<ChatStore>>, view_stack: Stack, chat_title: Label, renderer_mode: Rc<RefCell<String>>) -> GtkBox {
    let section = GtkBox::new(Orientation::Vertical, 10);
    section.pack_start(&build_section_header("💬 Recent Chats"), false, false, 0);

    let flow = gtk::FlowBox::new();
    flow.set_selection_mode(gtk::SelectionMode::None);
    flow.set_min_children_per_line(2);
    flow.set_max_children_per_line(5);
    flow.set_homogeneous(true);
    flow.set_row_spacing(10);
    flow.set_column_spacing(10);

    let store = chat_store.borrow();
    let chats = store.all_chats();
    for chat in chats.iter().take(8) {
        let title = chat.title.clone();
        let model = chat.model.clone();
        let id = chat.id.clone();
        let stack = view_stack.clone();
        let tlabel = chat_title.clone();
        let store = chat_store.clone();
        let card = build_recent_chat_card(&title, &model, &id);
        let mode = renderer_mode.clone();
        card.connect_clicked(move |_| {
            *mode.borrow_mut() = String::from("native");
            store.borrow_mut().set_active(&id);
            tlabel.set_text(&title);
            stack.set_visible_child_name("renderer");
        });
        flow.insert(&card, -1);
    }

    if chats.is_empty() {
        let empty_label = Label::new(Some("No recent chats yet. Start a conversation!"));
        empty_label.style_context().add_class("dashboard-empty-text");
        empty_label.set_halign(Align::Start);
        section.pack_start(&empty_label, false, false, 0);
    }

    section.pack_start(&flow, false, false, 0);
    section
}

fn build_model_card(emoji: &str, name: &str, _domain: &str, company: &str, model_type: &str, model_id: &str, on_click: Box<dyn Fn() + 'static>) -> Button {
    let card = Button::new();
    card.style_context().add_class("dashboard-model-card");
    card.set_size_request(170, 120);

    let content = GtkBox::new(Orientation::Vertical, 8);
    content.set_margin_top(12);
    content.set_margin_bottom(12);
    content.set_margin_start(10);
    content.set_margin_end(10);

    let dash_icon = match model_id {
        "chatgpt" | "gpt-4o" | "gpt-4" | "gpt-3.5-turbo" => "chatgpt-icon.png",
        "gemini" | "gemini-2.5-pro" | "gemini-2.5-flash" | "gemini-1.5-pro" => "google-gemini-icon.png",
        "claude" | "claude-opus" | "claude-sonnet" => "claude-ai-icon.png",
        "deepseek" | "deepseek-v3" | "deepseek-r1" | "deepseek-api" => "deepseek-logo-icon.png",
        "qwen" => "qwen-ai-icon.png",
        "kimi" => "kimi-ai-icon.png",
        "kaggle" => "kaggle-icon.png",
        "colab" => "google-colab-icon.png",
        _ => "",
    };
    let icon_path = if !dash_icon.is_empty() {
        format!("src/icons/{}", dash_icon)
    } else {
        format!("src/icons/{}-icon.png", model_id)
    };
    let icon_widget: GtkBox = if std::path::Path::new(&icon_path).exists() {
        if let Ok(pixbuf) = gdk_pixbuf::Pixbuf::from_file_at_scale(&icon_path, 28, 28, true) {
            let img = gtk::Image::from_pixbuf(Some(&pixbuf));
            img.set_halign(Align::Center);
            let box_wrapper = GtkBox::new(Orientation::Horizontal, 0);
            box_wrapper.set_halign(Align::Center);
            box_wrapper.pack_start(&img, false, false, 0);
            box_wrapper
        } else {
            let fallback = GtkBox::new(Orientation::Horizontal, 0);
            fallback.set_halign(Align::Center);
            fallback
        }
    } else {
        let emoji_label = Label::new(Some(emoji));
        emoji_label.style_context().add_class("dashboard-model-icon");
        emoji_label.set_halign(Align::Center);
        let box_wrapper = GtkBox::new(Orientation::Horizontal, 0);
        box_wrapper.set_halign(Align::Center);
        box_wrapper.pack_start(&emoji_label, false, false, 0);
        box_wrapper
    };
    content.pack_start(&icon_widget, false, false, 0);

    let name_label = Label::new(Some(name));
    name_label.style_context().add_class("dashboard-model-name");
    name_label.set_halign(Align::Center);
    content.pack_start(&name_label, false, false, 0);

    let company_label = Label::new(Some(company));
    company_label.style_context().add_class("dashboard-model-company");
    company_label.set_halign(Align::Center);
    content.pack_start(&company_label, false, false, 0);

    let type_label = Label::new(Some(match model_type {
        "webview" => "Web",
        "official" => "API",
        "unofficial" => "Unofficial",
        "local" => "Local",
        "cloud" => "Cloud",
        _ => "",
    }));
    type_label.style_context().add_class("dashboard-model-type");
    type_label.set_halign(Align::Center);
    content.pack_start(&type_label, false, false, 0);

    card.connect_clicked(move |_| on_click());
    card.add(&content);
    card
}

fn build_recent_chat_card(title: &str, model: &str, _chat_id: &str) -> Button {
    let card = Button::new();
    card.style_context().add_class("dashboard-recent-card");
    card.set_size_request(200, 80);

    let content = GtkBox::new(Orientation::Vertical, 6);
    content.set_margin_top(10);
    content.set_margin_bottom(10);
    content.set_margin_start(12);
    content.set_margin_end(12);

    let title_label = Label::new(Some(title));
    title_label.style_context().add_class("dashboard-recent-title");
    title_label.set_halign(Align::Start);
    title_label.set_ellipsize(pango::EllipsizeMode::End);
    title_label.set_max_width_chars(22);
    content.pack_start(&title_label, false, false, 0);

    let model_label = Label::new(Some(model));
    model_label.style_context().add_class("dashboard-recent-model");
    model_label.set_halign(Align::Start);
    content.pack_start(&model_label, false, false, 0);

    card.add(&content);
    card
}

fn build_dashboard(
    chat_store: Rc<RefCell<ChatStore>>,
    view_stack: Stack,
    bridge_switch: Rc<crate::ai_bridge::AiBridge>,
    chat_title: Label,
    api_chat: Option<Rc<ApiChat>>,
    renderer_mode: Rc<RefCell<String>>,
) -> GtkBox {
    let container = GtkBox::new(Orientation::Vertical, 0);
    container.set_hexpand(true);
    container.set_vexpand(true);

    let scroll = gtk::ScrolledWindow::new(None::<&gtk::Adjustment>, None::<&gtk::Adjustment>);
    scroll.set_vexpand(true);
    scroll.set_policy(gtk::PolicyType::Never, gtk::PolicyType::Automatic);

    let content = GtkBox::new(Orientation::Vertical, 16);
    content.set_halign(Align::Center);
    content.set_margin_start(40);
    content.set_margin_end(40);
    content.set_margin_top(30);
    content.set_margin_bottom(40);

    let greeting = build_dynamic_greeting();
    content.pack_start(&greeting, false, false, 0);

    let quote = build_motivational_quote();
    content.pack_start(&quote, false, false, 0);

    let date_label = build_date_label();
    content.pack_start(&date_label, false, false, 0);

    let clock = build_live_clock();
    content.pack_start(&clock, false, false, 0);

    let cards_registry: Rc<RefCell<Vec<(String, Button)>>> = Rc::new(RefCell::new(Vec::new()));

    let search_bar = build_model_search();
    let registry_clone = cards_registry.clone();
    search_bar.connect_changed(move |entry| {
        let query = entry.text().to_lowercase();
        for (name, card) in registry_clone.borrow().iter() {
            card.set_visible(query.is_empty() || name.contains(&query));
        }
    });
    content.pack_start(&search_bar, false, false, 0);

    content.pack_start(&build_webview_section(&cards_registry, view_stack.clone(), bridge_switch.clone(), chat_title.clone(), api_chat.clone(), chat_store.clone(), renderer_mode.clone()), false, false, 0);
    content.pack_start(&build_official_api_section(&cards_registry, view_stack.clone(), bridge_switch.clone(), chat_title.clone(), api_chat.clone(), chat_store.clone(), renderer_mode.clone()), false, false, 0);
    content.pack_start(&build_unofficial_api_section(&cards_registry, view_stack.clone(), bridge_switch.clone(), chat_title.clone(), api_chat.clone(), chat_store.clone(), renderer_mode.clone()), false, false, 0);
    content.pack_start(&build_local_models_section(&cards_registry, view_stack.clone(), bridge_switch.clone(), chat_title.clone(), api_chat.clone(), chat_store.clone(), renderer_mode.clone()), false, false, 0);
    content.pack_start(&build_cloud_gpu_section(&cards_registry, view_stack.clone(), bridge_switch.clone(), chat_title.clone(), api_chat.clone(), chat_store.clone(), renderer_mode.clone()), false, false, 0);
    content.pack_start(&build_recent_chats_section(chat_store, view_stack, chat_title, renderer_mode.clone()), false, false, 0);

    scroll.add(&content);
    container.pack_start(&scroll, true, true, 0);
    container
}

fn build_ai_card(emoji: &str, name: &str, desc: &str) -> Button {
    let card = Button::new();
    card.style_context().add_class("ai-dashboard-card");
    card.set_size_request(180, 140);

    let content = GtkBox::new(Orientation::Vertical, 10);
    content.set_margin_top(16);
    content.set_margin_bottom(16);

    let icon = Label::new(Some(emoji));
    icon.style_context().add_class("ai-dashboard-icon");
    icon.set_halign(Align::Center);
    content.pack_start(&icon, false, false, 0);

    let name_label = Label::new(Some(name));
    name_label.style_context().add_class("ai-dashboard-name");
    name_label.set_halign(Align::Center);
    content.pack_start(&name_label, false, false, 0);

    let desc_label = Label::new(Some(desc));
    desc_label.style_context().add_class("ai-dashboard-desc");
    desc_label.set_halign(Align::Center);
    content.pack_start(&desc_label, false, false, 0);

    card.add(&content);
    card
}

fn build_api_chat_page(
    api_chat: Option<Rc<ApiChat>>,
    logger: Rc<RefCell<crate::logger::Logger>>,
    _chat_store: Rc<RefCell<ChatStore>>,
    view_stack: Stack,
    chat_title: Label,
) -> GtkBox {
    let container = GtkBox::new(Orientation::Vertical, 0);
    container.set_hexpand(true);
    container.set_vexpand(true);

    let back_btn = Button::with_label("← Back");
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

    // Input bar
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

    // Send handler
    if let Some(ref api) = api_chat {
        let api = api.clone();
        let msg_area = message_area.clone();
        let entry_clone = entry.clone();
        let logger = logger.clone();
        let scroll_clone = scroll.clone();

        send_btn.connect_clicked(move |_| {
            let text = entry_clone.text().to_string();
            if text.trim().is_empty() { return; }

            // User bubble
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

            // Send to API
            let api_clone = api.clone();
            let msg_area_clone = msg_area.clone();
            let msg_area_clone2 = msg_area.clone();
            let logger_clone = logger.clone();
            let scroll_clone2 = scroll_clone.clone();
            let scroll_clone3 = scroll.clone();
            let text_clone = text.clone();
            gtk::glib::spawn_future_local(async move {
                let result = api_clone.send_message(&text_clone);
                gtk::glib::idle_add_local(move || {
                    match &result {
                        Ok(response) => {
                            logger_clone.borrow_mut().log(
                                crate::logger::LogLevel::Info,
                                "API",
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
                            msg_area_clone2.pack_start(&ai_row, false, false, 0);
                            ai_row.show_all();

                            for result in &vibi_results {
                                let result_row = GtkBox::new(Orientation::Horizontal, 0);
                                result_row.set_halign(Align::Start);
                                let result_label = Label::new(Some(result));
                                result_label.style_context().add_class("vibi-result");
                                result_row.pack_start(&result_label, false, false, 0);
                                msg_area_clone2.pack_start(&result_row, false, false, 0);
                                result_row.show_all();
                            }

                            let adj = scroll_clone3.vadjustment();
                            adj.set_value(adj.upper());
                        }
                        Err(e) => {
                            let error_label = Label::new(Some(&format!("Error: {}", e)));
                            error_label.style_context().add_class("vibi-error");
                            msg_area_clone.pack_start(&error_label, false, false, 0);
                            error_label.show_all();
                        }
                    }
                    let adj = scroll_clone2.vadjustment();
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
