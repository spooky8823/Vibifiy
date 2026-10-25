# Vibrium Tech Stack

| Component | Technology | Purpose |
|---|---|---|
| Browser Engine | Chromium | Core browser foundation |
| HTML Engine | Blink | HTML parsing and rendering |
| CSS Engine | Blink | CSS styling, layout, and rendering |
| JavaScript Engine | V8 | Executes website JavaScript |
| Backend | C++ | Chromium integration and browser internals |
| Additional Backend | Rust | Security-sensitive services and ad-blocking integration |
| UI Framework | Chromium Views + WebUI | Native browser controls and web-based UI |
| UI Markup | HTML | Structures WebUI components |
| UI Styling | CSS | Styles the Vibrium interface |
| UI Scripting | TypeScript | Type-safe UI logic and interactions |
| Ad-Blocking Engine | [Brave adblock-rust](https://github.com/brave/adblock-rust) | Native ad and tracker blocking |
| Filter Lists | EasyList + EasyPrivacy | Ad and tracker filtering rules |
| Extension Support | Chromium Extensions | Compatible browser extensions |
| Google Authentication | Standard web authentication flows | Supports Sign in with Google and account selection |
| Build System | GN + Ninja | Configures and builds Chromium |
| Version Control | Git | Source control and patch management |
| Testing | Automated tests + browser benchmarks | Validates functionality, privacy, and performance |
| CI/CD | Automated build and test pipelines | Tests changes and prepares releases |
| Distribution | Signed installers + secure updater | Distributes Vibrium and security updates |
| Performance | Chromium profiling tools | Measures memory consumption, CPU usage, and responsiveness |
