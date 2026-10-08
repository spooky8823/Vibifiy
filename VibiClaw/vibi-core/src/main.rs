mod types;
mod sandbox;
mod executor;
mod ui;
mod ai_bridge;
mod vibi_lang;
mod storage;
mod chat_store;
mod crypto;
mod logger;
mod notification_panel;
mod debug;
pub mod api;

use gtk::prelude::*;
use gtk::{Application, CssProvider};
use glib::LogLevel;

fn main() {

    // Global panic hook - logs crashes with location
    std::panic::set_hook(Box::new(|info| {
        let msg = if let Some(s) = info.payload().downcast_ref::<&str>() {
            s.to_string()
        } else if let Some(s) = info.payload().downcast_ref::<String>() {
            s.clone()
        } else {
            "Unknown panic".to_string()
        };
        let location = info.location().map(|l| format!("{}:{}", l.file(), l.line())).unwrap_or_default();
        eprintln!("[main.rs] [Error] in {} : {}", location, msg);
    }));
    
    // GTK/GLib warning and critical logs
    glib::log_set_default_handler(|_domain, level, msg| {
        if level == LogLevel::Error || level == LogLevel::Critical {
            eprintln!("[VibiClaw] {}", msg);
        }
    });
    
    std::env::set_var("WEBKIT_DISABLE_COMPOSITING_MODE", "1");
    std::env::set_var("GTK_CSD", "0");
    
    let webkit_data = dirs::config_dir()
        .unwrap_or_else(|| std::path::PathBuf::from("."))
        .join("vibi-ai")
        .join("webkit");
    std::fs::create_dir_all(&webkit_data).ok();
    std::env::set_var("WEBKIT_LOCAL_STORAGE_DIRECTORY", webkit_data.to_str().unwrap());
    
    let app = Application::builder()
        .application_id("com.vibi.ai")
        .build();

    app.connect_startup(|_| {
        load_css();
    });
    app.connect_activate(ui::build_window);

    app.run();
}

fn load_css() {
    let provider = CssProvider::new();
    let css = include_str!("ui/style.css");
    println!("[CSS] Loading {} bytes of CSS", css.len());
    if let Err(e) = provider.load_from_data(css.as_bytes()) {
        eprintln!("[CSS] Parse error: {:?}", e);
    }
    
    if let Some(screen) = gdk::Screen::default() {
        gtk::StyleContext::add_provider_for_screen(
            &screen,
            &provider,
            gtk::STYLE_PROVIDER_PRIORITY_APPLICATION,
        );
        println!("[CSS] Provider added to screen");
    } else {
        eprintln!("[CSS] No default screen");
    }
}
