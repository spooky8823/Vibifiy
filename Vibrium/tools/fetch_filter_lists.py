#!/usr/bin/env python3
"""Download Vibrium's default filter lists into a directory.

Keeps the URL list in sync with c++/adblock/filter_list_catalog.h (a CI/unit
check could parse both). Use it to seed a dev profile or a package:

    python tools/fetch_filter_lists.py <dir>          # e.g. %LOCALAPPDATA%\\Vibrium\\filters

Redistributing the downloaded files inside an installer is a licence decision
(GPL / CC BY-SA); by default Vibrium downloads them on the user's machine.
A list that fails to download is skipped; the others are still written.
"""
import pathlib, re, sys, urllib.request

HEADER = pathlib.Path(__file__).resolve().parent.parent / "c++/adblock/filter_list_catalog.h"
MAX = 10 * 1024 * 1024

def catalog():
    text = HEADER.read_text(encoding="utf-8")
    return re.findall(r'\{"([a-z0-9-]+)",\s*"(https://[^"]+)"\}', text)

def main(out_dir):
    out = pathlib.Path(out_dir); out.mkdir(parents=True, exist_ok=True)
    ok = 0
    for name, url in catalog():
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "Vibrium-list-fetch"})
            body = urllib.request.urlopen(req, timeout=60).read(MAX + 1)
            if len(body) > MAX or body.lstrip()[:1] in (b"<", b"{"):
                raise ValueError("too large or not a filter list")
            tmp = out / f".{name}.tmp"; tmp.write_bytes(body); tmp.replace(out / f"{name}.txt")
            print(f"ok    {name:<16} {len(body):>9} bytes"); ok += 1
        except Exception as e:
            print(f"FAIL  {name:<16} {e}")
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]) if len(sys.argv) == 2 else sys.exit(__doc__))
