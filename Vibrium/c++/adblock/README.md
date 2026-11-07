# Ad blocking: CEF + Brave adblock-rust

```
Page -> CEF -> AdblockResourceRequestHandler -> AdblockEngine (C++ RAII)
          -> vibrium_adblock_check (C ABI) -> Brave adblock-rust
          blocked => RV_CANCEL        allowed / error => RV_CONTINUE
```

| File | Role |
|---|---|
| `adblock_engine.*` | Owns a Rust engine handle (`shared_ptr`, safe replacement). |
| `request_policy.*` | CEF-free policy: what is checked, type mapping, cosmetic script. |
| `adblock_handler.*` | CEF request/redirect handlers (IO thread) + decision stats. |
| `adblock_service.*` | Facade used by `main.cc`: init, request handler, cosmetic injection. |
| `../selftest.*` | `--selftest` end-to-end check used by CI. |

Hooks in `main.cc`: `BrowserClient` returns the shared request handler and
calls `ApplyCosmeticFilters` from `OnLoadStart`; `wWinMain` calls
`AdblockService::Get().Initialize(...)` once before the first browser exists.

## Behaviour and failure policy

- **Fails open.** No engine, non-http(s) URL, engine error, or panic in Rust
  means the request continues. Nothing is blocked because the engine broke.
- **Main-frame navigations are never blocked**, nor are `chrome://`, `devtools://`,
  `data:`, `blob:`, `file:` or `about:` URLs (so your new-tab page is safe).
- **Source page** (for `$third-party`, `$domain`): top-level document URL, else
  the request initiator, else the referrer. Like Brave, not the issuing iframe.
- **Redirects** are re-checked. CEF cannot cancel inside `OnResourceRedirect`,
  so a blocked redirect is retargeted to `about:blank`, which fails the load.
- **Threading:** adblock-rust is built *without* its `single-thread` feature so
  the engine is `Send + Sync` (enforced by a compile-time test).

## Filter lists

`<dir>/*.txt`, where `<dir>` is `--filter-dir=<dir>` or `<exe dir>/filters`
(never the working directory). Each file goes through the existing Rust
validator; invalid lists are skipped and valid ones still load. CI copies
`Vibrium/filters/` next to the exe and **fails the build if it is missing or has
no `*.txt`**. Two selftests then run: one against a fixture list, and one with no
`--filter-dir`, which proves the *packaged* filters are found and block. The
shipped `vibrium-baseline.txt` ends with sentinel rules that this relies on.

### Default lists and updates

With no `--filter-dir`, lists live in `%LOCALAPPDATA%\Vibrium\filters` (the install
dir is usually read-only). Startup copies the shipped baseline there, loads whatever
is cached, then `FilterListUpdater` (`filter_list_updater.*`) downloads the lists in
`filter_list_catalog.h` with CEF's network stack: conditional requests (ETag), 10 MB
cap, each body checked with `vibrium_adblock_validate_filter_response`, atomic
replace, then the engine is rebuilt on a background thread and swapped in via
`AdblockEngineHolder::Set()`. Re-checked every 6 h; files younger than 24 h are
skipped. `--filter-dir=<dir>` = exactly that dir, no network. `--no-filter-updates`
= user dir, no network. First launch is protected only by the baseline until the
first download finishes (seconds); `tools/fetch_filter_lists.py <dir>` pre-seeds.

### Measuring coverage

`cargo run --release --example turtlecute_bench -- <filters-dir> <adblock_data.json>`
(in `rust/vibrium-adblock`) replays adblock.turtlecute.org's checks against the real
engine (`adblock_data.json` is from github.com/Turtlecute33/toolz, `src/data/`).
Its score is a *floor*: the real site also counts DNS failures as "blocked".

| Lists | Estimated score |
|---|---|
| baseline only | 7% |
| + EasyList, EasyPrivacy, uBO lists | 67% |
| + HaGeZi Light (shipped default) | ~92-96% |

Not implemented yet: persisted per-list enable/disable state in the UI.

## Cosmetic filtering

On `OnLoadStart` the service runs three things in the page (all CSP-safe, since
`ExecuteJavaScript` is not subject to the page's CSP):

1. **Hiding CSS** for plain selectors (`##.banner`), as a constructable stylesheet.
2. **`cosmetic_content.js`**, an in-page script (embedded in the binary by
   `tools/embed_cosmetic_js.py`; CI fails if `cosmetic_content_js.inc` is stale):
   * **Procedural filters**: `:has-text()`, `:matches-attr()`, `:matches-css*()`,
     `:matches-path()`, `:min-text-length()`, `:upward()`, `:xpath()` and the
     actions `:style()`, `:remove()`, `:remove-attr()`, `:remove-class()`. They are
     re-run (debounced) as the DOM changes.
   * **Generic selectors** (`##.ad`, `###banner`, with exceptions and
     `$generichide`): the script reports the class names / ids it sees through
     `window.cefQuery`; `CosmeticQueryHandler` answers with CSS from the engine.
     This needs the renderer half in `VibriumApp` (`main.cc`).
3. **Scriptlets** (`example.com##+js(name, args)`), only if a `resources.json` is
   next to the filter lists. Not bundled (licensing, see `THIRD_PARTY_NOTICES.md`).
   Limitation: injected via `ExecuteJavaScript` after load start, so a scriptlet is
   **not guaranteed to run before the page's own scripts**; scriptlets that must
   win that race (`set-constant`, `abort-on-property-read`) can be too late.

The page is untrusted and `window.cefQuery` is reachable by any script on it, so
the handler takes the URL from the browser's own frame, accepts one fixed request
format with a size cap, and the Rust side re-validates and bounds the input.

## Build (Windows, MSVC, Release only)

`cmake/vibrium_adblock.cmake` (included from `CMakeLists.txt`) builds
`rust/vibrium-adblock` with a static CRT to match CEF's `/MT` and links it.
Needs `cargo` on PATH; `rust-toolchain.toml` pins the Rust version.

## Sandbox (IMPORTANT)

`main.cc` sets `settings.no_sandbox = true`. That is acceptable for development
only. A release build must run with the Windows sandbox: with current CEF this
means building Vibrium as a DLL that exports `RunWinMain` and launching it via
CEF's `bootstrap.exe` (see `tests/cefsimple/` in the CEF sources). This is a
separate change from ad blocking and has not been made yet.

## Selftest

```powershell
python Vibrium/tests/native/selftest_server.py 8123      # in another shell
vibrium.exe --selftest --url=http://localhost:8123/selftest.html `
  --filter-dir=Vibrium/tests/fixtures/selftest-lists --selftest-report=report.txt
```
Loads a page requesting blocked, allowed and redirected resources, checks the
engine's decisions and the cosmetic result, closes cleanly and exits 0 or 1.
