use gtk::prelude::*;
use gtk::{Box as GtkBox, Button, Label, Orientation, Align, ScrolledWindow, PolicyType};

pub struct AiNotebook {
    pub container: GtkBox,
}

pub fn build_ai_notebook(stack: gtk::Stack) -> AiNotebook {
    let container = GtkBox::new(Orientation::Vertical, 0);
    container.set_hexpand(true);
    container.set_vexpand(true);
    
    let topbar = GtkBox::new(Orientation::Horizontal, 0);
    topbar.style_context().add_class("topbar");
    topbar.set_margin_start(24);
    topbar.set_margin_end(24);
    topbar.set_margin_top(14);
    topbar.set_margin_bottom(14);
    
    let title = Label::new(Some("AI Notebook"));
    title.style_context().add_class("topbar-title");
    topbar.pack_start(&title, false, false, 0);
    
    let divider = gtk::Separator::new(Orientation::Horizontal);
    divider.style_context().add_class("topbar-divider");
    
    container.pack_start(&topbar, false, false, 0);
    container.pack_start(&divider, false, false, 0);
    
    let scroll = ScrolledWindow::new(None::<&gtk::Adjustment>, None::<&gtk::Adjustment>);
    scroll.set_vexpand(true);
    scroll.set_policy(PolicyType::Never, PolicyType::Automatic);
    
    let content = GtkBox::new(Orientation::Vertical, 20);
    content.set_margin_start(60);
    content.set_margin_end(60);
    content.set_margin_top(40);
    content.set_margin_bottom(40);
    content.set_halign(Align::Start);
    content.set_valign(Align::Start);
    
    
    
    scroll.add(&content);
    container.pack_start(&scroll, true, true, 0);
    
    AiNotebook { container }
}