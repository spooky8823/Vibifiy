// End-to-end self test, enabled with `vibrium.exe --selftest`.
//
//   --selftest                   load --url, check ad-blocking, exit 0 or 1
//   --url=<url>                  page that reports "VIBRIUM_SELFTEST:..." as
//                                its title (tests/fixtures/selftest.html)
//   --selftest-report=<file>     where to write the human-readable report
//
// Not used in normal operation: enabled() is false and every hook is a no-op.

#ifndef VIBRIUM_SELFTEST_H_
#define VIBRIUM_SELFTEST_H_

#include <string>

#include "include/cef_command_line.h"

namespace vibrium {

class SelfTest {
 public:
  static constexpr unsigned kTimeoutMs = 60 * 1000;

  static SelfTest& Get();

  void Configure(CefRefPtr<CefCommandLine> command_line);
  bool enabled() const { return enabled_; }
  const std::wstring& start_url() const { return start_url_; }

  // Call for every tab title change. Returns true when the page has reported
  // its result and the test is finished; the caller then closes the window.
  bool OnTitleChanged(const std::wstring& title);

  // The page never reported: fail the test (caller then closes the window).
  void OnTimeout();

  // Value to return from wWinMain (0 unless a selftest failed).
  int exit_code() const { return exit_code_; }

 private:
  void Finish(const std::string& page_result, bool timed_out);

  bool enabled_ = false;
  bool finished_ = false;
  int exit_code_ = 0;
  std::wstring start_url_;
  std::string report_path_;
};

}  // namespace vibrium

#endif  // VIBRIUM_SELFTEST_H_
