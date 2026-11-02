// CEF-independent request policy: which requests are evaluated, how CEF
// resource types map onto adblock-rust request types, and how cosmetic CSS is
// delivered to a page. Kept free of CEF types so it is unit-testable.

#ifndef VIBRIUM_REQUEST_POLICY_H_
#define VIBRIUM_REQUEST_POLICY_H_

#include <cstddef>
#include <optional>
#include <string>

namespace vibrium {

enum class ResourceKind {
  kMainFrame,
  kSubFrame,
  kStylesheet,
  kScript,
  kImage,
  kFont,
  kMedia,
  kXhr,
  kPing,
  kCspReport,
  kObject,
  kOther,
};

// Request-type string understood by adblock-rust.
const char* AdblockTypeName(ResourceKind kind);

// True for http:// and https:// URLs (scheme compared case-insensitively).
// chrome://, devtools://, data:, blob:, file:, about: etc. are never filtered.
bool IsFilterableUrl(const std::string& url);

// Whether a request should be handed to the engine at all.
// Main-frame navigations are never blocked: blocking them would leave the user
// on a blank page with no explanation and could lock the browser out of sites.
bool ShouldEvaluate(ResourceKind kind, const std::string& url);

// Quotes `utf8` as a JavaScript string literal that is also safe to embed in
// an HTML <script> element (escapes quotes, backslashes, control characters,
// U+2028/U+2029 and '<').
std::string JsStringLiteral(const std::string& utf8);

// Script that applies `css` to the current document. Uses a constructable
// stylesheet, which, unlike an inline <style> element, is not blocked by a
// page's Content-Security-Policy; falls back to <style> if unsupported.
std::string BuildCosmeticScript(const std::string& css);

// Script for ExecuteJavaScript that runs the cosmetic content script
// (cosmetic_content.js) with `payload_json` from
// AdblockEngine::CosmeticPayload(). The payload is embedded as a JS string
// literal, so it cannot break out into code.
std::string BuildContentScript(const std::string& payload_json);

// Wraps scriptlet code from AdblockEngine::CosmeticScript() so an exception in
// it stays contained. Run it as its own ExecuteJavaScript call: a syntax error
// in a scriptlet must not stop the content script from being compiled.
std::string BuildScriptletScript(const std::string& scriptlet);

// What a page asks for through window.cefQuery: the class names and ids it
// contains, as two JSON arrays.
struct GenericQuery {
  std::string classes_json;
  std::string ids_json;
};

// Parses "G\n<classes json>\n<ids json>". The input comes from an untrusted
// page: anything else (wrong marker, extra lines, oversize) yields nullopt.
// The JSON is NOT validated here; the Rust side does that.
std::optional<GenericQuery> ParseGenericQuery(const std::string& request);

// Largest accepted generic query, in bytes.
inline constexpr size_t kMaxGenericQueryBytes = 600 * 1024;

}  // namespace vibrium

#endif  // VIBRIUM_REQUEST_POLICY_H_
