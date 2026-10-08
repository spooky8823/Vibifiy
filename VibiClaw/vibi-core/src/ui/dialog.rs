use gtk::prelude::*;
use gtk::{Box as GtkBox, Button, Label, Entry, Orientation, Align, Separator, Window};

pub fn show_rename_dialog<F: Fn(String) + 'static>(on_confirm: F) {
    let dialog = Window::new(gtk::WindowType::Toplevel);
    dialog.set_modal(true);
    dialog.set_default_size(380, 200);
    dialog.set_decorated(false);
    dialog.set_resizable(false);
    dialog.style_context().add_class("html-dialog");

    let content = GtkBox::new(Orientation::Vertical, 0);
    content.style_context().add_class("dialog-content");

    let title = Label::new(Some("Rename Chat"));
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

    let entry = Entry::new();
    entry.set_placeholder_text(Some("New chat name"));
    entry.style_context().add_class("dialog-input");
    body.pack_start(&entry, false, false, 0);
    content.pack_start(&body, false, false, 0);
    content.pack_start(&Separator::new(Orientation::Horizontal), false, false, 0);

    let buttons = GtkBox::new(Orientation::Horizontal, 8);
    buttons.set_halign(Align::End);
    buttons.set_margin_start(20);
    buttons.set_margin_end(20);
    buttons.set_margin_top(12);
    buttons.set_margin_bottom(16);

    let cancel = Button::with_label("Cancel");
    cancel.style_context().add_class("dialog-btn-secondary");
    let rename = Button::with_label("Rename");
    rename.style_context().add_class("dialog-btn-primary");
    buttons.pack_start(&cancel, false, false, 0);
    buttons.pack_start(&rename, false, false, 0);
    content.pack_start(&buttons, false, false, 0);

    dialog.add(&content);

    let dlg = dialog.clone();
    rename.connect_clicked(move |_| {
        let new_name = entry.text().to_string();
        if !new_name.trim().is_empty() {
            on_confirm(new_name);
        }
        dlg.close();
    });

    let dlg2 = dialog.clone();
    cancel.connect_clicked(move |_| dlg2.close());

    dialog.show_all();
}

pub fn show_delete_dialog<F: Fn() + 'static>(on_confirm: F) {
    let dialog = Window::new(gtk::WindowType::Toplevel);
    dialog.set_modal(true);
    dialog.set_default_size(380, 180);
    dialog.set_decorated(false);
    dialog.set_resizable(false);
    dialog.style_context().add_class("html-dialog");

    let content = GtkBox::new(Orientation::Vertical, 0);
    content.style_context().add_class("dialog-content");

    let title = Label::new(Some("Delete Chat"));
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
    let label = Label::new(Some("Are you sure you want to delete this chat?"));
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

    let cancel = Button::with_label("Cancel");
    cancel.style_context().add_class("dialog-btn-secondary");
    let delete = Button::with_label("Delete");
    delete.style_context().add_class("dialog-btn-danger");
    buttons.pack_start(&cancel, false, false, 0);
    buttons.pack_start(&delete, false, false, 0);
    content.pack_start(&buttons, false, false, 0);

    dialog.add(&content);

    let dlg = dialog.clone();
    delete.connect_clicked(move |_| { on_confirm(); dlg.close(); });
    let dlg2 = dialog.clone();
    cancel.connect_clicked(move |_| dlg2.close());

    dialog.show_all();
}

pub fn show_share_dialog() {
    let dialog = Window::new(gtk::WindowType::Toplevel);
    dialog.set_modal(true);
    dialog.set_default_size(380, 200);
    dialog.set_decorated(false);
    dialog.set_resizable(false);
    dialog.style_context().add_class("html-dialog");

    let content = GtkBox::new(Orientation::Vertical, 0);
    content.style_context().add_class("dialog-content");

    let title = Label::new(Some("Share Chat"));
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
    let label = Label::new(Some("Share this chat via link:"));
    label.style_context().add_class("dialog-body-text");
    body.pack_start(&label, false, false, 0);
    let link = Label::new(Some("https://sharefeaturecomingsoon.com/chat"));
    link.style_context().add_class("share-link");
    link.set_selectable(true);
    body.pack_start(&link, false, false, 0);
    content.pack_start(&body, false, false, 0);
    content.pack_start(&Separator::new(Orientation::Horizontal), false, false, 0);

    let buttons = GtkBox::new(Orientation::Horizontal, 8);
    buttons.set_halign(Align::End);
    buttons.set_margin_start(20);
    buttons.set_margin_end(20);
    buttons.set_margin_top(12);
    buttons.set_margin_bottom(16);
    let close = Button::with_label("Close");
    close.style_context().add_class("dialog-btn-primary");
    buttons.pack_start(&close, false, false, 0);
    content.pack_start(&buttons, false, false, 0);

    dialog.add(&content);

    let dlg = dialog.clone();
    close.connect_clicked(move |_| dlg.close());

    dialog.show_all();
}