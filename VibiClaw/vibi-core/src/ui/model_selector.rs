use gtk::prelude::*;
use gtk::{Box as GtkBox, Button, Label, Orientation, Align, ScrolledWindow, PolicyType, Window, Overlay, Revealer};
use std::rc::Rc;
use std::cell::RefCell;

#[derive(Clone)]
pub struct ModelSelector {
    pub container: GtkBox,
    pub selected_model: Rc<RefCell<String>>,
    pub selected_label: Rc<RefCell<Label>>,
    pub selected_icon: Rc<RefCell<Label>>,
    pub on_model_changed: Rc<RefCell<Option<Box<dyn Fn(String)>>>>,
}

struct ModelEntry {
    id: String,
    name: String,
    desc: String,
}

struct CompanyGroup {
    icon: &'static str,
    name: &'static str,
    models: Vec<ModelEntry>,
}

pub fn build_model_selector() -> ModelSelector {
    let selected_model = Rc::new(RefCell::new(String::new()));
    let on_model_changed: Rc<RefCell<Option<Box<dyn Fn(String)>>>> = Rc::new(RefCell::new(None));
    
    let container = GtkBox::new(Orientation::Vertical, 0);
    container.set_margin_bottom(8);
    
    let trigger = Button::new();
    trigger.style_context().add_class("model-trigger");
    trigger.set_halign(Align::Start);
    
    let trigger_content = GtkBox::new(Orientation::Horizontal, 8);
    let icon = Label::new(Some("🟢"));
    icon.style_context().add_class("model-trigger-icon");
    
    let selected_label = Rc::new(RefCell::new(Label::new(Some("Select Model"))));
    let selected_icon = Rc::new(RefCell::new(icon.clone()));
    icon.set_text("🤖");
    trigger_content.pack_start(&icon, false, false, 0);
    trigger_content.pack_start(&selected_label.borrow().clone(), false, false, 0);
    let arrow = Label::new(Some("▾"));
    arrow.style_context().add_class("model-trigger-arrow");
    trigger_content.pack_start(&arrow, false, false, 0);
    trigger.add(&trigger_content);
    container.pack_start(&trigger, false, false, 0);
    
    let sel_model = selected_model.clone();
    let sel_label = selected_label.clone();
    let sel_icon = selected_icon.clone();
    let on_change = on_model_changed.clone();
    
    trigger.connect_clicked(move |btn| {
        if let Some(window) = btn.toplevel().and_then(|w| w.downcast::<Window>().ok()) {
            show_model_popup(&window, sel_model.clone(), sel_label.clone(), sel_icon.clone(), on_change.clone());
        }
    });
    
    ModelSelector {
        container,
        selected_model,
        selected_label,
        selected_icon,
        on_model_changed,
    }
}

fn get_companies() -> Vec<CompanyGroup> {
    vec![
        CompanyGroup {
            icon: "🟢",
            name: "OpenAI",
            models: vec![
                ModelEntry { id: "chatgpt".into(), name: "ChatGPT-4o".into(), desc: "Latest multimodal model".into() },
                ModelEntry { id: "chatgpt".into(), name: "ChatGPT-4".into(), desc: "Advanced reasoning".into() },
                ModelEntry { id: "chatgpt".into(), name: "ChatGPT-3.5".into(), desc: "Fast & efficient".into() },
            ],
        },
        CompanyGroup {
            icon: "🟠",
            name: "Anthropic",
            models: vec![
                ModelEntry { id: "claude".into(), name: "Claude Opus 4.8".into(), desc: "Most powerful".into() },
                ModelEntry { id: "claude".into(), name: "Claude Sonnet 4.6".into(), desc: "Balanced speed".into() },
                ModelEntry { id: "claude".into(), name: "Claude Haiku 4.5".into(), desc: "Fast & lightweight".into() },
                ModelEntry { id: "claude".into(), name: "Claude Fable 5".into(), desc: "Creative writing".into() },
            ],
        },
        CompanyGroup {
            icon: "🔵",
            name: "Google",
            models: vec![
                ModelEntry { id: "gemini".into(), name: "Gemini 2.5 Pro".into(), desc: "Deep research".into() },
                ModelEntry { id: "gemini".into(), name: "Gemini 2.5 Flash".into(), desc: "Speed optimized".into() },
                ModelEntry { id: "gemini".into(), name: "Gemini 1.5 Pro".into(), desc: "Stable release".into() },
            ],
        },
        CompanyGroup {
            icon: "🐋",
            name: "DeepSeek",
            models: vec![
                ModelEntry { id: "deepseek".into(), name: "DeepSeek-V3".into(), desc: "Latest flagship".into() },
                ModelEntry { id: "deepseek".into(), name: "DeepSeek-R1".into(), desc: "Reasoning focused".into() },
            ],
        },
        CompanyGroup {
            icon: "⚡",
            name: "xAI",
            models: vec![
                ModelEntry { id: "grok".into(), name: "Grok-3".into(), desc: "Latest generation".into() },
                ModelEntry { id: "grok".into(), name: "Grok-2".into(), desc: "Previous version".into() },
            ],
        },
        CompanyGroup {
            icon: "🟣",
            name: "Alibaba",
            models: vec![
                ModelEntry { id: "qwen".into(), name: "Qwen 3.7 Max".into(), desc: "Top performance".into() },
                ModelEntry { id: "qwen".into(), name: "Qwen 3.7 Plus".into(), desc: "Great value".into() },
                ModelEntry { id: "qwen".into(), name: "Qwen 3.7 Multi Modal".into(), desc: "Vision + text".into() },
                ModelEntry { id: "qwen".into(), name: "Qwen3-VL 235B".into(), desc: "Vision language MoE".into() },
            ],
        },
        CompanyGroup {
            icon: "🌙",
            name: "MoonShot",
            models: vec![
                ModelEntry { id: "kimi".into(), name: "Kimi Code 2.7".into(), desc: "Coding specialist".into() },
                ModelEntry { id: "kimi".into(), name: "Kimi 2.6".into(), desc: "General purpose".into() },
            ],
        },
    ]
}

