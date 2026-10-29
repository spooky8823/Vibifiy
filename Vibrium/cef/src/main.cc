#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_sandbox_win.h"

#include <windows.h>

class VibriumClient final : public CefClient,
                            public CefLifeSpanHandler {
public:
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
        return this;
    }

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
        browser_ = browser;
    }

    bool DoClose(CefRefPtr<CefBrowser> browser) override {
        return false;
    }

    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
        browser_ = nullptr;
        PostQuitMessage(0);
    }

private:
    CefRefPtr<CefBrowser> browser_;

    IMPLEMENT_REFCOUNTING(VibriumClient);
};

int APIENTRY wWinMain(HINSTANCE instance,
                      HINSTANCE,
                      wchar_t*,
                      int show_command) {
    CefMainArgs main_args(instance);

    const int subprocess_result = CefExecuteProcess(
        main_args, nullptr, nullptr);

    if (subprocess_result >= 0) {
        return subprocess_result;
    }

    CefSettings settings;
    settings.no_sandbox = true;

    if (!CefInitialize(main_args, settings, nullptr, nullptr)) {
        return 1;
    }

    CefWindowInfo window_info;
    window_info.SetAsPopup(nullptr, "Vibrium");

    CefBrowserSettings browser_settings;
    CefRefPtr<VibriumClient> client = new VibriumClient();

    CefBrowserHost::CreateBrowser(
        window_info,
        client,
        "https://example.com",
        browser_settings,
        nullptr,
        nullptr);

    MSG message;
    while (GetMessage(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    CefShutdown();
    return 0;
}
