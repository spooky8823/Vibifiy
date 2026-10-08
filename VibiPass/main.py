"""
VibiPass - PyWebView Desktop App
Pre-loads storage data into HTML so it's available before any script runs.
"""

import webview
import json
import os
import sys
import threading
import socketserver
import socket
from http.server import SimpleHTTPRequestHandler


def get_base_dir() -> str:
    if getattr(sys, "frozen", False):
        return sys._MEIPASS
    return os.path.dirname(os.path.abspath(__file__))


def get_app_dir() -> str:
    if sys.platform == "win32":
        base = os.environ.get("APPDATA", os.path.expanduser("~"))
    elif sys.platform == "darwin":
        base = os.path.expanduser("~/Library/Application Support")
    else:
        base = os.path.expanduser("~/.local/share")
    data_dir = os.path.join(base, "VibiPass")
    os.makedirs(data_dir, exist_ok=True)
    return data_dir


def get_storage_path() -> str:
    return os.path.join(get_app_dir(), "store.json")


def find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


# Global storage instance
_storage = None


class StorageBackend:
    def __init__(self):
        self._lock = threading.Lock()
        self._path = get_storage_path()
        self._data = self._load()
        print(f"[VibiPass] Storage path: {self._path}")
        print(f"[VibiPass] Existing keys: {list(self._data.keys())}")

    def _load(self) -> dict:
        try:
            with open(self._path, "r", encoding="utf-8") as f:
                return json.load(f)
        except (FileNotFoundError, json.JSONDecodeError):
            return {}

    def _save(self):
        with open(self._path, "w", encoding="utf-8") as f:
            json.dump(self._data, f, ensure_ascii=False, indent=2)

    def get(self, key):
        with self._lock:
            return self._data.get(key)

    def set(self, key, value):
        with self._lock:
            self._data[key] = value
            self._save()
            print(f"[VibiPass] set({key}) ✅")

    def remove(self, key):
        with self._lock:
            self._data.pop(key, None)
            self._save()

    def clear(self):
        with self._lock:
            self._data.clear()
            self._save()

    def all(self):
        with self._lock:
            return dict(self._data)


class StorageBridge:
    """Exposed to JS as window.pywebview.api"""

    def getItem(self, key: str):
        val = _storage.get(key)
        print(f"[VibiPass] getItem({key}) = {str(val)[:40] if val else None}")
        return val

    def setItem(self, key: str, value: str):
        _storage.set(key, value)
        return True

    def removeItem(self, key: str):
        _storage.remove(key)
        return True

    def clear(self):
        _storage.clear()
        return True

    def getAllKeys(self):
        keys = list(_storage.all().keys())
        print(f"[VibiPass] getAllKeys() = {keys}")
        return keys


def make_shim(data: dict) -> str:
    """Generate a shim script with data pre-loaded — no async API calls needed."""
    data_json = json.dumps(data)
    return f"""<script>
(function() {{
  // Data pre-loaded from disk — available instantly, no async needed
  var _preloaded = {data_json};
  var _cache = Object.assign({{}}, _preloaded);

  function persist(key, value) {{
    // Also write back via pywebview API when available
    function tryWrite() {{
      if (window.pywebview && window.pywebview.api) {{
        try {{ window.pywebview.api.setItem(key, value); }} catch(e) {{}}
      }} else {{
        setTimeout(tryWrite, 50);
      }}
    }}
    tryWrite();
  }}

  function persistRemove(key) {{
    function tryRemove() {{
      if (window.pywebview && window.pywebview.api) {{
        try {{ window.pywebview.api.removeItem(key); }} catch(e) {{}}
      }} else {{
        setTimeout(tryRemove, 50);
      }}
    }}
    tryRemove();
  }}

  function persistClear() {{
    function tryClear() {{
      if (window.pywebview && window.pywebview.api) {{
        try {{ window.pywebview.api.clear(); }} catch(e) {{}}
      }} else {{
        setTimeout(tryClear, 50);
      }}
    }}
    tryClear();
  }}

  var store = {{
    getItem: function(key) {{
      return Object.prototype.hasOwnProperty.call(_cache, key) ? _cache[key] : null;
    }},
    setItem: function(key, value) {{
      _cache[key] = String(value);
      persist(key, String(value));
    }},
    removeItem: function(key) {{
      delete _cache[key];
      persistRemove(key);
    }},
    clear: function() {{
      Object.keys(_cache).forEach(function(k) {{ delete _cache[k]; }});
      persistClear();
    }},
    key: function(index) {{ return Object.keys(_cache)[index] || null; }},
    get length() {{ return Object.keys(_cache).length; }}
  }};

  try {{
    Object.defineProperty(window, 'localStorage', {{
      get: function() {{ return store; }},
      configurable: true
    }});
  }} catch(e) {{ window.localStorage = store; }}

  console.log('[VibiPass] localStorage ready with keys:', Object.keys(_cache));
}})();
</script>"""


def make_handler(base_dir):
    class DataInjectingHandler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=base_dir, **kwargs)

        def log_message(self, format, *args): pass
        def log_error(self, format, *args): pass

        def do_GET(self):
            clean_path = self.path.split('?')[0].lstrip('/')
            file_path = os.path.join(base_dir, clean_path)

            if clean_path.endswith('.html') and os.path.isfile(file_path):
                try:
                    with open(file_path, 'r', encoding='utf-8') as f:
                        content = f.read()

                    # Inject shim with current data pre-loaded
                    shim = make_shim(_storage.all())
                    if '<head>' in content:
                        content = content.replace('<head>', '<head>' + shim, 1)
                    elif '<HEAD>' in content:
                        content = content.replace('<HEAD>', '<HEAD>' + shim, 1)
                    else:
                        content = shim + content

                    encoded = content.encode('utf-8')
                    self.send_response(200)
                    self.send_header('Content-Type', 'text/html; charset=utf-8')
                    self.send_header('Content-Length', str(len(encoded)))
                    self.send_header('Cache-Control', 'no-cache, no-store')
                    self.end_headers()
                    self.wfile.write(encoded)
                    return
                except Exception as e:
                    print(f"[VibiPass] Handler error: {e}")

            super().do_GET()

    return DataInjectingHandler


def start_server(base_dir: str, port: int):
    handler = make_handler(base_dir)
    httpd = socketserver.TCPServer(('127.0.0.1', port), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd


_window = None


def main():
    global _storage, _window

    _storage = StorageBackend()
    base_dir = get_base_dir()

    port = find_free_port()
    start_server(base_dir, port)

    start_page = "auth.html" if _storage.get("vibipass_profile") else "landing.html"
    url = f"http://127.0.0.1:{port}/html/{start_page}"
    print(f"[VibiPass] Starting on: {url}")

    _window = webview.create_window(
        title="VibiPass",
        url=url,
        js_api=StorageBridge(),
        width=1024,
        height=700,
        min_size=(800, 560),
        text_select=False,
        confirm_close=False,
    )

    webview.start(debug=False)


if __name__ == "__main__":
    main()
