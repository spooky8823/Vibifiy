
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

class TabBar {
public:
    using TabCallback = std::function<void(int)>;

    TabBar() = default;
    ~TabBar() = default;

    TabBar(const TabBar&) = delete;
    TabBar& operator=(const TabBar&) = delete;

    bool Create(
        HWND parent,
        HINSTANCE instance,
        int x,
        int y,
        int width,
        int height
    );

    void Resize(int x, int y, int width, int height);

    int AddTab(const std::wstring& title);
    void RemoveTab(int index);
    void SetActiveTab(int index);

    int GetActiveTab() const;
    int GetTabCount() const;

    void SetOnTabSelected(TabCallback callback);
    void SetOnTabClosed(TabCallback callback);
    void SetOnNewTab(std::function<void()> callback);

    HWND GetHandle() const;

private:
    struct Tab {
        std::wstring title;
    };

    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    HINSTANCE instance_ = nullptr;

    std::vector<Tab> tabs_;
    int activeTab_ = -1;

    TabCallback onTabSelected_;
    TabCallback onTabClosed_;
    std::function<void()> onNewTab_;

    static constexpr int TAB_HEIGHT = 42;
    static constexpr int TAB_MIN_WIDTH = 120;
    static constexpr int TAB_MAX_WIDTH = 220;
    static constexpr int NEW_TAB_WIDTH = 42;

    static constexpr COLORREF COLOR_BACKGROUND = RGB(15, 15, 20);
    static constexpr COLORREF COLOR_TAB = RGB(25, 25, 33);
    static constexpr COLORREF COLOR_ACTIVE = RGB(42, 39, 55);
    static constexpr COLORREF COLOR_HOVER = RGB(55, 50, 70);
    static constexpr COLORREF COLOR_ACCENT = RGB(123, 92, 240);
    static constexpr COLORREF COLOR_TEXT = RGB(235, 232, 245);
    static constexpr COLORREF COLOR_MUTED = RGB(145, 140, 160);

    static LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    );

    LRESULT HandleMessage(
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    );

    void Paint(HDC hdc);
    void HandleClick(int x, int y);

    RECT GetTabRect(int index) const;
    int GetTabWidth() const;
};
