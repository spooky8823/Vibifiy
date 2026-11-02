#include "request_policy.h"

#include <cctype>
#include <cstdio>

namespace vibrium {

namespace {
#include "cosmetic_content_js.inc"
}  // namespace

const char* AdblockTypeName(ResourceKind kind) {
  switch (kind) {
    case ResourceKind::kMainFrame:
      return "document";
    case ResourceKind::kSubFrame:
      return "subdocument";
    case ResourceKind::kStylesheet:
      return "stylesheet";
    case ResourceKind::kScript:
      return "script";
    case ResourceKind::kImage:
      return "image";
    case ResourceKind::kFont:
      return "font";
    case ResourceKind::kMedia:
      return "media";
    case ResourceKind::kXhr:
      return "xhr";
    case ResourceKind::kPing:
      return "ping";
    case ResourceKind::kCspReport:
      return "csp_report";
    case ResourceKind::kObject:
      return "object";
    case ResourceKind::kOther:
      break;
  }
  return "other";
}

namespace {
bool StartsWithIgnoreCase(const std::string& text, const char* prefix) {
  for (size_t i = 0; prefix[i] != '\0'; ++i) {
    if (i >= text.size() ||
        std::tolower(static_cast<unsigned char>(text[i])) != prefix[i]) {
      return false;
    }
  }
  return true;
}
}  // namespace

bool IsFilterableUrl(const std::string& url) {
  return StartsWithIgnoreCase(url, "http://") ||
         StartsWithIgnoreCase(url, "https://");
}

bool ShouldEvaluate(ResourceKind kind, const std::string& url) {
  return kind != ResourceKind::kMainFrame && IsFilterableUrl(url);
}

std::string JsStringLiteral(const std::string& utf8) {
  std::string out = "\"";
  out.reserve(utf8.size() + 2);
  for (size_t i = 0; i < utf8.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(utf8[i]);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      case '<':  // keeps "</script>" and "<!--" inert if ever embedded in HTML
        out += "\\u003c";
        break;
      default:
        if (c < 0x20 || c == 0x7f) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else if (c == 0xE2 && i + 2 < utf8.size() &&
                   static_cast<unsigned char>(utf8[i + 1]) == 0x80 &&
                   (static_cast<unsigned char>(utf8[i + 2]) == 0xA8 ||
                    static_cast<unsigned char>(utf8[i + 2]) == 0xA9)) {
          // U+2028 / U+2029 were line terminators in pre-ES2019 JavaScript.
          out += (static_cast<unsigned char>(utf8[i + 2]) == 0xA8)
                     ? "\\u2028"
                     : "\\u2029";
          i += 2;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
  return out;
}

std::string BuildCosmeticScript(const std::string& css) {
  return "(function(){try{var css=" + JsStringLiteral(css) +
         ";var root=document.documentElement;"
         "function apply(){try{var s=new CSSStyleSheet();s.replaceSync(css);"
         "document.adoptedStyleSheets=document.adoptedStyleSheets.concat([s]);}"
         "catch(e){var st=document.createElement('style');st.textContent=css;"
         "(document.head||document.documentElement).appendChild(st);}}"
         "if(root){apply();}else{document.addEventListener("
         "'readystatechange',apply,{once:true});}}catch(e){}})();";
}

std::string BuildContentScript(const std::string& payload_json) {
  std::string script = "(";
  script.append(reinterpret_cast<const char*>(kCosmeticContentScript),
                sizeof(kCosmeticContentScript));
  script += ")(" + JsStringLiteral(payload_json) + ");";
  return script;
}

std::string BuildScriptletScript(const std::string& scriptlet) {
  return "(function(){try{" + scriptlet + "\n}catch(e){}})();";
}

std::optional<GenericQuery> ParseGenericQuery(const std::string& request) {
  if (request.size() > kMaxGenericQueryBytes || request.size() < 6 ||
      request.compare(0, 2, "G\n") != 0) {
    return std::nullopt;
  }
  const size_t first = 2;
  const size_t second = request.find('\n', first);
  if (second == std::string::npos) {
    return std::nullopt;
  }
  // Exactly three lines: a further newline means a malformed request.
  if (request.find('\n', second + 1) != std::string::npos) {
    return std::nullopt;
  }
  GenericQuery query;
  query.classes_json = request.substr(first, second - first);
  query.ids_json = request.substr(second + 1);
  if (query.classes_json.empty() || query.ids_json.empty()) {
    return std::nullopt;
  }
  return query;
}

}  // namespace vibrium
