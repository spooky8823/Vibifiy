// Copyright 2026 The Vibrium Authors
// SPDX-License-Identifier: MPL-2.0
//
// Tests the CEF-independent C++ layer (engine ownership + request policy)
// against the real Rust library. No CEF binaries are required.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "adblock_engine.h"
#include "request_policy.h"
#include "scriptlet_engine.h"

namespace {

int failures = 0;

void Expect(bool condition, const char* description) {
  if (condition) {
    std::cout << "[PASS] " << description << '\n';
  } else {
    std::cerr << "[FAIL] " << description << '\n';
    ++failures;
  }
}

using vibrium::AdblockEngine;
using vibrium::ResourceKind;

const char* kRules =
    "||ads.vibrium-test.example^\n"
    "||tracker.vibrium-test.example^$third-party\n"
    "vibrium-test.example##.sponsored\n";

void TestDecisionsAndFailOpen() {
  auto engine = AdblockEngine::FromRules(kRules);
  Expect(engine != nullptr, "engine is created from rules");
  if (!engine) return;

  const std::string page = "https://www.vibrium-test.example/";
  Expect(engine->Check("https://ads.vibrium-test.example/a.js", page,
                       vibrium::AdblockTypeName(ResourceKind::kScript)) ==
             AdblockEngine::Decision::kBlock,
         "matching script request is blocked");
  Expect(engine->Check("https://cdn.vibrium-test.example/a.js", page,
                       vibrium::AdblockTypeName(ResourceKind::kScript)) ==
             AdblockEngine::Decision::kAllow,
         "non-matching request is allowed");
  Expect(engine->Check("https://tracker.vibrium-test.example/p.gif", page,
                       "image") == AdblockEngine::Decision::kAllow,
         "first-party request is not treated as third-party");
  Expect(engine->Check("https://tracker.vibrium-test.example/p.gif",
                       "https://other.example/", "image") ==
             AdblockEngine::Decision::kBlock,
         "third-party request matching $third-party is blocked");
  Expect(engine->Check("not a url", page, "script") ==
             AdblockEngine::Decision::kError,
         "malformed URL yields kError (callers fail open)");
}

void TestCosmetic() {
  auto engine = AdblockEngine::FromRules(kRules);
  if (!engine) return;
  auto css = engine->CosmeticCss("https://www.vibrium-test.example/page");
  Expect(css && css->find(".sponsored{display:none!important}") !=
                    std::string::npos,
         "cosmetic CSS is returned for a matching page");
  Expect(!engine->CosmeticCss("https://unrelated.example/").has_value(),
         "no cosmetic CSS for an unrelated page");
  Expect(!engine->CosmeticCss("chrome://settings").has_value(),
         "no cosmetic CSS for internal pages");
}

void TestDirectoryLoading() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "vibrium-native-test-lists";
  fs::remove_all(dir);
  fs::create_directories(dir);

  Expect(AdblockEngine::FromDirectory(dir.string()) == nullptr,
         "empty directory yields no engine");
  Expect(AdblockEngine::FromDirectory((dir / "missing").string()) == nullptr,
         "missing directory yields no engine");

  std::ofstream(dir / "bad.txt") << "   \n";
  Expect(AdblockEngine::FromDirectory(dir.string()) == nullptr,
         "directory with only an invalid list yields no engine");

  std::ofstream(dir / "good.txt") << kRules;
  auto engine = AdblockEngine::FromDirectory(dir.string());
  Expect(engine != nullptr, "valid list loads even next to an invalid one");
  if (engine) {
    Expect(engine->Check("https://ads.vibrium-test.example/a.js",
                         "https://www.vibrium-test.example/", "script") ==
               AdblockEngine::Decision::kBlock,
           "rules loaded from directory are enforced");
  }
  fs::remove_all(dir);
}

void TestPolicy() {
  Expect(!vibrium::ShouldEvaluate(ResourceKind::kMainFrame,
                                  "https://ads.vibrium-test.example/"),
         "main-frame navigations are never evaluated");
  Expect(vibrium::ShouldEvaluate(ResourceKind::kSubFrame,
                                 "https://ads.vibrium-test.example/"),
         "sub-frame requests are evaluated");
  for (const char* url : {"chrome://settings", "devtools://devtools/x",
                          "data:text/html,hi", "blob:https://a/b", "file:///x",
                          "about:blank", "chrome-extension://id/x", ""}) {
    Expect(!vibrium::ShouldEvaluate(ResourceKind::kScript, url),
           (std::string("internal/non-network URL skipped: ") + url).c_str());
  }
  Expect(vibrium::ShouldEvaluate(ResourceKind::kImage, "HTTPS://Example.com/x"),
         "scheme comparison is case-insensitive");
  Expect(std::string(vibrium::AdblockTypeName(ResourceKind::kXhr)) == "xhr" &&
             std::string(vibrium::AdblockTypeName(ResourceKind::kSubFrame)) ==
                 "subdocument" &&
             std::string(vibrium::AdblockTypeName(ResourceKind::kOther)) ==
                 "other",
         "resource kinds map to adblock request types");
}

