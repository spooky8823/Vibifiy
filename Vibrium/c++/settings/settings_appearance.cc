#include "settings/settings_appearance.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <functional>
#include <vector>

#include "tabbar.h"
#include "theme.h"

namespace vibrium {

using namespace settings;

namespace {

// Hit ids.
constexpr int kHitPalette = 500;  // + palette index
constexpr int kHitResetAccent = 506;
constexpr int kHitCustomAccent = 507;

constexpr COLORREF kPaletteColors[6] = {
    RGB(0, 120, 215), RGB(0, 153, 102), RGB(16, 124, 16),
    RGB(232, 90, 35), RGB(190, 70, 160), RGB(100, 100, 110)};

// ---- colour wheel dialog (unchanged from the original settings page) -------

struct WheelPickerState {
  HWND hwnd = nullptr;
  HWND hexEdit = nullptr;
  HWND owner = nullptr;
  COLORREF original = RGB(0,120,215);
  COLORREF selected = RGB(0,120,215);
  double hue = 210.0, saturation = 1.0, value = 0.84;
  bool draggingWheel = false;
  bool draggingSquare = false;
  bool done = false;
  bool accepted = false;
  std::function<void(COLORREF)> preview;
};

COLORREF HsvToColor(double h, double s, double v) {
  h = std::fmod(h, 360.0);
  if (h < 0) h += 360.0;
  const double c = v * s;
  const double x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
  const double m = v - c;
  double r=0,g=0,b=0;
  if (h < 60) { r=c; g=x; }
  else if (h < 120) { r=x; g=c; }
  else if (h < 180) { g=c; b=x; }
  else if (h < 240) { g=x; b=c; }
  else if (h < 300) { r=x; b=c; }
  else { r=c; b=x; }
  return RGB(static_cast<BYTE>((r+m)*255.0+0.5),
            static_cast<BYTE>((g+m)*255.0+0.5),
            static_cast<BYTE>((b+m)*255.0+0.5));
}

void ColorToHsv(COLORREF color, double* h, double* s, double* v) {
  const double r=GetRValue(color)/255.0, g=GetGValue(color)/255.0,
               b=GetBValue(color)/255.0;
  const double mx=std::max({r,g,b}), mn=std::min({r,g,b}), d=mx-mn;
  *v=mx; *s=mx==0 ? 0 : d/mx; *h=0;
  if (d==0) return;
  if (mx==r) *h=60.0*std::fmod((g-b)/d,6.0);
  else if (mx==g) *h=60.0*((b-r)/d+2.0);
  else *h=60.0*((r-g)/d+4.0);
  if (*h<0) *h+=360.0;
}

std::wstring ColorHex(COLORREF c) {
  wchar_t text[8]{};
  swprintf_s(text,L"#%02X%02X%02X",GetRValue(c),GetGValue(c),GetBValue(c));
  return text;
}

bool ParseColorHex(const std::wstring& input, COLORREF* color) {
  std::wstring text=Trim(input);
  if (!text.empty() && text[0]==L'#') text.erase(0,1);
  if (text.size()!=6) return false;
  for (wchar_t ch:text) if (!((ch>=L'0'&&ch<=L'9')||(ch>=L'a'&&ch<=L'f')||(ch>=L'A'&&ch<=L'F'))) return false;
  unsigned int rgb=0;
  if (swscanf_s(text.c_str(),L"%x",&rgb)!=1) return false;
  *color=RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255);
  return true;
}

void WheelPickerUpdate(WheelPickerState* state, bool updateEdit=true) {
  state->selected=HsvToColor(state->hue,state->saturation,state->value);
  if (updateEdit && state->hexEdit) SetWindowTextW(state->hexEdit,ColorHex(state->selected).c_str());
  InvalidateRect(state->hwnd,nullptr,FALSE);
  if (state->preview) state->preview(state->selected);
}

void WheelPickerSetPoint(WheelPickerState* state,int x,int y,bool wheel) {
  if (wheel) {
    const double dx=x-160.0, dy=y-160.0;
    state->hue=std::atan2(dy,dx)*180.0/3.14159265358979323846+90.0;
    if (state->hue<0) state->hue+=360.0;
  } else {
    state->saturation=std::clamp((x-110.0)/100.0,0.0,1.0);
    state->value=1.0-std::clamp((y-110.0)/100.0,0.0,1.0);
  }
  WheelPickerUpdate(state);
}

void WheelPickerPaint(HDC hdc, WheelPickerState* state) {
  RECT client{}; GetClientRect(state->hwnd,&client);
  HBRUSH bg=CreateSolidBrush(RGB(24,24,28)); FillRect(hdc,&client,bg); DeleteObject(bg);
  const int w=320,h=320;
  std::vector<DWORD> pixels(static_cast<size_t>(w*h),0);
  BITMAPINFO bmi{}; bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth=w; bmi.bmiHeader.biHeight=-h;
  bmi.bmiHeader.biPlanes=1; bmi.bmiHeader.biBitCount=32;
  bmi.bmiHeader.biCompression=BI_RGB;
  for(int py=0;py<h;++py) for(int px=0;px<w;++px) {
    const double dx=px-160.0,dy=py-160.0,dist=std::sqrt(dx*dx+dy*dy);
    COLORREF c=RGB(24,24,28);
    if(dist>=126.0 && dist<=153.0) {
      double hue=std::atan2(dy,dx)*180.0/3.14159265358979323846+90.0;
      c=HsvToColor(hue,1.0,1.0);
    } else if(px>=110 && px<210 && py>=110 && py<210) {
      const double sat=(px-110)/100.0, val=1.0-(py-110)/100.0;
      c=HsvToColor(state->hue,sat,val);
    }
    pixels[static_cast<size_t>(py*w+px)]=0xff000000u|(GetRValue(c)<<16)|(GetGValue(c)<<8)|GetBValue(c);
  }
  SetDIBitsToDevice(hdc,18,18,w,h,0,0,0,h,pixels.data(),&bmi,DIB_RGB_COLORS);
  // Selection markers: white ring on the hue band and outlined marker in SV square.
  const double angle=(state->hue-90.0)*3.14159265358979323846/180.0;
  const int hx=18+160+static_cast<int>(139*cos(angle));
  const int hy=18+160+static_cast<int>(139*sin(angle));
  HPEN white=CreatePen(PS_SOLID,3,RGB(255,255,255)); HGDIOBJ old=SelectObject(hdc,white);
  HGDIOBJ oldBrush=SelectObject(hdc,GetStockObject(NULL_BRUSH));
  Ellipse(hdc,hx-8,hy-8,hx+8,hy+8);
  Ellipse(hdc,18+110+static_cast<int>(state->saturation*100)-7,
          18+110+static_cast<int>((1.0-state->value)*100)-7,
          18+110+static_cast<int>(state->saturation*100)+7,
          18+110+static_cast<int>((1.0-state->value)*100)+7);
  SelectObject(hdc,oldBrush); SelectObject(hdc,old); DeleteObject(white);
  HBRUSH swatch=CreateSolidBrush(state->selected);
  RECT preview{370,80,480,190}; FillRound(hdc,preview,18,state->selected,RGB(75,75,82));
  DeleteObject(swatch);
  static HFONT titleFont=MakeFont(20,FW_SEMIBOLD,L"Segoe UI");
  static HFONT labelFont=MakeFont(12,FW_NORMAL,L"Segoe UI");
  static HFONT smallFont=MakeFont(13,FW_NORMAL,L"Segoe UI");
  DrawLabel(hdc,L"Choose accent colour",RECT{360,28,510,62},titleFont,RGB(245,245,250),DT_SINGLELINE|DT_VCENTER|DT_LEFT);
  DrawLabel(hdc,L"HEX",RECT{365,215,405,245},smallFont,RGB(170,170,182),DT_SINGLELINE|DT_VCENTER|DT_LEFT);
  DrawLabel(hdc,L"Drag the wheel for hue, then the square for shade.",RECT{360,255,515,305},labelFont,RGB(170,170,182),DT_WORDBREAK|DT_LEFT);
}

LRESULT CALLBACK WheelPickerProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
  auto* state=reinterpret_cast<WheelPickerState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
  if(msg==WM_NCCREATE) {
    auto* cs=reinterpret_cast<CREATESTRUCTW*>(lp); state=static_cast<WheelPickerState*>(cs->lpCreateParams);
    state->hwnd=hwnd; SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
  }
  if(!state) return DefWindowProcW(hwnd,msg,wp,lp);
  switch(msg) {
    case WM_CREATE:
      state->hexEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",ColorHex(state->selected).c_str(),
        WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,405,215,100,28,hwnd,reinterpret_cast<HMENU>(10),GetModuleHandleW(nullptr),nullptr);
      CreateWindowW(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,365,345,90,36,hwnd,reinterpret_cast<HMENU>(2),GetModuleHandleW(nullptr),nullptr);
      CreateWindowW(L"BUTTON",L"Apply",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,465,345,90,36,hwnd,reinterpret_cast<HMENU>(1),GetModuleHandleW(nullptr),nullptr);
      return 0;
    case WM_PAINT: { PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps); WheelPickerPaint(dc,state); EndPaint(hwnd,&ps); return 0; }
    case WM_LBUTTONDOWN: {
      int x=MouseX(lp)-18,y=MouseY(lp)-18;
      const double d=sqrt((x-160.0)*(x-160.0)+(y-160.0)*(y-160.0));
      if(d>=118&&d<=160) {state->draggingWheel=true;SetCapture(hwnd);WheelPickerSetPoint(state,x,y,true);}
      else if(x>=100&&x<=220&&y>=100&&y<=220) {state->draggingSquare=true;SetCapture(hwnd);WheelPickerSetPoint(state,x,y,false);}
      return 0;
    }
    case WM_MOUSEMOVE:
      if(state->draggingWheel) WheelPickerSetPoint(state,MouseX(lp)-18,MouseY(lp)-18,true);
      else if(state->draggingSquare) WheelPickerSetPoint(state,MouseX(lp)-18,MouseY(lp)-18,false);
      return 0;
    case WM_LBUTTONUP:
      state->draggingWheel=state->draggingSquare=false;ReleaseCapture();return 0;
    case WM_COMMAND:
      if(LOWORD(wp)==10&&HIWORD(wp)==EN_CHANGE) {
        wchar_t text[32]{};GetWindowTextW(state->hexEdit,text,32);COLORREF c{};
        if(ParseColorHex(text,&c)){state->selected=c;ColorToHsv(c,&state->hue,&state->saturation,&state->value);InvalidateRect(hwnd,nullptr,FALSE);if(state->preview)state->preview(c);}
      } else if(LOWORD(wp)==1||LOWORD(wp)==2) {
        state->accepted=LOWORD(wp)==1;state->done=true;DestroyWindow(hwnd);
      }
      return 0;
    case WM_CLOSE: state->done=true;state->accepted=false;DestroyWindow(hwnd);return 0;
    case WM_DESTROY: return 0;
  }
  return DefWindowProcW(hwnd,msg,wp,lp);
}

