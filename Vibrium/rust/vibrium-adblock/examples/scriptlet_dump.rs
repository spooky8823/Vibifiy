//! Prints the scriptlet script the engine would inject for a URL.
//!   cargo run --release --example scriptlet_dump -- <dir> <url> [out.js]
use std::time::Instant;
use vibrium_adblock::ScriptletEngine;

fn main() {
    let mut args = std::env::args().skip(1);
    let dir = args.next().expect("dir");
    let url = args.next().expect("url");
    let out = args.next();

    let started = Instant::now();
    let engine = ScriptletEngine::from_directory(std::path::Path::new(&dir)).expect("engine");
    eprintln!("loaded {} scriptlet rules in {:?}", engine.rule_count(), started.elapsed());

    let started = Instant::now();
    let script = engine.injected_script(&url).expect("script");
    eprintln!("script for {url}: {} bytes in {:?}", script.len(), started.elapsed());
    if let Some(path) = out {
        std::fs::write(path, &script).unwrap();
    }
}
