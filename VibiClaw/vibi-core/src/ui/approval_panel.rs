use gtk::prelude::*;
use gtk::{Box as GtkBox, Button, Label, Orientation, Align, Revealer, Separator, ScrolledWindow, PolicyType};
use std::rc::Rc;
use std::cell::RefCell;

pub struct ApprovalPanel {
    pub container: GtkBox,
    pub revealer: Revealer,
    pub card_list: GtkBox,
    pending: Rc<RefCell<Vec<(crate::types::Command, bool)>>>,
}

impl ApprovalPanel {
    pub fn new() -> Self {
        let revealer = Revealer::new();
        revealer.set_transition_type(gtk::RevealerTransitionType::SlideLeft);
        revealer.set_transition_duration(300);
        revealer.set_reveal_child(false);

        let container = GtkBox::new(Orientation::Vertical, 0);
        container.set_size_request(340, -1);
        container.style_context().add_class("approval-panel");
        container.set_vexpand(true);

        // Header
        let header = GtkBox::new(Orientation::Vertical, 0);
        header.style_context().add_class("approval-panel-header");
        header.set_margin_start(16);
        header.set_margin_end(16);
        header.set_margin_top(12);
        header.set_margin_bottom(8);

        let title_row = GtkBox::new(Orientation::Horizontal, 8);
        let title = Label::new(Some("🛡️ Approval Queue"));
        title.style_context().add_class("approval-panel-title");
        title.set_halign(Align::Start);
        title.set_hexpand(true);
        title_row.pack_start(&title, true, true, 0);

        let close_btn = Button::with_label("✕");
        close_btn.style_context().add_class("approval-panel-close-btn");
        title_row.pack_start(&close_btn, false, false, 0);
        header.pack_start(&title_row, false, false, 0);

        // Batch buttons
        let batch_row = GtkBox::new(Orientation::Horizontal, 8);
        batch_row.set_margin_top(8);

        let approve_all = Button::with_label("✅ Approve All");
        approve_all.style_context().add_class("approval-panel-approve-all");
        approve_all.set_hexpand(true);
        batch_row.pack_start(&approve_all, true, true, 0);

        let deny_all = Button::with_label("❌ Deny All");
        deny_all.style_context().add_class("approval-panel-deny-all");
        deny_all.set_hexpand(true);
        batch_row.pack_start(&deny_all, true, true, 0);

        header.pack_start(&batch_row, false, false, 0);
        container.pack_start(&header, false, false, 0);
        container.pack_start(&Separator::new(Orientation::Horizontal), false, false, 0);

        // Scrollable card list
        let scroll = ScrolledWindow::new(None::<&gtk::Adjustment>, None::<&gtk::Adjustment>);
        scroll.set_vexpand(true);
        scroll.set_policy(PolicyType::Never, PolicyType::Automatic);

        let card_list = GtkBox::new(Orientation::Vertical, 4);
        card_list.set_vexpand(true);
        card_list.set_valign(Align::Start);
        let empty_label = Label::new(Some("No actions to approve yet."));
        empty_label.style_context().add_class("approval-empty");
        empty_label.set_halign(Align::Center);
        empty_label.set_margin_top(20);
        card_list.pack_start(&empty_label, false, false, 0);
        scroll.add(&card_list);
        container.pack_start(&scroll, true, true, 0);

        revealer.add(&container);

        // Wire Approve All
        let pending = Rc::new(RefCell::new(Vec::new()));
        let card_list_approve = card_list.clone();
        let pending_all = pending.clone();
        approve_all.connect_clicked(move |_| {
            for (_, approved) in pending_all.borrow_mut().iter_mut() {
                *approved = true;
            }
            let children = card_list_approve.children();
            for child in &children {
                card_list_approve.remove(child);
            }
            let empty_label = Label::new(Some("All approved! Executing..."));
            empty_label.style_context().add_class("approval-empty");
            empty_label.set_halign(Align::Center);
            empty_label.set_margin_top(20);
            card_list_approve.pack_start(&empty_label, false, false, 0);
            empty_label.show_all();
        });

        // Wire Deny All
        let card_list_deny = card_list.clone();
        let pending_deny = pending.clone();
        deny_all.connect_clicked(move |_| {
            pending_deny.borrow_mut().clear();
            let children = card_list_deny.children();
            for child in &children {
                card_list_deny.remove(child);
            }
            let empty_label = Label::new(Some("All denied."));
            empty_label.style_context().add_class("approval-empty");
            empty_label.set_halign(Align::Center);
            empty_label.set_margin_top(20);
            card_list_deny.pack_start(&empty_label, false, false, 0);
            empty_label.show_all();
        });

        // Close button wiring
        let rev = revealer.clone();
        close_btn.connect_clicked(move |_| {
            rev.set_reveal_child(false);
        });

        ApprovalPanel {
            container,
            revealer,
            card_list,
            pending: pending.clone(),
        }
    }