bool ShowModernColorWheel(HWND owner,COLORREF initial,std::function<void(COLORREF)> preview,COLORREF* result) {
  static bool registered=false;
  HINSTANCE instance=GetModuleHandleW(nullptr);
  if(!registered) {
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=WheelPickerProc;wc.hInstance=instance;
    wc.lpszClassName=L"VibriumModernColorWheel";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    wc.hbrBackground=nullptr;
    if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    registered=true;
  }
  WheelPickerState state{};state.owner=owner;state.original=initial;state.selected=initial;state.preview=std::move(preview);
  ColorToHsv(initial,&state.hue,&state.saturation,&state.value);
  HWND hwnd=CreateWindowExW(WS_EX_DLGMODALFRAME,L"VibriumModernColorWheel",L"Vibrium · Accent colour",
    WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE, CW_USEDEFAULT,CW_USEDEFAULT,590,435,owner,nullptr,instance,&state);
  if(!hwnd)return false;
  RECT wr{};GetWindowRect(hwnd,&wr);int width=wr.right-wr.left,height=wr.bottom-wr.top;
  int x=(GetSystemMetrics(SM_CXSCREEN)-width)/2,y=(GetSystemMetrics(SM_CYSCREEN)-height)/2;
  SetWindowPos(hwnd,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER);
  EnableWindow(owner,FALSE);SetForegroundWindow(hwnd);
  MSG msg{};
  while(!state.done&&GetMessageW(&msg,nullptr,0,0)>0) {
    if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
  }
  EnableWindow(owner,TRUE);SetForegroundWindow(owner);
  if(state.accepted){*result=state.selected;return true;}
  return false;
}


}  // namespace