void TestJsEscaping() {
  const std::string hostile = "a\"b\\c\n</script><!--\xE2\x80\xA8\x01";
  const std::string lit = vibrium::JsStringLiteral(hostile);
  Expect(lit.find('\n') == std::string::npos &&
             lit.find("</script>") == std::string::npos &&
             lit.find("\xE2\x80\xA8") == std::string::npos &&
             lit.find("\\u2028") != std::string::npos &&
             lit.find("\\u0001") != std::string::npos &&
             lit.front() == '"' && lit.back() == '"',
         "JsStringLiteral neutralises quotes, newlines, </script>, U+2028");
  Expect(vibrium::BuildCosmeticScript(".ad{display:none!important}")
                 .find("adoptedStyleSheets") != std::string::npos,
         "cosmetic script uses a CSP-safe constructable stylesheet");
}

// Generic class/id selectors, procedural filters and scriptlets through the
// C++ wrapper, including hostile page input.
void TestCosmeticPipeline() {
  auto engine = AdblockEngine::FromRules(
      "##.generic-ad\n"
      "###sponsor-box\n"
      "vibrium-test.example##.banner\n"
      "vibrium-test.example##.promo:has-text(Sponsored)\n"
      "exempt.example#@#.generic-ad\n");
  Expect(engine != nullptr, "cosmetic pipeline engine is created");
  if (!engine) return;

  const std::string page = "https://www.vibrium-test.example/";
  auto payload = engine->CosmeticPayload(page);
  Expect(payload.has_value() &&
             payload->find(".banner{display:none!important}") !=
                 std::string::npos,
         "payload carries plain hiding CSS");
  Expect(payload.has_value() && payload->find("has-text") != std::string::npos,
         "payload carries procedural filters");
  Expect(payload.has_value() &&
             payload->find("\"generichide\":false") != std::string::npos,
         "payload says generic lookups are allowed");

  auto generic = engine->GenericCss(page, "[\"generic-ad\",\"other\"]",
                                    "[\"sponsor-box\"]");
  Expect(generic.has_value() &&
             generic->find(".generic-ad{display:none!important}") !=
                 std::string::npos &&
             generic->find("#sponsor-box") != std::string::npos,
         "generic CSS matches reported class names and ids");
  Expect(!engine->GenericCss(page, "[\"other\"]", "[]").has_value(),
         "unmatched names produce no CSS");
  Expect(!engine->GenericCss("https://exempt.example/", "[\"generic-ad\"]",
                             "[]")
              .has_value(),
         "generic exceptions are honored");

  // The page controls these strings; none may yield a result or crash.
  for (const char* bad : {"", "not json", "{}", "[1]", "[\"x", "null"}) {
    Expect(!engine->GenericCss(page, bad, "[]").has_value() &&
               !engine->GenericCss(page, "[]", bad).has_value(),
           "malformed generic query is rejected");
  }
  Expect(!engine->GenericCss(page, std::string("[\"") + std::string(300000, 'a') + "\"]", "[]")
              .has_value(),
         "oversized generic query is rejected");
  Expect(!engine->GenericCss("file:///c:/x.html", "[\"generic-ad\"]", "[]")
              .has_value(),
         "non-http pages are not served generic CSS");
  Expect(!engine->CosmeticScript(page).has_value(),
         "no scriptlet without installed resources");
}

