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
        int height);

    void Resize(int x, int y, int width, int height);

    int AddTab(const std::wstring& title);
    void RemoveTab(int index);
    void SetActiveTab(int index);

    int GetActiveTab() const;
    int GetTabCount() const;

    void SetOnTabSelected(TabCallback callback);
    void SetOnTabClosed(TabCallback callback);
    void SetOnNewTab(std::function<void()> callback);
    void SetTabTitle(int index, const std::wstring& title);

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
    static constexpr int NEW_TAB_WIDTH = 44;

    static constexpr COLORREF COLOR_WINDOW_BG = RGB(19, 15, 29);
    static constexpr COLORREF COLOR_TAB = RGB(35, 29, 49);
    static constexpr COLORREF COLOR_ACTIVE = RGB(62, 47, 91);
    static constexpr COLORREF COLOR_HOVER = RGB(75, 58, 108);
    static constexpr COLORREF COLOR_ACCENT = RGB(151, 111, 255);
    static constexpr COLORREF COLOR_TEXT = RGB(243, 238, 252);
    static constexpr COLORREF COLOR_MUTED = RGB(164, 153, 183);

    static LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    LRESULT HandleMessage(
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    void Paint(HDC hdc);
    void HandleClick(int x, int y);

    RECT GetTabRect(int index) const;
    int GetTabWidth() const;
};