void AppearanceSection::Layout(const RECT& area) {
  width_ = area.right;
  height_ = area.bottom;
  hits_.Clear();

  const PageFrame frame = MakePageFrame(width_);
  const int cx = frame.cx;
  title_ = frame.title;
  subtitle_ = frame.subtitle;

  const int swatch = 48;
  for (int i = 0; i < 6; ++i) {
    const int left = cx + i * (swatch + 14);
    palette_[i] = RECT{left, 142, left + swatch, 142 + swatch};
    hits_.Add(palette_[i], kHitPalette + i);
  }
  resetAccentRect_ = RECT{cx, 220, cx + 190, 260};
  customAccentRect_ = RECT{cx + 204, 220, cx + 394, 260};
  hits_.Add(resetAccentRect_, kHitResetAccent);
  hits_.Add(customAccentRect_, kHitCustomAccent);
}

void AppearanceSection::OnClick(int id) {
  if (id >= kHitPalette && id < kHitPalette + 6) {
    Theme::SetAccent(kPaletteColors[id - kHitPalette]);
    TabBar::SetAccentColor(Theme::Accent());
    host_->NotifyAccentChanged(Theme::Accent());
    host_->RepaintEverything();
    return;
  }
  if (id == kHitResetAccent) {
    Theme::ResetAccent();
    TabBar::SetAccentColor(Theme::Accent());
    host_->NotifyAccentChanged(Theme::Accent());
    host_->RepaintEverything();
    return;
  }
  if (id == kHitCustomAccent) {
    const COLORREF previous = Theme::Accent();
    auto preview = [this](COLORREF color) {
      Theme::accent = color;
      Theme::manual = true;
      TabBar::SetAccentColor(color);
      host_->NotifyAccentChanged(color);
      host_->RepaintEverything();
    };
    COLORREF chosen = previous;
    if (ShowModernColorWheel(host_->window(), previous, preview, &chosen)) {
      Theme::SetAccent(chosen);
      preview(chosen);
    } else {
      Theme::SetAccent(previous);
      preview(previous);
    }
    return;
  }
}

