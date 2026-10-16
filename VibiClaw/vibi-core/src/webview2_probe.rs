use std::num::NonZeroIsize;
use wry::raw_window_handle::{
    HandleError, HasWindowHandle, RawWindowHandle, Win32WindowHandle, WindowHandle,
};
use wry::WebViewBuilder;

struct Hwnd(isize);

impl HasWindowHandle for Hwnd {
    fn window_handle(&self) -> Result<WindowHandle<'_>, HandleError> {
        let hwnd = NonZeroIsize::new(self.0).ok_or(HandleError::Unavailable)?;
        let raw = RawWindowHandle::Win32(Win32WindowHandle::new(hwnd));
        Ok(unsafe { WindowHandle::borrow_raw(raw) })
    }
}

pub fn probe(hwnd: isize) -> Result<(), String> {
    let parent = Hwnd(hwnd);
    let view = WebViewBuilder::new()
        .with_url("https://example.com")
        .build_as_child(&parent)
        .map_err(|e| e.to_string())?;
    std::mem::forget(view);
    Ok(())
}