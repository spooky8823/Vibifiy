#pragma once

#include <windows.h>

namespace vibrium {

class ConsoleWindow {
 public:
  ConsoleWindow() = default;
  ~ConsoleWindow();

  bool Create(HWND owner, HINSTANCE instance);
  void Show();
  void Refresh();

 private:
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message,
                                     WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
  void CreateControls();
  void Layout();
  void RenderLogs();

  HWND hwnd_ = nullptr;
  HWND owner_ = nullptr;
  HWND summary_ = nullptr;
  HWND search_ = nullptr;
  HWND logs_ = nullptr;
  HINSTANCE instance_ = nullptr;
};

}  // namespace vibrium