void AppearanceSection::Paint(HDC hdc) {
  const SettingsFonts& f = fonts();
  DrawLabel(hdc, L"Appearance", title_, f.title, kText, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
  DrawLabel(hdc, L"Choose the accent used throughout Vibrium.", subtitle_, f.ui, kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
  const wchar_t* names[6] = {L"Blue", L"Teal", L"Green", L"Orange", L"Pink", L"Graphite"};
  for (int i=0;i<6;++i) {
    const bool selected = Theme::Accent() == kPaletteColors[i];
    FillRound(hdc, palette_[i], 12, kPaletteColors[i], selected ? kText : kBorderStrong, selected ? 3 : 1);
    RECT label{palette_[i].left-8,palette_[i].bottom+8,palette_[i].right+8,palette_[i].bottom+30};
    DrawLabel(hdc,names[i],label,f.ui,kText,DT_SINGLELINE|DT_CENTER|DT_VCENTER);
  }
  FillRound(hdc, resetAccentRect_, 12, kPanel3, kBorderStrong);
  DrawLabel(hdc, L"Reset to default", resetAccentRect_, f.ui, kText, DT_SINGLELINE|DT_CENTER|DT_VCENTER);
  FillRound(hdc, customAccentRect_, 12, Theme::Accent(), Theme::Accent());
  DrawLabel(hdc, L"Custom colour...", customAccentRect_, f.ui, RGB(255,255,255), DT_SINGLELINE|DT_CENTER|DT_VCENTER);
  std::wstring mode = Theme::IsManual() ? L"Using your saved Vibrium accent" : L"Using Vibrium's default accent. Windows colour settings are ignored.";
  DrawLabel(hdc, mode, RECT{resetAccentRect_.left, 282, resetAccentRect_.right+260, 310}, f.ui, kMuted, DT_SINGLELINE|DT_VCENTER|DT_LEFT);
}

}  // namespace vibrium