void TestContentScriptHelpers() {
  const std::string script =
      vibrium::BuildContentScript("{\"css\":\"</script>\\u2028\"}");
  Expect(script.rfind("(function", 0) == 0 && script.back() == ';',
         "content script is a self-invoking expression");
  Expect(script.find("MutationObserver") != std::string::npos &&
             script.find("cefQuery") != std::string::npos,
         "content script contains the real implementation");
  Expect(script.find("</script>") == std::string::npos,
         "payload is escaped inside the script");
  Expect(script.find("\n//") == std::string::npos &&
             script.find("Vibrium cosmetic-filtering") == std::string::npos,
         "embedded script has its comment-only lines stripped");

  const std::string scriptlet = vibrium::BuildScriptletScript("var a = 1;");
  Expect(scriptlet.find("try{var a = 1;") != std::string::npos &&
             scriptlet.find("catch(e)") != std::string::npos,
         "scriptlets run inside a try/catch");

  auto query = vibrium::ParseGenericQuery("G\n[\"a\",\"b\"]\n[\"id\"]");
  Expect(query.has_value() && query->classes_json == "[\"a\",\"b\"]" &&
             query->ids_json == "[\"id\"]",
         "well-formed generic query is split into classes and ids");
  for (const char* bad :
       {"", "G", "G\n", "G\n[]", "G\n[]\n", "G\n\n[]", "X\n[]\n[]",
        "G\n[]\n[]\nextra", "g\n[]\n[]", "[]\n[]\n[]"}) {
    Expect(!vibrium::ParseGenericQuery(bad).has_value(),
           "malformed generic request is rejected");
  }
  Expect(!vibrium::ParseGenericQuery(
              "G\n[\"" + std::string(vibrium::kMaxGenericQueryBytes, 'a') +
              "\"]\n[]")
              .has_value(),
         "oversized generic request is rejected");
}


// ---------------------------------------------------------------------------
// Scriptlets (renderer side)
// ---------------------------------------------------------------------------

