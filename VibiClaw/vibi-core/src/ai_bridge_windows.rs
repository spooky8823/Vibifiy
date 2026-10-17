// Windows AiBridge: WebView2 (via wry) embedded into the GTK3 window.
//
// How it fits together:
//   * GTK keeps a blank DrawingArea ("placeholder") in the layout.
//   * The WebView2 control is a child HWND of the GTK toplevel window, and is
//     moved/resized every frame to sit exactly on top of the placeholder.
//   * The page JS talks back the same way as on Linux: by changing the URL
//     hash (#vibi-..., #vibi-action-...). wry's navigation handler sees those.

use glib::translate::ToGlibPtr;
use gtk::prelude::*;
use gtk::{Box as GtkBox, Button, DrawingArea, Orientation};
use std::cell::{Cell, RefCell};
use std::num::NonZeroIsize;
use std::rc::{Rc, Weak};
use std::sync::atomic::{AtomicI32, Ordering};
use wry::dpi::{PhysicalPosition, PhysicalSize, Position, Size};
use wry::raw_window_handle::{
    HandleError, HasWindowHandle, RawWindowHandle, Win32WindowHandle, WindowHandle,
};
use wry::{PageLoadEvent, Rect, WebContext, WebView, WebViewBuilder};

const SYSTEM_PROMPT: &str = include_str!("system_prompt.txt");
const CHATGPT_JS: &str = include_str!("agentic_detection/chatgpt.js");
const GEMINI_JS: &str = include_str!("agentic_detection/gemini.js");

/// Injected into every page before its own scripts run.
///  * The observer scripts report results with history.replaceState(url#vibi-...),
///    which fires NO event WebView2 can see (WebKitGTK notices it, WebView2 does
///    not). Wrap replaceState so those URLs are forwarded over wry's IPC.
///  * TEMPORARY diagnostics: report what the page looks like to the detector,
///    only when it changes, as "vibi-debug:{json}" (printed as "[JS] ...").
const PAGE_BRIDGE_JS: &str = r##"
(function () {
  if (window.__vibi_ipc_patched) return;
  window.__vibi_ipc_patched = true;
  function post(msg) { try { window.ipc.postMessage(msg); } catch (e) {} }

  var orig = History.prototype.replaceState;
  History.prototype.replaceState = function (state, title, url) {
    var r = orig.apply(this, arguments);
    try { if (typeof url === 'string' && url.indexOf('#vibi-') !== -1) post(url); } catch (e) {}
    return r;
  };

  window.addEventListener('hashchange', function () {
    if (location.hash.indexOf('#vibi-') === 0) {
      post(location.href);
      history.replaceState(null, '', location.pathname + location.search);
    }
  });

  var last = '';
  setInterval(function () {
    try {
      var m = document.querySelectorAll('[data-message-author-role="assistant"]');
      var lm = m.length ? m[m.length - 1] : null;
      var s = {
        host: location.host,
        assistant: m.length,
        streaming: !!document.querySelector('[data-testid="stop-button"]'),
        pre: lm ? lm.querySelectorAll('pre').length : 0,
        code: lm ? lm.querySelectorAll('pre code').length : 0,
        observer: !!window.__vibi_detector,
        lastHash: (window.__vibi_last_hash === undefined) ? null : window.__vibi_last_hash
      };
      var j = JSON.stringify(s);
      if (j !== last) { last = j; post('vibi-debug:' + j); }
    } catch (e) {}
  }, 2000);
})();
"##;

// ---------------------------------------------------------------------------
// Win32 plumbing
// ---------------------------------------------------------------------------

extern "C" {
    // Exported by libgdk-3 on Windows; returns the HWND of a native GdkWindow.
    fn gdk_win32_window_get_handle(window: *mut gdk::ffi::GdkWindow) -> *mut std::ffi::c_void;
}

struct Hwnd(isize);

