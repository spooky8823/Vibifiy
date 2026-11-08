# Third-party notices

Vibrium is built from the components below. Each keeps its own license; the
license texts ship with the respective sources or distributions.

## Brave adblock-rust (`rust/adblock-rust/`)

- Project: https://github.com/brave/adblock-rust
- License: **MPL-2.0** (see `rust/adblock-rust/LICENSE`)
- Vendored snapshot. Verified: its `src/` is identical to upstream commit
  `6028a6d` (the same sources as tag v0.13.4). Its manifest still reports
  version 0.13.3, and other files (lockfile, upstream CI files, test data)
  differ from the tags, so treat it as an upstream snapshot between releases.
- MPL-2.0 is file-level copyleft: modifications to its files must be published
  under MPL-2.0. `rust/vibrium-adblock/` is also MPL-2.0 (see its `Cargo.toml`).

## Chromium Embedded Framework (CEF)

- Project: https://bitbucket.org/chromiumembedded/cef (mirror:
  https://github.com/chromiumembedded/cef)
- Pinned build: see `cef-version.txt` (downloaded in CI, never committed).
- Codec build: the pinned archive is a third-party build of the same CEF
  version with `proprietary_codecs=true ffmpeg_branding=Chrome`
  (https://github.com/psmux/godwinmix-cef), verified in CI by pinned SHA1 and
  SHA256. **H.264 and AAC are patent-licensed technologies. These flags enable
  the decoders but do not grant a patent licence; review this before
  distributing Vibrium widely.**
- License: BSD-3-Clause for CEF itself (`LICENSE.txt` in the distribution).
  The distribution also contains Chromium and its third-party components under
  their own licenses; **any redistributed Vibrium build must include the
  distribution's `LICENSE.txt` and the Chromium credits.**

## Filter lists

Vibrium ships **no** filter lists by default. If you bundle lists (for example
EasyList/EasyPrivacy, which the vendored adblock-rust keeps under
`rust/adblock-rust/data/`), each list has its own license and attribution
requirements; add them here before distributing.

## Rust crates

Resolved versions and checksums are in `rust/Cargo.lock`. Most are under
permissive licenses (MIT, Apache-2.0, Unicode-3.0, Unlicense). The
`css-validation` feature of adblock-rust (needed for procedural cosmetic
filters such as `:has-text()`) adds Servo's CSS stack, three crates of which are
**MPL-2.0** (file-level copyleft, the same licence as Vibrium):

| Crate | Licence |
|-------|---------|
| `cssparser` 0.35 | MPL-2.0 |
| `selectors` 0.32 | MPL-2.0 |
| `dtoa-short` 0.3 | MPL-2.0 |
| `servo_arc`, `precomputed-hash`, `phf*` and their helpers | MIT / Apache-2.0 |

Regenerate the full list with `cargo metadata --locked` before a release.

Filter lists are not bundled except `filters/vibrium-baseline.txt`, which is
original first-party work under MPL-2.0. No scriptlet library (`resources.json`)
is bundled; uBlock Origin's is GPL-3.0, so shipping one is a deliberate
licensing decision for the maintainers.
