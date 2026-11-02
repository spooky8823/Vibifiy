# Vibrium filter lists

Vibrium loads every `*.txt` file from `<folder of vibrium.exe>\filters` at
startup (override with `--filter-dir=<dir>`). The build copies this directory
next to the executable, so the file below ships with every package:

| File | What it is |
|------|------------|
| `vibrium-baseline.txt` | First-party baseline: major ad networks plus the self-test sentinel rules. |

## Adding lists

Drop more Adblock-Plus / uBlock-syntax lists (`*.txt`) here. They are not
committed to this repository because most popular lists (EasyList, uBlock
filters) carry GPL / CC-BY-SA licences that need a deliberate decision before
they are redistributed inside Vibrium builds. A malformed list is skipped and
logged; it never disables blocking from the other files.

## Optional scriptlets

Scriptlet rules (`example.com##+js(name, args)`) need a `resources.json` in
this folder (a JSON array of adblock-rust `Resource` objects). Without it
scriptlet rules are ignored. Scriptlets are injected after page load starts and
are not guaranteed to run before the page's own scripts.

## The sentinel rules

The last block of `vibrium-baseline.txt` exists only so CI can prove, from the
*packaged* build, that the shipped filters are found and active. Do not remove
it. `cargo test` checks that it matches `tests/fixtures/selftest-lists/selftest.txt`.