impl HasWindowHandle for Hwnd {
    fn window_handle(&self) -> Result<WindowHandle<'_>, HandleError> {
        let hwnd = NonZeroIsize::new(self.0).ok_or(HandleError::Unavailable)?;
        let raw = RawWindowHandle::Win32(Win32WindowHandle::new(hwnd));
        Ok(unsafe { WindowHandle::borrow_raw(raw) })
    }
}

fn toplevel_hwnd(widget: &DrawingArea) -> Option<isize> {
    let top = widget.toplevel()?;
    let win = top.window()?;
    let h = unsafe { gdk_win32_window_get_handle(win.to_glib_none().0) };
    if h.is_null() {
        None
    } else {
        Some(h as isize)
    }
}

/// Width (logical px) on the right edge that GTK overlays (the approval panel)
/// are covering. The WebView2 is a native window that always draws above GTK
/// widgets, so it has to be made narrower instead of being overlapped.
static RIGHT_INSET: AtomicI32 = AtomicI32::new(0);

pub fn set_right_inset(logical_px: i32) {
    RIGHT_INSET.store(logical_px.max(0), Ordering::Relaxed);
}

/// (x, y, w, h) of the placeholder inside the toplevel, in physical pixels.
fn placeholder_rect(w: &DrawingArea) -> Option<(i32, i32, i32, i32)> {
    let top = w.toplevel()?;
    let (x, y) = w.translate_coordinates(&top, 0, 0)?;
    let a = w.allocation();
    let s = w.scale_factor();
    let width = (a.width() - RIGHT_INSET.load(Ordering::Relaxed)).max(1);
    Some((x * s, y * s, width * s, a.height() * s))
}

fn to_rect(r: (i32, i32, i32, i32)) -> Rect {
    Rect {
        position: Position::Physical(PhysicalPosition::new(r.0, r.1)),
        size: Size::Physical(PhysicalSize::new(r.2.max(1) as u32, r.3.max(1) as u32)),
    }
}

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------

struct Shared {
    placeholder: DrawingArea,
    webview: RefCell<Option<WebView>>,
    pending_url: RefCell<Option<String>>,
    model: RefCell<String>,
    page_loaded: Cell<bool>,
    creating: Cell<bool>,
    last_rect: Cell<(i32, i32, i32, i32)>,
    chunks: RefCell<Vec<String>>,
    actions: RefCell<Vec<String>>,
    last_why: Cell<&'static str>,
}

/// Print a "waiting" reason once each time it changes (the tick runs every frame).
fn why(s: &Shared, reason: &'static str) {
    if s.last_why.get() != reason {
        s.last_why.set(reason);
        println!("[AiBridge] waiting: {}", reason);
    }
}

fn short(s: &str) -> String {
    s.chars().take(100).collect()
}

