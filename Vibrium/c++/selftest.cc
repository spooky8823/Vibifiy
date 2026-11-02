#include "selftest.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

#include "adblock/adblock_service.h"

namespace vibrium {

namespace {

constexpr char kTitlePrefix[] = "VIBRIUM_SELFTEST:";

bool Contains(const std::vector<std::string>& urls, const std::string& part) {
  return std::any_of(urls.begin(), urls.end(), [&](const std::string& u) {
    return u.find(part) != std::string::npos;
  });
}

}  // namespace

SelfTest& SelfTest::Get() {
  static SelfTest instance;
  return instance;
}

void SelfTest::Configure(CefRefPtr<CefCommandLine> command_line) {
  if (!command_line || !command_line->HasSwitch("selftest")) {
    return;
  }
  enabled_ = true;
  start_url_ = command_line->GetSwitchValue("url").ToWString();
  report_path_ = command_line->GetSwitchValue("selftest-report").ToString();
}

bool SelfTest::OnTitleChanged(const std::wstring& title) {
  if (!enabled_ || finished_) {
    return false;
  }
  const CefString wrapped(title);
  const std::string text = wrapped.ToString();
  if (text.rfind(kTitlePrefix, 0) != 0) {
    return false;
  }
  Finish(text, /*timed_out=*/false);
  return true;
}

void SelfTest::OnTimeout() {
  if (enabled_ && !finished_) {
    Finish("timeout", /*timed_out=*/true);
  }
}

void SelfTest::Finish(const std::string& page_result, bool timed_out) {
  finished_ = true;
  const AdblockStats::Snapshot s = AdblockService::Get().Stats();

  struct Check {
    const char* name;
    bool ok;
  };
  const AdblockService& service = AdblockService::Get();
  const std::vector<Check> checks = {
      {"ad blocking is enabled (filter lists were found and loaded)",
       service.enabled()},
      {"page reported its result before the timeout", !timed_out},
      {"blocked: script from ads.vibrium-selftest.test",
       Contains(s.blocked_urls, "ads.vibrium-selftest.test/blocked.js")},
      {"blocked: redirect target ads.vibrium-selftest.test/redirected.js",
       Contains(s.blocked_urls, "ads.vibrium-selftest.test/redirected.js")},
      {"allowed: image from allowed.vibrium-selftest.test",
       Contains(s.allowed_urls, "allowed.vibrium-selftest.test/pic.png")},
      {"main-frame navigation was never blocked",
       !Contains(s.blocked_urls, "selftest.html")},
      {"cosmetic filter hid .vibrium-ad",
       page_result.find("cosmetic=hidden") != std::string::npos},
      {"generic class selector hid .vibrium-generic-ad (renderer -> browser)",
       page_result.find("generic=hidden") != std::string::npos},
      {"procedural :has-text() filter hid the matching element",
       page_result.find("procedural=hidden") != std::string::npos},
      {"procedural filter left non-matching content visible",
       page_result.find("kept=visible") != std::string::npos},
  };

  bool all_ok = true;
  std::ostringstream report;
  for (const Check& c : checks) {
    report << (c.ok ? "PASS " : "FAIL ") << c.name << "\n";
    all_ok = all_ok && c.ok;
  }
  report << "filter dir: " << service.filter_dir() << "\n"
         << "page result: " << page_result << "\n"
         << "blocked=" << s.blocked << " allowed=" << s.allowed
         << " errors=" << s.errors << "\n";
  for (const auto& u : s.blocked_urls) report << "  blocked " << u << "\n";
  for (const auto& u : s.allowed_urls) report << "  allowed " << u << "\n";
  report << (all_ok ? "SELFTEST PASSED\n" : "SELFTEST FAILED\n");

  if (!report_path_.empty()) {
    std::ofstream(report_path_, std::ios::binary) << report.str();
  }
  exit_code_ = all_ok ? 0 : 1;
}

}  // namespace vibrium