fn show_model_popup(
    parent_window: &Window,
    selected_model: Rc<RefCell<String>>,
    selected_label: Rc<RefCell<Label>>,
    selected_icon: Rc<RefCell<Label>>,
    on_model_changed: Rc<RefCell<Option<Box<dyn Fn(String)>>>>,
) {
    let backdrop = GtkBox::new(Orientation::Vertical, 0);
    backdrop.style_context().add_class("model-popup-backdrop");
    backdrop.set_halign(Align::Fill);
    backdrop.set_valign(Align::Fill);
    
    let card = GtkBox::new(Orientation::Vertical, 0);
    card.style_context().add_class("model-popup-card");
    card.set_halign(Align::Center);
    card.set_valign(Align::Center);
    card.set_size_request(920, 620);
    
    let header = GtkBox::new(Orientation::Horizontal, 16);
    header.style_context().add_class("model-popup-header");
    header.set_margin_start(24);
    header.set_margin_end(24);
    header.set_margin_top(20);
    header.set_margin_bottom(12);
    
    let title = Label::new(Some("Select AI Model"));
    title.style_context().add_class("model-popup-title");
    title.set_hexpand(true);
    title.set_halign(Align::Start);
    header.pack_start(&title, true, true, 0);
    
    let close_btn = Button::with_label("✕");
    close_btn.style_context().add_class("model-popup-close");
    header.pack_start(&close_btn, false, false, 0);
    
    card.pack_start(&header, false, false, 0);
    
    let revealer = Revealer::new();
    revealer.set_transition_duration(300);
    revealer.set_transition_type(gtk::RevealerTransitionType::SlideDown);
    revealer.set_reveal_child(false);
    revealer.add(&card);
    
    let rev = revealer.clone();
    let bd_close = backdrop.clone();
    close_btn.connect_clicked(move |_| {
        rev.set_reveal_child(false);
        let bd = bd_close.clone();
        gtk::glib::timeout_add_local(std::time::Duration::from_millis(300), move || {
            if let Some(parent) = bd.parent() {
                parent.downcast_ref::<Overlay>().map(|o| o.remove(&bd));
            }
            gtk::glib::ControlFlow::Break
        });
    });
    
    let separator = gtk::Separator::new(Orientation::Horizontal);
    separator.style_context().add_class("model-popup-separator");
    card.pack_start(&separator, false, false, 0);
    
    let body_scroll = ScrolledWindow::new(None::<&gtk::Adjustment>, None::<&gtk::Adjustment>);
    body_scroll.set_hexpand(true);
    body_scroll.set_vexpand(true);
    body_scroll.set_policy(PolicyType::Automatic, PolicyType::Automatic);
    
    let flow = gtk::FlowBox::new();
    flow.set_margin_start(24);
    flow.set_margin_end(24);
    flow.set_margin_top(16);
    flow.set_margin_bottom(16);
    flow.set_hexpand(true);
    flow.set_selection_mode(gtk::SelectionMode::None);
    flow.set_min_children_per_line(2);
    flow.set_max_children_per_line(5);
    flow.set_homogeneous(true);
    flow.set_row_spacing(16);
    flow.set_column_spacing(16);
    
    let companies = get_companies();
    let current_model = selected_model.borrow().clone();
    let bd_close = backdrop.clone();
    
    for company in &companies {
        let column = build_company_column(
            company,
            &current_model,
            selected_model.clone(),
            selected_label.clone(),
            selected_icon.clone(),
            on_model_changed.clone(),
            &bd_close,
        );
        flow.insert(&column, -1);
    }
    
    body_scroll.add(&flow);
    card.pack_start(&body_scroll, true, true, 0);
    
    backdrop.pack_start(&revealer, true, true, 0);
    backdrop.show_all();
    revealer.set_reveal_child(true);
    
    if let Some(overlay) = parent_window.child().and_then(|c| c.downcast::<Overlay>().ok()) {
        overlay.add_overlay(&backdrop);
    } else {
        let new_overlay = Overlay::new();
        if let Some(child) = parent_window.child() {
            parent_window.remove(&child);
            new_overlay.add(&child);
        }
        new_overlay.add_overlay(&backdrop);
        parent_window.add(&new_overlay);
        parent_window.show_all();
    }
}