/// Create the WebView2 control once the placeholder is mapped and has a size.
fn ensure_webview(shared: &Rc<Shared>) {
    if shared.creating.get() || shared.webview.borrow().is_some() {
        return;
    }
    let url = match shared.pending_url.borrow().clone() {
        Some(u) => u,
        None => {
            why(shared, "no model selected yet");
            return;
        }
    };
    if !shared.placeholder.is_mapped() {
        why(shared, "WebView page is not on screen yet (renderer has not switched to it)");
        return;
    }
    let Some(hwnd) = toplevel_hwnd(&shared.placeholder) else {
        why(shared, "no native window handle (gdk_win32_window_get_handle returned null)");
        return;
    };
    let Some(r) = placeholder_rect(&shared.placeholder) else {
        why(shared, "could not measure the placeholder");
        return;
    };
    if r.2 <= 0 || r.3 <= 0 {
        why(shared, "placeholder has zero size");
        return;
    }
    println!("[AiBridge] creating WebView2: hwnd={:#x} rect={:?} url={}", hwnd, r, url);

    shared.creating.set(true);
    let parent = Hwnd(hwnd);
    let w_nav: Weak<Shared> = Rc::downgrade(shared);
    let w_load: Weak<Shared> = Rc::downgrade(shared);

    let data_dir = dirs::data_local_dir()
        .unwrap_or_else(|| std::path::PathBuf::from("."))
        .join("VibiClaw")
        .join("webview2");
    std::fs::create_dir_all(&data_dir).ok();
    let mut ctx = WebContext::new(Some(data_dir));
    let w_ipc: Weak<Shared> = Rc::downgrade(shared);

    let result = WebViewBuilder::new_with_web_context(&mut ctx)
        .with_url(&url)
        .with_bounds(to_rect(r))
        .with_initialization_script(PAGE_BRIDGE_JS)
        .with_ipc_handler(move |req| {
            let uri = req.body();
            if let Some(rest) = uri.strip_prefix("vibi-debug:") {
                println!("[JS] {}", rest);
                return;
            }
            println!("[AiBridge] ipc: {}", short(uri));
            if uri.contains("#vibi-") {
                if let Some(s) = w_ipc.upgrade() {
                    handle_nav(&s, uri);
                }
            }
        })
        .with_navigation_handler(move |uri| {
            println!("[AiBridge] nav: {}", short(&uri));
            if uri.contains("#vibi-") {
                if let Some(s) = w_nav.upgrade() {
                    handle_nav(&s, &uri);
                }
            }
            true // always allow the navigation
        })
        .with_on_page_load_handler(move |event, _url| {
            let Some(s) = w_load.upgrade() else { return };
            match event {
                PageLoadEvent::Started => {
                    println!("[AiBridge] page load started");
                    s.page_loaded.set(false);
                }
                PageLoadEvent::Finished => {
                    s.page_loaded.set(true);
                    let model = s.model.borrow().clone();
                    println!("[AiBridge] Page loaded for {}", model);
                    let js = match model.as_str() {
                        "gemini" => GEMINI_JS,
                        _ => CHATGPT_JS,
                    };
                    if let Ok(g) = s.webview.try_borrow() {
                        if let Some(wv) = g.as_ref() {
                            let _ = wv.evaluate_script(
                                "delete window.__vibi_obs; delete window.__vibi_last_hash; delete window.__vibi_send;",
                            );
                            let _ = wv.evaluate_script(js);
                            println!("[AiBridge] Observer JS injected for {}", model);
                        }
                    }
                }
            }
        })
        .build_as_child(&parent);
    shared.creating.set(false);

    match result {
        Ok(wv) => {
            *shared.webview.borrow_mut() = Some(wv);
            shared.last_rect.set(r);
            println!("[AiBridge] WebView2 created");
        }
        Err(e) => {
            eprintln!(
                "[AiBridge] WebView2 creation failed: {} (is the WebView2 Runtime installed?)",
                e
            );
        }
    }
}

/// Keep the WebView2 rectangle glued to the placeholder.
fn update_bounds(s: &Shared) {
    let Some(r) = placeholder_rect(&s.placeholder) else { return };
    if r.2 <= 0 || r.3 <= 0 || r == s.last_rect.get() {
        return;
    }
    if let Ok(g) = s.webview.try_borrow() {
        if let Some(wv) = g.as_ref() {
            if wv.set_bounds(to_rect(r)).is_ok() {
                s.last_rect.set(r);
            }
        }
    }
}

fn set_webview_visible(s: &Shared, visible: bool) {
    if let Ok(g) = s.webview.try_borrow() {
        if let Some(wv) = g.as_ref() {
            let _ = wv.set_visible(visible);
        }
    }
}

// ---------------------------------------------------------------------------
// Page -> app messages (URL hash protocol, same as the Linux bridge)
// ---------------------------------------------------------------------------

fn clean_payload(p: &str) -> &str {
    p.split('&').next().unwrap_or(p).split('?').next().unwrap_or(p)
}

/// Feed one "#vibi-...-<payload>" piece. Returns the full hex string on "done".
fn feed(buf: &RefCell<Vec<String>>, payload: &str) -> Option<String> {
    if payload == "done" {
        let hex: String = buf.borrow().concat();
        buf.borrow_mut().clear();
        return Some(hex);
    }
    if let Some((idx, data)) = payload.split_once('-') {
        if let Ok(i) = idx.parse::<usize>() {
            let mut b = buf.borrow_mut();
            if b.len() <= i {
                b.resize(i + 1, String::new());
            }
            b[i] = data.to_string();
        }
    }
    None
}

