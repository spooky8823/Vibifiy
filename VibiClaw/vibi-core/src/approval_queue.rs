//! Backend approval queue (the "memory").
//!
//! Every command the vibi compiler produces from a ```vibi block lands here first.
//! The GTK approval panel is only a *view* of this queue: it re-renders from
//! `snapshot()` whenever it is told the queue changed.
//!
//!   enqueue()  -> command is stored in memory, UI is told to re-sync
//!   approve()  -> command leaves memory and is handed to the executor worker
//!   deny()     -> command leaves memory and is dropped
//!
//! Approved commands run on ONE background thread, in the order they were approved,
//! so a slow shell command / git clone never freezes the UI and "Approve All"
//! keeps its original ordering (e.g. create folder -> create file -> run command).
//! Results are sent back to the UI thread over `result_tx` (shown in the Logs view).

use std::sync::mpsc;
use std::sync::{Mutex, MutexGuard};

use crate::executor::Executor;
use crate::types::{Command, CommandStatus};

struct QueuedAction {
    /// Identifies the whole batch (one ```vibi block) this command came from.
    /// Used to ignore a batch that is re-delivered while still pending.
    signature: String,
    command: Command,
}

static QUEUE: Mutex<Vec<QueuedAction>> = Mutex::new(Vec::new());
static EXEC_TX: Mutex<Option<mpsc::Sender<Command>>> = Mutex::new(None);
static UI_TX: Mutex<Option<async_channel::Sender<()>>> = Mutex::new(None);

/// Lock that survives a poisoned mutex (a panic elsewhere must not kill the queue).
fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
    m.lock().unwrap_or_else(|e| e.into_inner())
}

fn sandbox_dir() -> std::path::PathBuf {
    dirs::config_dir()
        .unwrap_or_else(|| std::path::PathBuf::from("."))
        .join("vibi-ai")
        .join("sandbox")
}

fn signature(commands: &[Command]) -> String {
    commands
        .iter()
        .map(|c| format!("{:?}|{:?}|{:?}", c.kind, c.path, c.content))
        .collect::<Vec<_>>()
        .join("\n")
}

fn notify_ui() {
    if let Some(tx) = lock(&UI_TX).as_ref() {
        let _ = tx.try_send(());
    }
}

fn worker_ready() -> bool {
    lock(&EXEC_TX).is_some()
}

/// Call once at startup (from the GTK main thread).
///  * `ui_tx`     receives a `()` every time the queue gains items
///  * `result_tx` receives each command after the worker has executed it
pub fn init(ui_tx: async_channel::Sender<()>, result_tx: async_channel::Sender<Command>) {
    *lock(&UI_TX) = Some(ui_tx);

    let root = sandbox_dir().to_string_lossy().to_string();
    let executor = match Executor::new(&root, true) {
        Ok(e) => e,
        Err(e) => {
            // Without an executor nothing can be approved; items just stay pending.
            eprintln!("[ApprovalQueue] cannot create sandbox at {}: {}", root, e);
            return;
        }
    };

    let (tx, rx) = mpsc::channel::<Command>();
    *lock(&EXEC_TX) = Some(tx);

    std::thread::spawn(move || {
        while let Ok(mut cmd) = rx.recv() {
            executor.run(&mut cmd);
            let _ = result_tx.try_send(cmd);
        }
    });
}

/// Store a freshly compiled batch in memory and tell the UI.
/// Returns how many commands were queued (0 if the batch was empty or an
/// identical batch is already waiting for approval).
pub fn enqueue(commands: Vec<Command>) -> usize {
    if commands.is_empty() {
        return 0;
    }
    let sig = signature(&commands);
    let count = commands.len();

    {
        let mut q = lock(&QUEUE);
        if q.iter().any(|a| a.signature == sig) {
            println!(
                "[ApprovalQueue] identical batch already pending, ignoring {} command(s)",
                count
            );
            return 0;
        }
        for mut c in commands {
            c.status = CommandStatus::Pending;
            q.push(QueuedAction {
                signature: sig.clone(),
                command: c,
            });
        }
    }

    println!("[ApprovalQueue] queued {} command(s) for approval", count);
    notify_ui();
    count
}

/// Everything currently waiting for approval, oldest first.
pub fn snapshot() -> Vec<Command> {
    lock(&QUEUE).iter().map(|a| a.command.clone()).collect()
}

/// Hand an already-removed command to the executor worker.
fn dispatch(mut cmd: Command) -> bool {
    cmd.status = CommandStatus::Approved;
    match lock(&EXEC_TX).as_ref() {
        Some(tx) => tx.send(cmd).is_ok(),
        None => false,
    }
}

/// Approve one command: it leaves the queue and is executed in the background.
/// Returns false (and leaves the command pending) if it can't be run right now.
pub fn approve(id: &str) -> bool {
    if !worker_ready() {
        eprintln!("[ApprovalQueue] executor is not running; leaving {} pending", id);
        return false;
    }
    let item = {
        let mut q = lock(&QUEUE);
        let pos = q.iter().position(|a| a.command.id == id);
        pos.map(|i| q.remove(i))
    };
    match item {
        Some(a) => dispatch(a.command),
        None => false,
    }
}

/// Deny one command: it leaves the queue and is never executed.
pub fn deny(id: &str) -> bool {
    let item = {
        let mut q = lock(&QUEUE);
        let pos = q.iter().position(|a| a.command.id == id);
        pos.map(|i| q.remove(i))
    };
    match item {
        Some(a) => {
            println!(
                "[ApprovalQueue] denied {:?} {}",
                a.command.kind,
                a.command.path.as_deref().unwrap_or("")
            );
            true
        }
        None => false,
    }
}

/// Approve everything, preserving order. Returns how many were handed to the executor.
pub fn approve_all() -> usize {
    if !worker_ready() {
        eprintln!("[ApprovalQueue] executor is not running; leaving queue untouched");
        return 0;
    }
    let items: Vec<QueuedAction> = std::mem::take(&mut *lock(&QUEUE));
    let mut n = 0;
    for a in items {
        if dispatch(a.command) {
            n += 1;
        }
    }
    n
}

/// Deny everything. Returns how many were dropped.
pub fn deny_all() -> usize {
    let items: Vec<QueuedAction> = std::mem::take(&mut *lock(&QUEUE));
    println!("[ApprovalQueue] denied {} command(s)", items.len());
    items.len()
}