fn build_company_column(
    company: &CompanyGroup,
    _current_model: &str,
    selected_model: Rc<RefCell<String>>,
    selected_label: Rc<RefCell<Label>>,
    selected_icon: Rc<RefCell<Label>>,
    on_model_changed: Rc<RefCell<Option<Box<dyn Fn(String)>>>>,
    backdrop: &GtkBox,
) -> GtkBox {
    let column = GtkBox::new(Orientation::Vertical, 6);
    column.style_context().add_class("model-company-column");
    column.set_size_request(160, -1);
    
    let header = GtkBox::new(Orientation::Horizontal, 6);
    let icon = Label::new(Some(company.icon));
    icon.style_context().add_class("model-company-icon");
    header.pack_start(&icon, false, false, 0);
    let name = Label::new(Some(company.name));
    name.style_context().add_class("model-company-name");
    header.pack_start(&name, false, false, 0);
    column.pack_start(&header, false, false, 0);
    
    let sep = gtk::Separator::new(Orientation::Horizontal);
    sep.style_context().add_class("model-company-sep");
    column.pack_start(&sep, false, false, 0);
    
    for model_entry in &company.models {
        let row = build_model_row(
            model_entry,
            company.icon,
            selected_model.clone(),
            selected_label.clone(),
            selected_icon.clone(),
            on_model_changed.clone(),
            backdrop,
        );
        column.pack_start(&row, false, false, 0);
    }
    
    column
}

fn build_model_row(
    entry: &ModelEntry,
    company_icon: &str,
    selected_model: Rc<RefCell<String>>,
    selected_label: Rc<RefCell<Label>>,
    selected_icon: Rc<RefCell<Label>>,
    on_model_changed: Rc<RefCell<Option<Box<dyn Fn(String)>>>>,
    backdrop: &GtkBox,
) -> GtkBox {
    let wrapper = GtkBox::new(Orientation::Vertical, 0);
    wrapper.set_margin_top(3);
    wrapper.set_margin_bottom(3);
    
    let event_box = gtk::EventBox::new();
    event_box.style_context().add_class("model-subcard");
    event_box.set_margin_start(4);
    event_box.set_margin_end(4);
    
    let card = GtkBox::new(Orientation::Vertical, 6);
    card.set_margin_start(12);
    card.set_margin_end(12);
    card.set_margin_top(10);
    card.set_margin_bottom(10);
    
    let name_label = Label::new(Some(&entry.name));
    name_label.style_context().add_class("model-item-name");
    name_label.set_halign(Align::Start);
    card.pack_start(&name_label, false, false, 0);
    
    let desc_label = Label::new(Some(&entry.desc));
    desc_label.style_context().add_class("model-item-desc");
    desc_label.set_halign(Align::Start);
    desc_label.set_wrap(true);
    desc_label.set_max_width_chars(20);
    card.pack_start(&desc_label, false, false, 0);
    
    event_box.add(&card);
    wrapper.pack_start(&event_box, false, false, 0);
    
    let id = entry.id.clone();
    let model_name = entry.name.clone();
    let icon_text = company_icon.to_string();
    let sel = selected_model;
    let lbl = selected_label;
    let sicon = selected_icon;
    let on_change = on_model_changed;
    let bd = backdrop.clone();
    
    event_box.connect_button_press_event(move |_, _| {
        *sel.borrow_mut() = id.clone();
        lbl.borrow().set_text(&model_name);
        sicon.borrow().set_text(&icon_text);
        if let Some(parent) = bd.parent() {
            let bd_clone = bd.clone();
            if let Some(overlay) = parent.downcast_ref::<Overlay>() {
                let ov = overlay.clone();
                let b = bd_clone.clone();
                gtk::glib::timeout_add_local(std::time::Duration::from_millis(200), move || {
                    ov.remove(&b);
                    gtk::glib::ControlFlow::Break
                });
            } else {
                parent.downcast_ref::<Overlay>().map(|o| o.remove(&bd_clone));
            }
        }
        if let Some(ref cb) = *on_change.borrow() {
            cb(id.clone());
        }
        false.into()
    });
    
    wrapper
}