#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "tabbar_geometry.h"

#include <functional>
#include <string>
#include <vector>

class TabBar {
public:
    using TabCallback = std::function<void(int)>;

    TabBar() = default;
    ~TabBar();

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
    void SetTabIconText(int index, const std::wstring& text);
    void SetTabIconBitmap(int index, HBITMAP bitmap);

    HWND GetHandle() const;
    static void SetAccentColor(COLORREF color);

private:
    struct Tab {
        std::wstring title;
        std::wstring iconText = L"•";
        HBITMAP iconBitmap = nullptr;
        ULONGLONG animationStart = 0;
        bool opening = false;
        bool closing = false;
    };

    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    HINSTANCE instance_ = nullptr;

    std::vector<Tab> tabs_;
    int activeTab_ = -1;
    int closingTab_ = -1;

    float GetTabAnimationScale(int index) const;

    TabCallback onTabSelected_;
    TabCallback onTabClosed_;
    std::function<void()> onNewTab_;

    static constexpr int TAB_MIN_WIDTH = 130;
    static constexpr int TAB_MAX_WIDTH = 250;
    static constexpr int NEW_TAB_WIDTH = 42;

    static constexpr COLORREF COLOR_WINDOW_BG = RGB(30, 30, 32);
    static constexpr COLORREF COLOR_TAB = RGB(35, 35, 39);
    static constexpr COLORREF COLOR_ACTIVE = RGB(54, 54, 60);
    static constexpr COLORREF COLOR_HOVER = RGB(48, 48, 54);
    static COLORREF accentColor_;
    static constexpr COLORREF COLOR_TEXT = RGB(243, 238, 252);
    static constexpr COLORREF COLOR_MUTED = RGB(158, 158, 166);

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

    RECT GetTabRect(int index) const;      // bounding box of the slanted tab
    RECT GetNewTabRect() const;            // floating "+" button
    bool TabContains(int index, int x, int y) const;  // slanted hit-test
    int GetTabWidth() const;
    int GetBarHeight() const;
    vibrium::tabgeo::Layout GetLayout() const;   // tab spacing + "+" position
};