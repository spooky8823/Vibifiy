
#pragma once

#include <windows.h>

#include <functional>
#include <string>

class SearchEngine {
public:
    using NavigateCallback =
        std::function<void(const std::wstring&)>;

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

    static std::wstring EncodeURL(
        const std::wstring& value);

    static bool LooksLikeURL(
        const std::wstring& value);

    static std::wstring NormalizeURL(
        const std::wstring& value);

private:
    HWND parent_ = nullptr;
    HWND hwnd_ = nullptr;

    HWND addressEdit_ = nullptr;
    HWND goButton_ = nullptr;

    HINSTANCE instance_ = nullptr;

    WNDPROC originalEditProc_ = nullptr;

    NavigateCallback onNavigate_;

    std::wstring searchProvider_ =
        L"https://www.google.com/search?q=";

    int width_ = 0;
    int height_ = 0;
};
