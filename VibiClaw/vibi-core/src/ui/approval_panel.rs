use gtk::prelude::*;
use gtk::{Align, Box as GtkBox, Button, Label, Orientation, PolicyType, Revealer, ScrolledWindow, Separator};
use std::cell::RefCell;
use std::rc::Rc;

use crate::types::Command;

/// The panel owns no approval state of its own. The backend queue
/// (`crate::approval_queue`) is the single source of truth; `sync()` makes the
/// cards on screen match it. Clicking Approve/Deny talks to the queue first and
/// then re-syncs, so a card disappears exactly when its command has left memory.
#[derive(Clone)]
struct PanelState {
    revealer: Revealer,
    card_list: GtkBox,
    empty_label: Label,
    title: Label,
    /// (command id, card widget) for every card currently on screen, oldest first.
    cards: Rc<RefCell<Vec<(String, GtkBox)>>>,
}

impl PanelState {
    fn sync(&self) {
        let queued = crate::approval_queue::snapshot();

        // 1. Remove cards whose command is no longer in memory (approved / denied).
        let gone: Vec<String> = self
            .cards
            .borrow()
            .iter()
            .filter(|entry| !queued.iter().any(|c| c.id == entry.0))
            .map(|entry| entry.0.clone())
            .collect();
        for id in &gone {
            self.drop_card(id);
        }

        // 2. Add a card for every queued command that has none yet.
        for cmd in queued {
            let known = self.cards.borrow().iter().any(|entry| entry.0 == cmd.id);
            if !known {
                self.add_card(cmd);
            }
        }

        self.refresh();
    }

    fn drop_card(&self, id: &str) {
        let pos = self.cards.borrow().iter().position(|entry| entry.0 == id);
        if let Some(i) = pos {
            let (_, card) = self.cards.borrow_mut().remove(i);
            self.card_list.remove(&card);
        }
    }

    fn add_card(&self, cmd: Command) {
        let (card, approve_btn, deny_btn) = build_approval_card(
            cmd.kind.label(),
            cmd.path.as_deref().unwrap_or("-"),
            &cmd.display_detail(),
        );

        let id_approve = cmd.id.clone();
        let st_approve = self.clone();
        approve_btn.connect_clicked(move |_| {
            if !crate::approval_queue::approve(&id_approve) {
                eprintln!("[Approval] could not approve {}", id_approve);
            }
            st_approve.sync();
            st_approve.hide_if_empty();
        });

        let id_deny = cmd.id.clone();
        let st_deny = self.clone();
        deny_btn.connect_clicked(move |_| {
            crate::approval_queue::deny(&id_deny);
            st_deny.sync();
            st_deny.hide_if_empty();
        });

        self.card_list.pack_start(&card, false, false, 0);
        card.show_all();
        self.cards.borrow_mut().push((cmd.id.clone(), card));
    }

    fn refresh(&self) {
        let n = self.cards.borrow().len();
        self.empty_label.set_visible(n == 0);
        if n == 0 {
            self.title.set_text("Approval Queue");
        } else {
            self.title.set_text(&format!("Approval Queue ({})", n));
        }
    }

    fn hide_if_empty(&self) {
        if self.cards.borrow().is_empty() {
            self.revealer.set_reveal_child(false);
        }
    }
}

pub struct ApprovalPanel {
    pub container: GtkBox,
    pub revealer: Revealer,
    pub card_list: GtkBox,
    state: PanelState,
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
        let title = Label::new(Some("Approval Queue"));
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

        let approve_all = Button::with_label("Approve All");
        approve_all.style_context().add_class("approval-panel-approve-all");
        approve_all.set_hexpand(true);
        batch_row.pack_start(&approve_all, true, true, 0);

        let deny_all = Button::with_label("Deny All");
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

        // Its visibility is managed by PanelState::refresh(), so keep show_all() away from it.
        let empty_label = Label::new(Some("No actions to approve yet."));
        empty_label.style_context().add_class("approval-empty");
        empty_label.set_halign(Align::Center);
        empty_label.set_margin_top(20);
        empty_label.set_no_show_all(true);
        empty_label.set_visible(true);
        card_list.pack_start(&empty_label, false, false, 0);

        scroll.add(&card_list);
        container.pack_start(&scroll, true, true, 0);

        revealer.add(&container);

        let state = PanelState {
            revealer: revealer.clone(),
            card_list: card_list.clone(),
            empty_label,
            title,
            cards: Rc::new(RefCell::new(Vec::new())),
        };

        // Approve All: everything leaves memory in order and runs on the executor worker.
        let st_all = state.clone();
        approve_all.connect_clicked(move |_| {
            let n = crate::approval_queue::approve_all();
            println!("[Approval] approved {} queued action(s)", n);
            st_all.sync();
            st_all.hide_if_empty();
        });

        // Deny All: everything leaves memory and is never executed.
        let st_none = state.clone();
        deny_all.connect_clicked(move |_| {
            crate::approval_queue::deny_all();
            st_none.sync();
            st_none.hide_if_empty();
        });

        let rev = revealer.clone();
        close_btn.connect_clicked(move |_| {
            rev.set_reveal_child(false);
        });

        ApprovalPanel {
            container,
            revealer,
            card_list,
            state,
        }
    }

    /// Re-render the cards from the backend queue.
    pub fn sync(&self) {
        self.state.sync();
    }

    pub fn has_cards(&self) -> bool {
        !self.state.cards.borrow().is_empty()
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
}

/// Returns (card, approve button, deny button).
fn build_approval_card(tool: &str, path: &str, detail: &str) -> (GtkBox, Button, Button) {
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
        detail_label.set_wrap_mode(pango::WrapMode::WordChar);
        detail_label.set_max_width_chars(35);
        info.pack_start(&detail_label, false, false, 0);
    }

    card.pack_start(&info, true, true, 0);

    let buttons = GtkBox::new(Orientation::Horizontal, 8);
    buttons.set_margin_top(4);

    let approve = Button::with_label("Approve");
    approve.style_context().add_class("approval-approve-btn");
    approve.set_size_request(44, 32);
    buttons.pack_start(&approve, false, false, 0);

    let deny = Button::with_label("Deny");
    deny.style_context().add_class("approval-deny-btn");
    deny.set_size_request(44, 32);
    buttons.pack_start(&deny, false, false, 0);

    card.pack_start(&buttons, false, false, 0);

    (card, approve, deny)
}