fn handle_nav(s: &Shared, uri: &str) {
    let action_tag = "#vibi-action-";
    let resp_tag = "#vibi-";

    if let Some(pos) = uri.find(action_tag) {
        let payload = clean_payload(&uri[pos + action_tag.len()..]);
        if let Some(hex) = feed(&s.actions, payload) {
            run_vibi_code(&hex_to_string(&hex));
        }
    } else if let Some(pos) = uri.find(resp_tag) {
        let payload = clean_payload(&uri[pos + resp_tag.len()..]);
        if let Some(hex) = feed(&s.chunks, payload) {
            match hex_decode(&hex) {
                Ok(text) if !text.is_empty() => {
                    println!("[AiBridge] Response ({} chars): {}", text.len(), text);
                }
                Ok(_) => println!("[AiBridge] ERROR: Decoded text is empty"),
                Err(_) => println!("[AiBridge] ERROR: hex_decode failed for {} chars", hex.len()),
            }
        }
    }
}

fn run_vibi_code(vibi_code: &str) {
    println!("[VibiClaw] Processing vibi code ({} chars)", vibi_code.len());
    if !vibi_code.contains("main vibi.claw") {
        return; // silently ignore non-vibi code blocks
    }
    match crate::vibi_lang::compile(vibi_code) {
        Ok(commands) => {
            println!("[VibiClaw] Compiled {} commands, executing...", commands.len());
            let sandbox_path = dirs::config_dir()
                .unwrap_or_else(|| std::path::PathBuf::from("."))
                .join("vibi-ai")
                .join("sandbox");
            if let Ok(executor) =
                crate::executor::Executor::new(sandbox_path.to_str().unwrap(), true)
            {
                let results = crate::vibi_lang::runtime::execute(commands, &executor, true);
                for result in &results {
                    println!("[VibiClaw] {}", result);
                }
            }
        }
        Err(errors) => {
            println!("[VibiClaw] Compilation failed:");
            for e in &errors {
                println!("  - {}", e);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// App -> page: typing a message into the AI's own input box
// ---------------------------------------------------------------------------

fn build_send_js(model: &str, text: &str) -> String {
    let (inputs, buttons): (&[&str], &[&str]) = match model {
        "gemini" => (
            &["rich-textarea div[contenteditable=\"true\"]", "rich-textarea p", "textarea"],
            &["button[aria-label=\"Send message\"]", "button.send-button", "button[aria-label*=\"Send\"]"],
        ),
        "chatgpt" => (
            &["#prompt-textarea", "div.ProseMirror[contenteditable=\"true\"]", "[contenteditable=\"true\"]", "textarea"],
            &[
                "[data-testid=\"send-button\"]",
                "#composer-submit-button",
                "button[aria-label=\"Send prompt\"]",
                "button[aria-label*=\"Send\"]",
            ],
        ),
        _ => (&["[contenteditable=\"true\"]", "textarea"], &["button[type=\"submit\"]"]),
    };
    let sels = serde_json::to_string(inputs).unwrap_or_else(|_| "[]".into());
    let btns = serde_json::to_string(buttons).unwrap_or_else(|_| "[]".into());
    let txt = serde_json::to_string(text).unwrap_or_else(|_| "\"\"".into());

    // Types the text the way a user would (execCommand fires the real beforeinput /
    // input events that ProseMirror and React listen to), waits for the send button
    // to become enabled, clicks it, and falls back to pressing Enter. Every step is
    // reported back as "[JS] send ...".
    const TEMPLATE: &str = r##"(function () {
  function dbg(m) { try { window.ipc.postMessage('vibi-debug:send ' + m); } catch (e) {} }
  var sels = __SELS__, btnSels = __BTNS__, text = __TEXT__;
  var input = null;
  for (var i = 0; i < sels.length && !input; i++) { input = document.querySelector(sels[i]); }
  if (!input) { dbg('input NOT FOUND on ' + location.host); return; }
  dbg('input found: <' + input.tagName.toLowerCase() + '> id=' + (input.id || '-') + ' editable=' + input.isContentEditable);
  input.focus();
  var how = '';
  if (input.tagName === 'TEXTAREA' || input.tagName === 'INPUT') {
    var proto = input.tagName === 'TEXTAREA' ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype;
    Object.getOwnPropertyDescriptor(proto, 'value').set.call(input, text);
    input.dispatchEvent(new Event('input', { bubbles: true }));
    how = 'native value setter';
  } else {
    var sel = window.getSelection(), range = document.createRange();
    range.selectNodeContents(input);
    sel.removeAllRanges(); sel.addRange(range);
    var ok = false;
    try { ok = document.execCommand('insertText', false, text); } catch (e) {}
    if (ok) { how = 'execCommand insertText'; }
    else {
      input.textContent = text;
      input.dispatchEvent(new InputEvent('input', { bubbles: true, inputType: 'insertText', data: text }));
      how = 'textContent fallback';
    }
  }
  dbg('text set via ' + how + ' (' + text.length + ' chars)');
  var tries = 0;
  (function clickWhenReady() {
    tries++;
    var b = null;
    for (var i = 0; i < btnSels.length && !b; i++) { b = document.querySelector(btnSels[i]); }
    if (b && !b.disabled && b.getAttribute('aria-disabled') !== 'true') {
      b.click();
      dbg('send button clicked after ' + tries + ' tries');
      return;
    }
    if (tries < 24) { setTimeout(clickWhenReady, 250); return; }
    var ev = { key: 'Enter', code: 'Enter', keyCode: 13, which: 13, bubbles: true, cancelable: true };
    input.dispatchEvent(new KeyboardEvent('keydown', ev));
    input.dispatchEvent(new KeyboardEvent('keyup', ev));
    dbg('send button ' + (b ? 'stayed disabled' : 'not found') + ', pressed Enter instead');
  })();
})()"##;

    TEMPLATE
        .replace("__SELS__", &sels)
        .replace("__BTNS__", &btns)
        .replace("__TEXT__", &txt)
}

// ---------------------------------------------------------------------------
// Public interface (same shape as the Linux AiBridge)
// ---------------------------------------------------------------------------

pub struct AiBridge {
    pub container: GtkBox,
    pub webview: DrawingArea, // placeholder widget the WebView2 control sits on
    shared: Rc<Shared>,
}

impl AiBridge {
    pub fn new() -> Self {
        let placeholder = DrawingArea::new();
        placeholder.set_hexpand(true);
        placeholder.set_vexpand(true);

        let container = GtkBox::new(Orientation::Vertical, 0);
        container.set_hexpand(true);
        container.set_vexpand(true);

        let input_bar = GtkBox::new(Orientation::Horizontal, 8);
        input_bar.style_context().add_class("vibi-input-bar");
        input_bar.set_margin_start(12);
        input_bar.set_margin_end(12);
        input_bar.set_margin_bottom(10);

        let entry = gtk::Entry::new();
        entry.set_placeholder_text(Some("Type here — VibiClaw will send to the AI..."));
        entry.style_context().add_class("vibi-chat-entry");
        entry.set_hexpand(true);
        input_bar.pack_start(&entry, true, true, 0);

        let send_btn = Button::with_label("Send");
        send_btn.style_context().add_class("vibi-send-btn");
        input_bar.pack_start(&send_btn, false, false, 0);

        container.pack_start(&placeholder, true, true, 0);
        container.pack_start(&input_bar, false, false, 0);

        let shared = Rc::new(Shared {
            placeholder: placeholder.clone(),
            webview: RefCell::new(None),
            pending_url: RefCell::new(None),
            model: RefCell::new(String::new()),
            page_loaded: Cell::new(false),
            creating: Cell::new(false),
            last_rect: Cell::new((0, 0, 0, 0)),
            chunks: RefCell::new(Vec::new()),
            actions: RefCell::new(Vec::new()),
            last_why: Cell::new(""),
        });

        // Per-frame: create the webview when possible, then keep it aligned.
        let w_tick = Rc::downgrade(&shared);
        placeholder.add_tick_callback(move |_, _| {
            if let Some(s) = w_tick.upgrade() {
                if s.webview.borrow().is_none() {
                    ensure_webview(&s);
                } else {
                    update_bounds(&s);
                }
            }
            glib::ControlFlow::Continue
        });

        // The WebView2 is a native child window: it must be hidden by hand
        // whenever GTK hides the placeholder (page switch, etc).
        let w_map = Rc::downgrade(&shared);
        placeholder.connect_map(move |_| {
            if let Some(s) = w_map.upgrade() {
                set_webview_visible(&s, true);
            }
        });
        let w_unmap = Rc::downgrade(&shared);
        placeholder.connect_unmap(move |_| {
            if let Some(s) = w_unmap.upgrade() {
                set_webview_visible(&s, false);
            }
        });

        // Send button
        let msg_count = Rc::new(Cell::new(0u32));
        let entry_c = entry.clone();
        let w_send = Rc::downgrade(&shared);
        send_btn.connect_clicked(move |_| {
            let text = entry_c.text().to_string();
            if text.trim().is_empty() {
                return;
            }
            entry_c.set_text("");
            let Some(s) = w_send.upgrade() else { return };

            msg_count.set(msg_count.get() + 1);
            let count = msg_count.get();
            let model = s.model.borrow().clone();

            let full_text = if count % 10 == 1 {
                format!("{}\n\n{}", SYSTEM_PROMPT, text)
            } else {
                text
            };
            let js = build_send_js(&model, &full_text);

            if s.page_loaded.get() {
                println!("[AiBridge] sending {} chars to {}", full_text.len(), model);
                if let Ok(g) = s.webview.try_borrow() {
                    if let Some(wv) = g.as_ref() {
                        if let Err(e) = wv.evaluate_script(&js) {
                            eprintln!("[AiBridge] send failed: {}", e);
                        }
                    }
                }
            } else {
                println!("[AiBridge] Page not loaded yet, message not sent");
            }
        });
        entry.connect_activate(move |_| {
            send_btn.emit_clicked();
        });

        AiBridge {
            container,
            webview: placeholder,
            shared,
        }
    }

    pub fn load_model(&self, new_model: &str) {
        let s = &self.shared;
        if *s.model.borrow() == new_model && s.page_loaded.get() {
            println!("[AiBridge] Model {} already loaded", new_model);
            return;
        }

        *s.model.borrow_mut() = new_model.to_string();
        s.page_loaded.set(false);

        let url = match new_model {
            "chatgpt" => "https://chat.openai.com",
            "gemini" => "https://gemini.google.com",
            _ => "https://chat.openai.com",
        };
        *s.pending_url.borrow_mut() = Some(url.to_string());

        println!("[AiBridge] Loading model: {} -> {}", new_model, url);
        let existing = s.webview.try_borrow().ok().map(|g| g.is_some()).unwrap_or(false);
        if existing {
            if let Ok(g) = s.webview.try_borrow() {
                if let Some(wv) = g.as_ref() {
                    let _ = wv.load_url(url);
                }
            }
        } else {
            ensure_webview(s); // if not ready yet, the tick callback retries
        }
    }
}

// ---------------------------------------------------------------------------
// Hex helpers (same as the Linux bridge)
// ---------------------------------------------------------------------------

fn hex_decode(hex: &str) -> Result<String, ()> {
    if hex.len() % 2 != 0 {
        return Err(());
    }
    let bytes: Result<Vec<u8>, _> = (0..hex.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&hex[i..i + 2], 16).map_err(|_| ()))
        .collect();
    bytes.and_then(|b| String::from_utf8(b).map_err(|_| ()))
}

fn hex_to_string(hex: &str) -> String {
    let bytes: Vec<u8> = (0..hex.len())
        .step_by(2)
        .filter_map(|i| u8::from_str_radix(&hex[i..i + 2], 16).ok())
        .collect();
    String::from_utf8_lossy(&bytes).to_string()
}