    pub fn toggle(&self) {
        let revealed = self.revealer.reveals_child();
        self.revealer.set_reveal_child(!revealed);
    }

    pub fn show(&self) {
        self.revealer.set_reveal_child(true);
    }

    pub fn hide(&self) {
        self.revealer.set_reveal_child(false);
    }

    pub fn add_card(&self, tool: &str, path: &str, detail: &str, cmd: crate::types::Command) {
        let pending = self.pending.clone();
        let idx = pending.borrow().len();
        pending.borrow_mut().push((cmd, false));
        
        let card = build_approval_card(tool, path, detail);
        
        // Wire approve/deny buttons — they're the last two children of the button row
        // The card has: info box, then buttons box. We find the button box and get its children.
        let children = card.children();
        let btn_box = children.iter()
            .filter_map(|c| c.downcast_ref::<GtkBox>())
            .last();
        if let Some(btn_box) = btn_box {
            let children = btn_box.children();
            let btns: Vec<Button> = children.iter()
                .filter_map(|c| c.downcast_ref::<Button>().cloned())
                .collect();
            if btns.len() >= 2 {
                let approve_btn = &btns[0];
                let deny_btn = &btns[1];
                let p = pending.clone();
                let card_clone = card.clone();
                approve_btn.connect_clicked(move |_| {
                    p.borrow_mut()[idx].1 = true;
                    card_clone.remove(&card_clone);
                });
                let p2 = pending.clone();
                let card_clone2 = card.clone();
                deny_btn.connect_clicked(move |_| {
                    p2.borrow_mut()[idx].1 = false;
                    card_clone2.remove(&card_clone2);
                });
            }
        }
        
        self.card_list.pack_start(&card, false, false, 0);
        card.show_all();
        self.show();
    }

    pub fn clear(&self) {
        let children = self.card_list.children();
        for child in &children {
            self.card_list.remove(child);
        }
        self.pending.borrow_mut().clear();
        let empty_label = Label::new(Some("No actions to approve yet."));
        empty_label.style_context().add_class("approval-empty");
        empty_label.set_halign(Align::Center);
        empty_label.set_margin_top(20);
        self.card_list.pack_start(&empty_label, false, false, 0);
    }
    
    pub fn get_approved_commands(&self) -> Vec<crate::types::Command> {
        self.pending.borrow().iter()
            .filter(|(_, approved)| *approved)
            .map(|(cmd, _)| cmd.clone())
            .collect()
    }
    
    pub fn all_resolved(&self) -> bool {
        self.pending.borrow().iter().all(|(_, resolved)| *resolved)
    }
    
    pub fn is_empty(&self) -> bool {
        self.pending.borrow().is_empty()
    }
}

fn build_approval_card(tool: &str, path: &str, detail: &str) -> GtkBox {
    let card = GtkBox::new(Orientation::Vertical, 6);
    card.style_context().add_class("approval-card");
    card.set_margin_start(12);
    card.set_margin_end(12);
    card.set_margin_top(6);
    card.set_margin_bottom(6);

    let info = GtkBox::new(Orientation::Vertical, 2);

    let tool_label = Label::new(Some(tool));
    tool_label.style_context().add_class("approval-tool");
    tool_label.set_halign(Align::Start);
    info.pack_start(&tool_label, false, false, 0);

    let path_label = Label::new(Some(path));
    path_label.style_context().add_class("approval-path");
    path_label.set_halign(Align::Start);
    path_label.set_ellipsize(pango::EllipsizeMode::Middle);
    info.pack_start(&path_label, false, false, 0);

    if !detail.is_empty() {
        let detail_label = Label::new(Some(detail));
        detail_label.style_context().add_class("approval-detail");
        detail_label.set_halign(Align::Start);
        detail_label.set_wrap(true);
        detail_label.set_max_width_chars(35);
        info.pack_start(&detail_label, false, false, 0);
    }

    card.pack_start(&info, true, true, 0);

    let buttons = GtkBox::new(Orientation::Horizontal, 8);
    buttons.set_margin_top(4);

    let approve = Button::with_label("✅");
    approve.style_context().add_class("approval-approve-btn");
    approve.set_size_request(44, 32);
    buttons.pack_start(&approve, false, false, 0);

    let deny = Button::with_label("❌");
    deny.style_context().add_class("approval-deny-btn");
    deny.set_size_request(44, 32);
    buttons.pack_start(&deny, false, false, 0);

    card.pack_start(&buttons, false, false, 0);

    card
}