// A scratch directory that is removed when it goes out of scope.
class ScratchDir {
 public:
  ScratchDir() {
    static int counter = 0;
    path_ = std::filesystem::temp_directory_path() /
            ("vibrium-scriptlet-test-" + std::to_string(counter++) + "-" +
             std::to_string(std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count()));
    std::filesystem::create_directories(path_);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;

  std::string str() const { return path_.string(); }
  void Write(const std::string& name, const std::string& text) const {
    std::ofstream out(path_ / name, std::ios::binary | std::ios::trunc);
    out << text;
  }

 private:
  std::filesystem::path path_;
};

// safe.fn (helper) and plain.js (a scriptlet that depends on it).
const char* kScriptletResources =
    R"([{"name":"safe.fn","aliases":[],"kind":{"mime":"fn/javascript"},"content":"ZnVuY3Rpb24gc2FmZUhlbHBlcigpe3JldHVybiAxO30="},)"
    R"({"name":"plain.js","aliases":[],"kind":{"mime":"application/javascript"},)"
    R"("content":"ZnVuY3Rpb24gcGxhaW4oYSl7IHNlbGYuX19wbGFpbiA9IGE7IH0=","dependencies":["safe.fn"]}])";

bool Contains(const std::optional<std::string>& text, const char* needle) {
  return text.has_value() && text->find(needle) != std::string::npos;
}

void TestScriptletDirectorySignature() {
  ScratchDir dir;
  dir.Write("ubo-a.txt", "example.com##+js(plain, a)\n");
  const uint64_t base = vibrium::ScriptletDirectorySignature(dir.str());

  Expect(base == vibrium::ScriptletDirectorySignature(dir.str()),
         "signature is stable while nothing changes");

  dir.Write(".ubo-a.tmp", "x");
  dir.Write(".hidden.txt", "x");
  dir.Write("ubo-a.etag", "x");
  Expect(base == vibrium::ScriptletDirectorySignature(dir.str()),
         "temp files, hidden files and .etag sidecars are ignored");

  dir.Write("ubo-a.txt", "example.com##+js(plain, a)\nother.org##+js(plain, b)\n");
  const uint64_t edited = vibrium::ScriptletDirectorySignature(dir.str());
  Expect(edited != base, "signature changes when a list is rewritten");

  dir.Write("resources.json", "[]");
  Expect(vibrium::ScriptletDirectorySignature(dir.str()) != edited,
         "signature changes when resources.json appears");

  Expect(vibrium::ScriptletDirectorySignature(
             (std::filesystem::temp_directory_path() / "vibrium-no-such-dir")
                 .string()) == 0,
         "a missing directory has an empty signature");
}

void TestScriptletProvider() {
  ScratchDir dir;
  // Interval 0: re-check the directory on every call.
  vibrium::ScriptletProvider provider(dir.str(), std::chrono::milliseconds(0));

  Expect(!provider.ScriptFor("https://example.com/").has_value(),
         "no scriptlets while the directory is empty (renderer starts first)");

  // The first list download finishes after the renderer has started.
  dir.Write("resources.json", kScriptletResources);
  dir.Write("ubo-test.txt", "example.com##+js(plain, hello)\n");

  const auto script = provider.ScriptFor("https://example.com/");
  Expect(Contains(script, "plain(\"hello\")"),
         "scriptlets appear once the files exist, without a restart");
  Expect(Contains(script, "function safeHelper"),
         "the scriptlet's helper function is included");
  Expect(!provider.ScriptFor("https://other.org/").has_value(),
         "an unrelated page gets no script");
  Expect(!provider.ScriptFor("file:///c:/page.html").has_value() &&
             !provider.ScriptFor("about:blank").has_value(),
         "non-http pages never get a script");

  // The updater replaces a list: the new rule is picked up.
  dir.Write("ubo-test.txt",
            "example.com##+js(plain, hello)\nother.org##+js(plain, bye)\n");
  Expect(Contains(provider.ScriptFor("https://other.org/"), "plain(\"bye\")"),
         "a rewritten list is reloaded while the process runs");

  // A long interval must not re-scan on every call.
  ScratchDir slow_dir;
  slow_dir.Write("resources.json", kScriptletResources);
  slow_dir.Write("ubo-test.txt", "example.com##+js(plain, one)\n");
  vibrium::ScriptletProvider slow(slow_dir.str(), std::chrono::hours(1));
  Expect(Contains(slow.ScriptFor("https://example.com/"), "plain(\"one\")"),
         "provider loads on first use");
  slow_dir.Write("ubo-test.txt",
                 "example.com##+js(plain, one)\nother.org##+js(plain, two)\n");
  Expect(!slow.ScriptFor("https://other.org/").has_value(),
         "changes are not picked up before the re-check interval elapses");
}

void TestScriptletWrapper() {
  const std::string wrapped = vibrium::BuildScriptletScript("var a = 1;");
  Expect(wrapped.find("scriptletGlobals") != std::string::npos &&
             wrapped.find("scriptletGlobals") < wrapped.find("var a = 1;"),
         "scriptletGlobals is defined before the scriptlet runs");
}

// Many threads check requests while the active engine is swapped repeatedly.
// Run under ASan/TSan in CI-style checks: any use-after-free or double free of
// the Rust handle would be caught here.
void TestConcurrentChecksWhileSwappingEngines() {
  vibrium::AdblockEngineHolder holder;
  holder.Set(AdblockEngine::FromRules(kRules));

  std::atomic<bool> stop{false};
  std::atomic<long> blocked{0}, wrong{0};
  std::vector<std::thread> workers;
  for (int i = 0; i < 8; ++i) {
    workers.emplace_back([&] {
      while (!stop.load()) {
        auto engine = holder.Get();  // keeps the engine alive for this request
        if (!engine) continue;
        auto d = engine->Check("https://ads.vibrium-test.example/a.js",
                               "https://www.vibrium-test.example/", "script");
        if (d == AdblockEngine::Decision::kBlock) {
          ++blocked;
        } else {
          ++wrong;
        }
      }
    });
  }
  for (int i = 0; i < 300; ++i) {
    holder.Set(AdblockEngine::FromRules(kRules));  // old engine released
  }
  stop = true;
  for (auto& t : workers) t.join();

  Expect(wrong.load() == 0 && blocked.load() > 0,
         "concurrent checks stay correct while engines are swapped");
}

void TestEngineOutlivesHolder() {
  vibrium::AdblockEngineHolder holder;
  holder.Set(AdblockEngine::FromRules(kRules));
  auto held = holder.Get();
  holder.Set(nullptr);
  Expect(holder.Get() == nullptr, "holder can be cleared");
  Expect(held->Check("https://ads.vibrium-test.example/a.js",
                     "https://www.vibrium-test.example/", "script") ==
             AdblockEngine::Decision::kBlock,
         "an in-flight reference keeps the engine alive after replacement");
}

}  // namespace

int main() {
  TestDecisionsAndFailOpen();
  TestCosmetic();
  TestDirectoryLoading();
  TestPolicy();
  TestJsEscaping();
  TestCosmeticPipeline();
  TestContentScriptHelpers();
  TestScriptletDirectorySignature();
  TestScriptletProvider();
  TestScriptletWrapper();
  TestConcurrentChecksWhileSwappingEngines();
  TestEngineOutlivesHolder();

  if (failures != 0) {
    std::cerr << failures << " test(s) failed.\n";
    return 1;
  }
  std::cout << "All native wrapper tests passed.\n";
  return 0;
}
