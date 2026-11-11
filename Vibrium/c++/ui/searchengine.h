#pragma once

#include <windows.h>

#include <functional>
#include <string>

class SearchEngine {
public:
    using NavigateCallback =
        std::function<void(const std::wstring&)>;
    using SettingsCallback = std::function<void()>;

    bool Create(
        HWND parent,
        HINSTANCE instance,
        int x,
        int y,
        int width,
        int height);

    void Resize(
        int x,
        int y,
        int width,
        int height);

    void SetText(const std::wstring& text);
    std::wstring GetText() const;

    void SetOnNavigate(NavigateCallback callback);

    // Called when the settings (gear) button next to the address bar is
    // clicked.
    void SetOnSettings(SettingsCallback callback);
    void SetSearchProvider(const std::wstring& provider);

    void Navigate();

    HWND GetHandle() const;

private:
    static LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    static LRESULT CALLBACK EditProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    static std::wstring EncodeURL(const std::wstring& value);
    static bool LooksLikeURL(const std::wstring& value);
    static std::wstring NormalizeURL(const std::wstring& value);

    void Paint(HDC hdc);
    void LayoutControls();
    RECT GearRect(const RECT& client) const;
    bool PointInGear(int x, int y) const;

    HWND parent_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND addressEdit_ = nullptr;

    HINSTANCE instance_ = nullptr;
    WNDPROC originalEditProc_ = nullptr;

    HBRUSH editBrush_ = nullptr;
    HFONT font_ = nullptr;

    NavigateCallback onNavigate_;
    SettingsCallback onSettings_;

    std::wstring searchProvider_ =
        L"https://www.google.com/search?q=";

    int width_ = 0;
    int height_ = 0;

    bool editFocused_ = false;
    bool gearHover_ = false;
    bool trackingMouse_ = false;
};