#include "Controls.h"
#include "Renderer.h"
#include "Utils.h"
#include <algorithm>
#include <commctrl.h>
#include <uxtheme.h>

// ---------------------------------------------------------------------------
// Factory helpers
// ---------------------------------------------------------------------------

static HWND CreateTooltipWnd(HWND parent) {
    HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (tip) {
        SendMessageW(tip, TTM_SETMAXTIPWIDTH, 0, Scale(320));
        SendMessageW(tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 8000);
    }
    return tip;
}

static void AddTip(HWND tip, HWND ctrl, const wchar_t* text) {
    if (!tip || !ctrl || !text) return;
    TOOLINFOW ti{};
    ti.cbSize   = sizeof(TOOLINFOW);
    ti.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd     = GetParent(ctrl);
    ti.uId      = reinterpret_cast<UINT_PTR>(ctrl);
    ti.lpszText = const_cast<wchar_t*>(text);
    SendMessageW(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
}

static HWND MakeButton(HWND p, const wchar_t* text, int id, DWORD extra = 0) {
    return CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | extra, 0,0,0,0, p,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
}
static HWND MakeEdit(HWND p, int id, DWORD extra = 0) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | extra, 0,0,0,0, p,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
}
static HWND MakeLabel(HWND p, const wchar_t* text) {
    return CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0,0,0,0, p,
        nullptr, GetModuleHandleW(nullptr), nullptr);
}
static HWND MakeSpin(HWND p, int id) {
    HWND h = CreateWindowExW(0, UPDOWN_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | UDS_NOTHOUSANDS,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    if (h) SendMessageW(h, UDM_SETRANGE32, -9999, 9999);
    return h;
}

namespace {

constexpr wchar_t kFooterProgressClassName[] = L"SeatingChartFooterProgress";

ThemeColors DefaultFooterProgressAppTheme() {
    ThemeColors theme{};
    theme.window            = RGB(248, 250, 252);
    theme.panel             = RGB(255, 255, 255);
    theme.text              = RGB(30, 30, 34);
    theme.mutedText         = RGB(95, 100, 110);
    theme.seatEmpty         = RGB(245, 247, 250);
    theme.seatOccupied      = RGB(214, 232, 252);
    theme.seatSelected      = RGB(189, 214, 255);
    theme.border            = RGB(110, 120, 135);
    theme.accent            = RGB(45, 105, 200);
    theme.furniture         = RGB(230, 235, 245);
    theme.furnitureSelected = RGB(74, 116, 178);
    theme.paper             = RGB(244, 244, 246);
    return theme;
}

struct FooterProgressState {
    int minValue = 0;
    int maxValue = 100;
    int pos      = 0;
    FooterProgressTheme theme = FooterProgressTheme::Crayon;
    ThemeColors appTheme = DefaultFooterProgressAppTheme();
};

FooterProgressState* FooterProgressFromHwnd(HWND hwnd) {
    return reinterpret_cast<FooterProgressState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

COLORREF BlendColor(COLORREF from, COLORREF to, int toWeight, int totalWeight) {
    totalWeight = std::max(1, totalWeight);
    toWeight    = std::clamp(toWeight, 0, totalWeight);
    const int fromWeight = totalWeight - toWeight;
    return RGB(
        (GetRValue(from) * fromWeight + GetRValue(to) * toWeight) / totalWeight,
        (GetGValue(from) * fromWeight + GetGValue(to) * toWeight) / totalWeight,
        (GetBValue(from) * fromWeight + GetBValue(to) * toWeight) / totalWeight);
}

COLORREF LerpColor(COLORREF a, COLORREF b, int numer, int denom) {
    denom = std::max(1, denom);
    numer = std::clamp(numer, 0, denom);
    return RGB(
        (GetRValue(a) * (denom - numer) + GetRValue(b) * numer) / denom,
        (GetGValue(a) * (denom - numer) + GetGValue(b) * numer) / denom,
        (GetBValue(a) * (denom - numer) + GetBValue(b) * numer) / denom);
}

bool ThemeIsDark(const ThemeColors& theme) {
    return (static_cast<int>(GetRValue(theme.window)) +
            static_cast<int>(GetGValue(theme.window)) +
            static_cast<int>(GetBValue(theme.window))) / 3 < 128;
}

int RectWidth(const RECT& rc) {
    return static_cast<int>(rc.right - rc.left);
}

int RectHeight(const RECT& rc) {
    return static_cast<int>(rc.bottom - rc.top);
}

void FillSolidRect(HDC hdc, const RECT& rc, COLORREF color) {
    if (rc.right <= rc.left || rc.bottom <= rc.top) return;
    HBRUSH brush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
    const COLORREF old = SetDCBrushColor(hdc, color);
    FillRect(hdc, &rc, brush);
    SetDCBrushColor(hdc, old);
}

void DrawClippedVerticalGradient(HDC hdc, const RECT& rc, HRGN clip,
                                 COLORREF top, COLORREF bottom) {
    if (!clip || rc.right <= rc.left || rc.bottom <= rc.top) return;
    const int saved = SaveDC(hdc);
    SelectClipRgn(hdc, clip);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    const int height = std::max(1, RectHeight(rc) - 1);
    for (int y = rc.top; y < rc.bottom; ++y) {
        SetDCPenColor(hdc, LerpColor(top, bottom, y - rc.top, height));
        MoveToEx(hdc, rc.left, y, nullptr);
        LineTo(hdc, rc.right, y);
    }
    SelectObject(hdc, oldPen);
    RestoreDC(hdc, saved);
}

void DrawClippedHorizontalGradient(HDC hdc, const RECT& rc, HRGN clip,
                                   COLORREF left, COLORREF right) {
    if (!clip || rc.right <= rc.left || rc.bottom <= rc.top) return;
    const int saved = SaveDC(hdc);
    SelectClipRgn(hdc, clip);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    const int width = std::max(1, RectWidth(rc) - 1);
    for (int x = rc.left; x < rc.right; ++x) {
        SetDCPenColor(hdc, LerpColor(left, right, x - rc.left, width));
        MoveToEx(hdc, x, rc.top, nullptr);
        LineTo(hdc, x, rc.bottom);
    }
    SelectObject(hdc, oldPen);
    RestoreDC(hdc, saved);
}

void DrawRoundedBorder(HDC hdc, const RECT& rc, int radius, COLORREF color) {
    if (rc.right <= rc.left || rc.bottom <= rc.top) return;
    HGDIOBJ oldPen   = SelectObject(hdc, GetStockObject(DC_PEN));
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    const COLORREF oldColor = SetDCPenColor(hdc, color);
    const int diameter = std::max(2, radius * 2);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, diameter, diameter);
    SetDCPenColor(hdc, oldColor);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
}

void PaintGlossBand(HDC hdc, const RECT& rc, HRGN clip, COLORREF top, COLORREF bottom) {
    RECT gloss = rc;
    gloss.left   += 1;
    gloss.top    += 1;
    gloss.right  -= 1;
    gloss.bottom  = std::min(static_cast<int>(rc.bottom - 1),
                             static_cast<int>(rc.top) + std::max(2, RectHeight(rc) / 2));
    if (gloss.right <= gloss.left || gloss.bottom <= gloss.top) return;
    DrawClippedVerticalGradient(hdc, gloss, clip, top, bottom);
}

void PaintRoundedSurface(HDC hdc, const RECT& rc, int radius,
                         COLORREF fillTop, COLORREF fillBottom,
                         COLORREF borderOuter, COLORREF borderInner,
                         COLORREF glossTop, COLORREF glossBottom) {
    if (rc.right <= rc.left || rc.bottom <= rc.top) return;
    const int diameter = std::max(2, radius * 2);
    HRGN clip = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, diameter, diameter);
    if (!clip) return;
    DrawClippedVerticalGradient(hdc, rc, clip, fillTop, fillBottom);
    PaintGlossBand(hdc, rc, clip, glossTop, glossBottom);
    DeleteObject(clip);

    DrawRoundedBorder(hdc, rc, radius, borderOuter);

    RECT inner = rc;
    InflateRect(&inner, -1, -1);
    if (inner.right > inner.left && inner.bottom > inner.top)
        DrawRoundedBorder(hdc, inner, std::max(1, radius - 1), borderInner);
}

RECT FooterFillRect(const RECT& track, const FooterProgressState& state) {
    RECT fill = track;
    InflateRect(&fill, -2, -2);
    if (fill.right <= fill.left || fill.bottom <= fill.top) return fill;

    const int minValue = std::min(state.minValue, state.maxValue);
    const int maxValue = std::max(state.minValue, state.maxValue);
    const int clamped  = std::clamp(state.pos, minValue, maxValue);
    const int span     = std::max(1, maxValue - minValue);
    const int width    = RectWidth(fill);
    int fillWidth      = MulDiv(width, clamped - minValue, span);

    if (fillWidth <= 0) {
        fill.right = fill.left;
        return fill;
    }

    if (clamped > minValue)
        fillWidth = std::max(fillWidth, std::min(width, RectHeight(fill)));

    fill.right = fill.left + std::clamp(fillWidth, 0, width);
    return fill;
}

RECT ClampRectToBounds(RECT rc, const RECT& bounds) {
    rc.left   = std::max(static_cast<int>(rc.left),   static_cast<int>(bounds.left));
    rc.top    = std::max(static_cast<int>(rc.top),    static_cast<int>(bounds.top));
    rc.right  = std::min(static_cast<int>(rc.right),  static_cast<int>(bounds.right));
    rc.bottom = std::min(static_cast<int>(rc.bottom), static_cast<int>(bounds.bottom));
    if (rc.right < rc.left)   rc.right  = rc.left;
    if (rc.bottom < rc.top)   rc.bottom = rc.top;
    return rc;
}

RECT InsetRectCopy(const RECT& rc, int dx, int dy) {
    RECT out = rc;
    InflateRect(&out, -dx, -dy);
    return out;
}

RECT ExpandRectCopy(const RECT& rc, int dx, int dy) {
    RECT out = rc;
    InflateRect(&out, dx, dy);
    return out;
}

void DrawChunkSeparators(HDC hdc, const RECT& rc, int radius, COLORREF color, int step) {
    if (step <= 0 || rc.right - rc.left <= step) return;
    const int diameter = std::max(2, radius * 2);
    HRGN clip = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, diameter, diameter);
    if (!clip) return;

    const int saved = SaveDC(hdc);
    SelectClipRgn(hdc, clip);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    for (int x = rc.left + step; x < rc.right - 1; x += step) {
        SetDCPenColor(hdc, color);
        MoveToEx(hdc, x, rc.top + 1, nullptr);
        LineTo(hdc, x, rc.bottom - 1);
    }
    SelectObject(hdc, oldPen);
    RestoreDC(hdc, saved);
    DeleteObject(clip);
}

void PaintClippedGlassBand(HDC hdc, const RECT& rc, int radius,
                           COLORREF top, COLORREF bottom) {
    if (rc.right <= rc.left || rc.bottom <= rc.top) return;
    const int diameter = std::max(2, radius * 2);
    HRGN clip = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, diameter, diameter);
    if (!clip) return;
    DrawClippedVerticalGradient(hdc, rc, clip, top, bottom);
    DeleteObject(clip);
}

void PaintVistaGlowLayer(HDC hdc, const RECT& rc, int radius,
                         COLORREF top, COLORREF bottom,
                         COLORREF borderOuter, COLORREF borderInner) {
    if (rc.right <= rc.left || rc.bottom <= rc.top) return;
    PaintRoundedSurface(hdc, rc, radius,
                        top, bottom,
                        borderOuter, borderInner,
                        BlendColor(top, RGB(255, 255, 255), 4, 10),
                        top);
}

void PaintCrayonBands(HDC hdc, const RECT& rc, int radius, const COLORREF* colors, int colorCount) {
    if (!colors || colorCount < 2 || rc.right <= rc.left || rc.bottom <= rc.top) return;
    const int diameter = std::max(2, radius * 2);
    HRGN clip = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, diameter, diameter);
    if (!clip) return;

    const int segments = colorCount - 1;
    const int width = RectWidth(rc);
    for (int i = 0; i < segments; ++i) {
        RECT band = rc;
        band.left  = rc.left + MulDiv(width, i, segments);
        band.right = rc.left + MulDiv(width, i + 1, segments);
        if (band.right <= band.left) continue;
        DrawClippedHorizontalGradient(hdc, band, clip, colors[i], colors[i + 1]);
    }
    DeleteObject(clip);
}

void PaintCrayonTexture(HDC hdc, const RECT& rc, int radius,
                        COLORREF lightStroke, COLORREF darkStroke, COLORREF dust) {
    if (rc.right <= rc.left || rc.bottom <= rc.top) return;
    const int diameter = std::max(2, radius * 2);
    HRGN clip = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, diameter, diameter);
    if (!clip) return;

    const int saved = SaveDC(hdc);
    SelectClipRgn(hdc, clip);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));

    const int width = RectWidth(rc);
    const int segBase = std::max(6, std::min(18, width / 3));

    for (int y = rc.top + 2; y < rc.bottom - 1; y += 3) {
        const bool useLight = (((y - rc.top) / 3) % 2) == 0;
        SetDCPenColor(hdc, useLight ? lightStroke : darkStroke);
        const int start = rc.left + 1 + ((y * 7) % 9);
        for (int x = start; x < rc.right - 2; x += segBase + 4) {
            const int len = std::max(4, segBase + ((x + y) % 7) - 3);
            const int y2 = std::clamp(y + (((x / 5) + y) % 3) - 1,
                                      static_cast<int>(rc.top + 1),
                                      static_cast<int>(rc.bottom - 2));
            const int x2 = std::min(static_cast<int>(rc.right - 2), x + len);
            MoveToEx(hdc, x, y2, nullptr);
            LineTo(hdc, x2, y2 + (((x + y) % 2) ? 0 : 1));
        }
    }

    SetDCPenColor(hdc, dust);
    for (int x = rc.left + 2; x < rc.right - 1; x += 9) {
        for (int y = rc.top + 1 + ((x * 3) % 5); y < rc.bottom - 1; y += 7) {
            MoveToEx(hdc, x, y, nullptr);
            LineTo(hdc, std::min(static_cast<int>(rc.right - 1), x + 2), y);
        }
    }

    SelectObject(hdc, oldPen);
    RestoreDC(hdc, saved);
    DeleteObject(clip);
}

void PaintNativeFooterProgress(HDC hdc, const RECT& track, int radius,
                               const FooterProgressState& state) {
    const bool dark = ThemeIsDark(state.appTheme);
    const COLORREF trackTop    = dark ? BlendColor(state.appTheme.panel, RGB(72, 76, 84), 3, 10)
                                      : BlendColor(state.appTheme.panel, RGB(255, 255, 255), 5, 10);
    const COLORREF trackBottom = dark ? BlendColor(state.appTheme.panel, RGB(18, 18, 22), 3, 10)
                                      : BlendColor(state.appTheme.panel, RGB(214, 220, 228), 4, 10);
    const COLORREF trackOuter  = dark ? BlendColor(state.appTheme.border, state.appTheme.panel, 2, 10)
                                      : BlendColor(state.appTheme.border, state.appTheme.panel, 3, 10);
    const COLORREF trackInner  = dark ? BlendColor(trackTop, RGB(255, 255, 255), 1, 10)
                                      : BlendColor(trackTop, RGB(255, 255, 255), 3, 10);
    PaintRoundedSurface(hdc, track, radius,
                        trackTop, trackBottom,
                        trackOuter, trackInner,
                        BlendColor(trackTop, RGB(255, 255, 255), dark ? 1 : 4, 10),
                        trackTop);

    const RECT fill = FooterFillRect(track, state);
    if (fill.right <= fill.left) return;

    const COLORREF fillTop   = BlendColor(state.appTheme.accent, RGB(255, 255, 255), dark ? 2 : 4, 10);
    const COLORREF fillBot   = BlendColor(state.appTheme.accent, RGB(0, 0, 0), dark ? 3 : 1, 10);
    const COLORREF fillOuter = BlendColor(state.appTheme.accent, state.appTheme.border, 4, 10);
    const COLORREF fillInner = BlendColor(fillTop, RGB(255, 255, 255), 4, 10);
    PaintRoundedSurface(hdc, fill, std::min(radius, RectHeight(fill) / 2),
                        fillTop, fillBot,
                        fillOuter, fillInner,
                        BlendColor(fillTop, RGB(255, 255, 255), 5, 10),
                        fillTop);
}

void PaintVistaFooterProgress(HDC hdc, const RECT& track, int radius,
                              const FooterProgressState& state) {
    const bool dark = ThemeIsDark(state.appTheme);
    auto adaptTrack = [&](COLORREF color) {
        return dark ? BlendColor(color, state.appTheme.panel, 1, 14) : color;
    };
    auto adaptFill = [&](COLORREF color) {
        return dark ? BlendColor(color, state.appTheme.panel, 1, 16) : color;
    };

    const COLORREF trackTop      = adaptTrack(RGB(253, 254, 255));
    const COLORREF trackBottom   = adaptTrack(RGB(170, 194, 216));
    const COLORREF trackOuter    = adaptTrack(RGB(97, 123, 148));
    const COLORREF trackInner    = adaptTrack(RGB(255, 255, 255));
    const COLORREF trackGlossTop = adaptTrack(RGB(255, 255, 255));
    const COLORREF trackGlossBot = adaptTrack(RGB(223, 236, 248));
    PaintRoundedSurface(hdc, track, radius,
                        trackTop, trackBottom,
                        trackOuter, trackInner,
                        trackGlossTop, trackGlossBot);

    RECT trackBand = InsetRectCopy(track, 2, 2);
    trackBand.bottom = std::min(trackBand.bottom,
                                trackBand.top + std::max(3, (RectHeight(trackBand) * 2) / 3));
    PaintClippedGlassBand(hdc, trackBand, std::max(2, radius - 1),
                          adaptTrack(RGB(255, 255, 255)),
                          adaptTrack(RGB(206, 225, 241)));

    RECT trackCore = InsetRectCopy(track, 3, 3);
    if (trackCore.right > trackCore.left && trackCore.bottom > trackCore.top) {
        PaintClippedGlassBand(hdc, trackCore, std::max(2, radius - 2),
                              adaptTrack(RGB(242, 249, 255)),
                              adaptTrack(RGB(180, 207, 230)));
    }

    const RECT fill = FooterFillRect(track, state);
    if (fill.right <= fill.left) return;

    const RECT trackInnerBounds = InsetRectCopy(track, 1, 1);

    RECT glowOuter = ClampRectToBounds(ExpandRectCopy(fill, 3, 2), trackInnerBounds);
    PaintVistaGlowLayer(hdc, glowOuter, std::min(radius + 2, std::max(2, RectHeight(glowOuter) / 2)),
                        BlendColor(trackTop, adaptFill(RGB(235, 255, 190)), 2, 10),
                        BlendColor(trackBottom, adaptFill(RGB(134, 219, 79)), 3, 10),
                        BlendColor(trackOuter, adaptFill(RGB(169, 244, 119)), 3, 10),
                        BlendColor(trackInner, adaptFill(RGB(244, 255, 219)), 2, 10));

    RECT glowMid = ClampRectToBounds(ExpandRectCopy(fill, 2, 1), trackInnerBounds);
    PaintVistaGlowLayer(hdc, glowMid, std::min(radius + 1, std::max(2, RectHeight(glowMid) / 2)),
                        BlendColor(trackTop, adaptFill(RGB(235, 255, 194)), 3, 10),
                        BlendColor(trackBottom, adaptFill(RGB(126, 214, 73)), 4, 10),
                        BlendColor(trackOuter, adaptFill(RGB(126, 210, 68)), 5, 10),
                        BlendColor(trackInner, adaptFill(RGB(248, 255, 231)), 3, 10));

    const int fillRadius = std::min(radius, RectHeight(fill) / 2);
    PaintRoundedSurface(hdc, fill, fillRadius,
                        adaptFill(RGB(237, 254, 196)),
                        adaptFill(RGB(52, 162, 34)),
                        adaptFill(RGB(47, 124, 26)),
                        adaptFill(RGB(248, 255, 232)),
                        adaptFill(RGB(255, 255, 255)),
                        adaptFill(RGB(208, 245, 149)));

    RECT fillBand = InsetRectCopy(fill, 2, 2);
    fillBand.bottom = std::min(fillBand.bottom,
                               fillBand.top + std::max(2, (RectHeight(fillBand) * 3) / 5));
    PaintClippedGlassBand(hdc, fillBand, std::max(2, fillRadius - 1),
                          adaptFill(RGB(255, 255, 239)),
                          adaptFill(RGB(191, 239, 118)));

    RECT cap = fill;
    const int capWidth = std::max(5, Scale(6));
    cap.left = std::max(static_cast<int>(fill.left), static_cast<int>(fill.right) - capWidth);
    if (cap.right > cap.left) {
        PaintRoundedSurface(hdc, cap, std::min(fillRadius, RectHeight(cap) / 2),
                            adaptFill(RGB(255, 255, 223)),
                            adaptFill(RGB(136, 224, 73)),
                            adaptFill(RGB(125, 206, 64)),
                            adaptFill(RGB(255, 255, 240)),
                            adaptFill(RGB(255, 255, 255)),
                            adaptFill(RGB(227, 249, 177)));
    }

    DrawChunkSeparators(hdc, fill, fillRadius,
                        adaptFill(RGB(244, 255, 236)), std::max(8, Scale(12)));
}

void PaintCrayonFooterProgress(HDC hdc, const RECT& track, int radius,
                               const FooterProgressState& state) {
    const bool dark = ThemeIsDark(state.appTheme);
    auto adapt = [&](COLORREF color) {
        return dark ? BlendColor(color, state.appTheme.panel, 2, 10) : color;
    };

    const COLORREF paperTop      = adapt(RGB(254, 249, 240));
    const COLORREF paperBottom   = adapt(RGB(238, 231, 222));
    const COLORREF paperOuter    = adapt(RGB(184, 171, 181));
    const COLORREF paperInner    = adapt(RGB(255, 252, 247));
    const COLORREF paperGlossTop = adapt(RGB(255, 255, 252));
    const COLORREF paperGlossBot = adapt(RGB(244, 238, 231));

    PaintRoundedSurface(hdc, track, radius,
                        paperTop, paperBottom,
                        paperOuter, paperInner,
                        paperGlossTop, paperGlossBot);

    RECT trackInset = InsetRectCopy(track, 2, 2);
    if (trackInset.right > trackInset.left && trackInset.bottom > trackInset.top) {
        PaintClippedGlassBand(hdc, trackInset, std::max(2, radius - 1),
                              adapt(RGB(255, 252, 247)),
                              adapt(RGB(236, 228, 220)));
        PaintCrayonTexture(hdc, trackInset, std::max(2, radius - 1),
                           adapt(RGB(255, 251, 245)),
                           adapt(RGB(229, 217, 209)),
                           adapt(RGB(246, 239, 233)));
    }

    const RECT fill = FooterFillRect(track, state);
    if (fill.right <= fill.left) return;

    const RECT bounds = InsetRectCopy(track, 1, 1);
    RECT glow = ClampRectToBounds(ExpandRectCopy(fill, 2, 1), bounds);
    PaintVistaGlowLayer(hdc, glow,
                        std::min(radius + 1, std::max(2, RectHeight(glow) / 2)),
                        adapt(RGB(255, 240, 245)),
                        adapt(RGB(226, 217, 240)),
                        adapt(RGB(209, 191, 219)),
                        adapt(RGB(255, 248, 251)));

    const int fillRadius = std::min(radius, RectHeight(fill) / 2);
    PaintRoundedSurface(hdc, fill, fillRadius,
                        adapt(RGB(255, 248, 252)),
                        adapt(RGB(234, 225, 239)),
                        adapt(RGB(176, 157, 183)),
                        adapt(RGB(255, 252, 253)),
                        adapt(RGB(255, 255, 255)),
                        adapt(RGB(244, 236, 247)));

    RECT fillInset = InsetRectCopy(fill, 2, 2);
    if (fillInset.right > fillInset.left && fillInset.bottom > fillInset.top) {
        const COLORREF pastelBands[] = {
            adapt(RGB(255, 188, 217)), // Cotton Candy
            adapt(RGB(255, 189, 136)), // Macaroni and Cheese
            adapt(RGB(255, 255, 153)), // Canary
            adapt(RGB(170, 240, 209)), // Magic Mint
            adapt(RGB(128, 218, 235)), // Sky Blue
            adapt(RGB(205, 164, 222)), // Wisteria
        };
        PaintCrayonBands(hdc, fillInset, std::max(2, fillRadius - 1),
                         pastelBands, static_cast<int>(sizeof(pastelBands) / sizeof(pastelBands[0])));
        PaintCrayonTexture(hdc, fillInset, std::max(2, fillRadius - 1),
                           adapt(RGB(255, 252, 246)),
                           adapt(RGB(233, 205, 214)),
                           adapt(RGB(255, 247, 241)));
    }

    RECT fillHighlight = fillInset;
    fillHighlight.bottom = std::min(fillHighlight.bottom,
                                    fillHighlight.top + std::max(2, RectHeight(fillHighlight) / 2));
    if (fillHighlight.right > fillHighlight.left && fillHighlight.bottom > fillHighlight.top) {
        PaintClippedGlassBand(hdc, fillHighlight, std::max(2, fillRadius - 1),
                              adapt(RGB(255, 255, 252)),
                              adapt(RGB(255, 235, 243)));
    }

    RECT cap = fill;
    const int capWidth = std::max(6, Scale(7));
    cap.left = std::max(static_cast<int>(fill.left), static_cast<int>(fill.right) - capWidth);
    if (cap.right > cap.left) {
        PaintRoundedSurface(hdc, cap, std::min(fillRadius, RectHeight(cap) / 2),
                            adapt(RGB(255, 253, 250)),
                            adapt(RGB(255, 229, 234)),
                            adapt(RGB(223, 181, 194)),
                            adapt(RGB(255, 255, 255)),
                            adapt(RGB(255, 255, 255)),
                            adapt(RGB(255, 241, 246)));
    }
}

void PaintFooterProgress(HDC hdc, const RECT& client, const FooterProgressState& state) {
    FillSolidRect(hdc, client, state.appTheme.panel);

    RECT track = client;
    InflateRect(&track, -1, -1);
    if (track.right <= track.left || track.bottom <= track.top) return;

    const int radius = std::max(2, std::min(RectHeight(track) / 2, Scale(4)));
    if (state.theme == FooterProgressTheme::Vista) {
        PaintVistaFooterProgress(hdc, track, radius, state);
    } else if (state.theme == FooterProgressTheme::Crayon) {
        PaintCrayonFooterProgress(hdc, track, radius, state);
    } else {
        PaintNativeFooterProgress(hdc, track, radius, state);
    }
}

LRESULT CALLBACK FooterProgressWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCCREATE: {
        auto* state = new FooterProgressState();
        if (!state) return FALSE;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }
    case PBM_SETRANGE32: {
        if (auto* state = FooterProgressFromHwnd(hwnd)) {
            state->minValue = static_cast<int>(wParam);
            state->maxValue = std::max(state->minValue, static_cast<int>(lParam));
            state->pos = std::clamp(state->pos, state->minValue, state->maxValue);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case PBM_SETPOS: {
        int previous = 0;
        if (auto* state = FooterProgressFromHwnd(hwnd)) {
            previous   = state->pos;
            state->pos = std::clamp(static_cast<int>(wParam), state->minValue, state->maxValue);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return previous;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);
        if (client.right > client.left && client.bottom > client.top) {
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = memDC
                ? CreateCompatibleBitmap(hdc, client.right - client.left, client.bottom - client.top)
                : nullptr;
            if (memDC && memBmp) {
                HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
                if (const auto* state = FooterProgressFromHwnd(hwnd))
                    PaintFooterProgress(memDC, client, *state);
                BitBlt(hdc, 0, 0, client.right - client.left, client.bottom - client.top,
                       memDC, 0, 0, SRCCOPY);
                SelectObject(memDC, oldBmp);
            } else if (const auto* state = FooterProgressFromHwnd(hwnd)) {
                PaintFooterProgress(hdc, client, *state);
            }
            if (memBmp) DeleteObject(memBmp);
            if (memDC) DeleteDC(memDC);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCDESTROY:
        delete FooterProgressFromHwnd(hwnd);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RegisterFooterProgressClass() {
    static bool registered = false;
    if (registered) return;

    WNDCLASSW wc{};
    wc.lpfnWndProc   = FooterProgressWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = kFooterProgressClassName;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    registered = true;
}

HWND CreateFooterProgress(HWND parent) {
    RegisterFooterProgressClass();
    return CreateWindowExW(0, kFooterProgressClassName, nullptr,
        WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
// CreateAllUIControls
// Controls that are permanently hidden (retired UI) are not created at all.
// Their ControlHandles members remain nullptr, which all downstream code
// already guards for via "if (h)" / "if (ctrl)" checks.
// ---------------------------------------------------------------------------

void CreateAllUIControls(HWND parent, ControlHandles& c) {
    c.sidebar = CreateWindowExW(0, L"SeatingChartSidebar", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
        0,0,0,0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);

    HWND p = c.sidebar ? c.sidebar : parent;

    // Fixed header / footer — always visible
    c.titleLabel      = MakeLabel(p, L"Seating Chart");
    c.summaryLabel    = MakeLabel(p, L"");
    c.statusLabel     = MakeLabel(p, L"Ready");
    c.footerMetaLabel = MakeLabel(p, L"Rounds left: --");
    c.footerProgress  = CreateFooterProgress(p);
    if (c.footerProgress) {
        SetFooterProgressRange(c.footerProgress, 0, 100);
        SetFooterProgressPos(c.footerProgress, 0);
    }

    // ---- Roster tab --------------------------------------------------------
    c.importRoster    = MakeButton(p, L"Paste", kImportRosterId);
    c.loadRoster      = MakeButton(p, L"Load",  kLoadRosterId);
    c.saveNow         = MakeButton(p, L"Save",       kSaveNowId);
    c.autoAssign      = MakeButton(p, L"Seat Automatically", kAutoAssignId);
    c.clearAllSeats   = MakeButton(p, L"Clear", kClearAllSeatsId);
    c.rosterListLabel = MakeLabel(p, L"Students");
    if (c.rosterListLabel)
        SetWindowTextW(c.rosterListLabel, L"Students");
    c.rosterList      = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_EXTENDEDSEL,
        0,0,0,0, p, reinterpret_cast<HMENU>(kRosterListId),
        GetModuleHandleW(nullptr), nullptr);
    c.assignSelectedRoster = MakeButton(p, L"Seat", kAssignSelectedRosterId);
    c.bulkTag              = MakeButton(p, L"Tag",  kBulkTagId);

    // Two-column student ListView
    c.rosterView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kRosterViewId)),
        GetModuleHandleW(nullptr), nullptr);
    if (c.rosterView) {
        ListView_SetExtendedListViewStyle(c.rosterView,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
            LVS_EX_INFOTIP); // enables LVN_GETINFOTIP → per-row tooltip
        LVCOLUMNW lvc{};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
        lvc.fmt  = LVCFMT_CENTER;
        lvc.iSubItem = 0; lvc.cx = Scale(28);
        lvc.pszText = const_cast<wchar_t*>(L"#");
        ListView_InsertColumn(c.rosterView, 0, &lvc);
        // Win32 ignores LVCFMT_CENTER on column 0 at insert time; apply it again
        // after insertion — this is honoured on Windows Vista+.  Use a throwaway
        // LVCOLUMNW so we don't narrow `lvc.mask` (doing that previously dropped
        // LVCF_TEXT, so the "First Name"/"Last Name" headers below never applied).
        LVCOLUMNW col0fmt{}; col0fmt.mask = LVCF_FMT; col0fmt.fmt = LVCFMT_CENTER;
        ListView_SetColumn(c.rosterView, 0, &col0fmt);
        lvc.iSubItem = 1; lvc.cx = Scale(100);
        lvc.pszText = const_cast<wchar_t*>(L"First Name");
        ListView_InsertColumn(c.rosterView, 1, &lvc);
        lvc.iSubItem = 2; lvc.cx = Scale(100);
        lvc.pszText = const_cast<wchar_t*>(L"Last Name");
        ListView_InsertColumn(c.rosterView, 2, &lvc);
    }
    // "+" add-student button — positioned by LayoutRosterPanel as the table's
    // final row (full table width, one row tall), like the class strip's "+".
    c.addStudentBtn = MakeButton(p, L"+ Student", kAddStudentId);

    c.showLastNamesBtn = MakeButton(p, L"Show Last", kShowLastNamesId,
                                    BS_AUTOCHECKBOX | BS_PUSHLIKE);

    // ---- Rules tab ---------------------------------------------------------
    c.keepApartHeader = MakeLabel(p, L"Keep Apart");
    c.keepApartDesc   = MakeLabel(p, L"Students who should not sit near each other.");
    c.keepApartList   = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kKeepApartListId)),
        GetModuleHandleW(nullptr), nullptr);
    if (c.keepApartList) {
        ListView_SetExtendedListViewStyle(c.keepApartList,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW lvc{};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        lvc.iSubItem = 0; lvc.cx = Scale(110);
        lvc.pszText = const_cast<wchar_t*>(L"Student A");
        ListView_InsertColumn(c.keepApartList, 0, &lvc);
        lvc.iSubItem = 1; lvc.cx = Scale(110);
        lvc.pszText = const_cast<wchar_t*>(L"Student B");
        ListView_InsertColumn(c.keepApartList, 1, &lvc);
        if (HWND hdr = ListView_GetHeader(c.keepApartList))
            SetWindowLongW(hdr, GWL_STYLE,
                GetWindowLongW(hdr, GWL_STYLE) | HDS_NOSIZING);
    }
    c.addKeepApartBtn = MakeButton(p, L"+ Pair", kAddKeepApartId);
    c.remKeepApartBtn = MakeButton(p, L"Remove", kRemKeepApartId);

    c.keepTogetherHeader = MakeLabel(p, L"Keep Together");
    c.keepTogetherDesc   = MakeLabel(p, L"Students who should sit together or nearby.");
    c.keepTogetherList   = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kKeepTogetherListId)),
        GetModuleHandleW(nullptr), nullptr);
    if (c.keepTogetherList) {
        ListView_SetExtendedListViewStyle(c.keepTogetherList,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW lvc{};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        lvc.iSubItem = 0; lvc.cx = Scale(110);
        lvc.pszText = const_cast<wchar_t*>(L"Student A");
        ListView_InsertColumn(c.keepTogetherList, 0, &lvc);
        lvc.iSubItem = 1; lvc.cx = Scale(110);
        lvc.pszText = const_cast<wchar_t*>(L"Student B");
        ListView_InsertColumn(c.keepTogetherList, 1, &lvc);
        if (HWND hdr = ListView_GetHeader(c.keepTogetherList))
            SetWindowLongW(hdr, GWL_STYLE,
                GetWindowLongW(hdr, GWL_STYLE) | HDS_NOSIZING);
    }
    c.addKeepTogetherBtn = MakeButton(p, L"+ Pair", kAddKeepTogetherId);
    c.remKeepTogetherBtn = MakeButton(p, L"Remove", kRemKeepTogetherId);

    c.deskTagHeader = MakeLabel(p, L"Desk Rules");
    c.deskTagDesc   = MakeLabel(p,
        L"Restrict students on the seating chart to desks with a certain tag.");
    c.deskTagList   = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kDeskTagListId)),
        GetModuleHandleW(nullptr), nullptr);
    if (c.deskTagList) {
        LVCOLUMNW lvc{};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        lvc.iSubItem = 0; lvc.cx = Scale(110);
        lvc.pszText = const_cast<wchar_t*>(L"Student");
        ListView_InsertColumn(c.deskTagList, 0, &lvc);
        lvc.iSubItem = 1; lvc.cx = Scale(110);
        lvc.pszText = const_cast<wchar_t*>(L"Tag");
        ListView_InsertColumn(c.deskTagList, 1, &lvc);
        if (HWND hdr = ListView_GetHeader(c.deskTagList))
            SetWindowLongW(hdr, GWL_STYLE,
                GetWindowLongW(hdr, GWL_STYLE) | HDS_NOSIZING);
    }
    c.addDeskTagRuleBtn = MakeButton(p, L"Desk Rules...", kAddDeskTagRuleId);
    c.remDeskTagRuleBtn = MakeButton(p, L"Remove",     kRemDeskTagRuleId);

    // ---- Arrange tab -------------------------------------------------------
    c.layoutToolsLabel = MakeLabel(p, L"Add");
    c.addSmartboard    = MakeButton(p, L"Board",         kAddSmartboardId);
    c.addTrap          = MakeButton(p, L"Angle Desk",    kAddTrapezoidId);
    c.addDesk          = MakeButton(p, L"Desk",          kAddDeskId);
    c.addTable         = MakeButton(p, L"Table (4)",     kAddTableId);
    c.addBigTable      = MakeButton(p, L"Large Table",   kAddBigTableId);
    c.addBlock         = MakeButton(p, L"Label",         kAddBlockId);
    c.addTrapPair      = MakeButton(p, L"Pair",          kAddTrapPairId);
    c.addTrapPod       = MakeButton(p, L"Pod",           kAddTrapPodId);

    c.layoutTransformLabel = MakeLabel(p, L"Selected");
    c.deleteLayout        = MakeButton(p, L"Delete",       kDeleteLayoutItemId);
    c.duplicateLayoutItem = MakeButton(p, L"Duplicate",    kDuplicateLayoutItemId);
    c.lockItem            = MakeButton(p, L"Lock",         kLockItemId);
    c.rotateCW            = MakeButton(p, L"Rotate R", kRotateCWId);
    c.rotateCCW           = MakeButton(p, L"Rotate L", kRotateCCWId);
    c.flipH               = MakeButton(p, L"Flip",     kFlipHId);
    c.selectAllLayout     = MakeButton(p, L"Select All", kSelectAllLayoutId);
    c.toggleVisible       = MakeButton(p, L"Hide/Show",  kToggleVisibleId);
    c.sendLayoutBack      = MakeButton(p, L"Back",       kSendLayoutBackId);
    c.bringLayoutFront    = MakeButton(p, L"Front",      kBringLayoutFrontId);
    c.quickFillSeats      = MakeButton(p, L"Random Seats", kQuickFillSeatsId);
    c.showAllObjects      = MakeButton(p, L"Show All",   kShowAllObjectsId);

    // ---- Layout inspector (Arrange tab) ------------------------------------
    c.layoutInspectorLabel = MakeLabel(p, L"Size & Position");
    c.layoutNameLabel      = MakeLabel(p, L"Name");
    c.layoutLabelEdit      = MakeEdit(p, kLayoutLabelEditId);
    c.layoutXLabel         = MakeLabel(p, L"X");
    c.layoutXEdit          = MakeEdit(p, kLayoutXEditId, ES_NUMBER);
    c.layoutXSpin          = MakeSpin(p, kLayoutXSpinId);
    c.layoutYLabel         = MakeLabel(p, L"Y");
    c.layoutYEdit          = MakeEdit(p, kLayoutYEditId, ES_NUMBER);
    c.layoutYSpin          = MakeSpin(p, kLayoutYSpinId);
    c.layoutWidthLabel     = MakeLabel(p, L"W");
    c.layoutWidthEdit      = MakeEdit(p, kLayoutWidthEditId, ES_NUMBER);
    c.layoutWSpin          = MakeSpin(p, kLayoutWSpinId);
    c.layoutHeightLabel    = MakeLabel(p, L"H");
    c.layoutHeightEdit     = MakeEdit(p, kLayoutHeightEditId, ES_NUMBER);
    c.layoutHSpin          = MakeSpin(p, kLayoutHSpinId);
    c.layoutCapacityLabel  = MakeLabel(p, L"Seats");
    c.layoutCapacityEdit   = MakeEdit(p, kLayoutCapacityEditId, ES_NUMBER);
    c.applyLayoutItem      = MakeButton(p, L"Apply", kApplyLayoutItemId);

    // ---- Groups tab --------------------------------------------------------
    c.groupSizeLabel    = MakeLabel(p, L"Groups of:");
    // Retired dropdown: a COMBOBOX cannot be themed readably inside the custom
    // dark sidebar (white-on-white face, unthemed list).  It stays hidden so the
    // selection logic keeps a fallback, and the visible UI is a −/+ stepper.
    c.groupSizeCombo    = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kGroupSizeComboId)),
        GetModuleHandleW(nullptr), nullptr);
    c.groupSizeMinus = MakeButton(p, L"−", kGroupSizeMinusId); // −
    c.groupSizeValue = MakeLabel(p, L"2");
    c.groupSizePlus  = MakeButton(p, L"+", kGroupSizePlusId);
    c.groupOrLabel      = MakeLabel(p, L"or");
    c.groupOrValLabel   = MakeLabel(p, L"—");
    c.groupSummaryLabel = MakeLabel(p,
        L"Pick a group size to see the active pattern and exact count.");
    c.shuffleGroupsBtn  = MakeButton(p, L"Make Groups",   kShuffleGroupsId);
    c.groupResetBtn     = MakeButton(p, L"Reset History", kGroupResetId);
    c.groupKeepApartToggle    = MakeButton(p, L"Keep Apart Rules",
                                           kGroupKeepApartToggleId);
    c.groupKeepTogetherToggle = MakeButton(p, L"Keep Together Rules",
                                           kGroupKeepTogetherToggleId);
    c.groupApartSameChk = CreateWindowExW(
        0, L"BUTTON", L"Same as seating rules",
        WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kGroupApartSameId)),
        GetModuleHandleW(nullptr), nullptr);
    c.groupTogetherSameChk = CreateWindowExW(
        0, L"BUTTON", L"Same as seating rules",
        WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kGroupTogetherSameId)),
        GetModuleHandleW(nullptr), nullptr);
    c.groupAvoidSameNumberCheck = CreateWindowExW(
        0, L"BUTTON", L"Avoid same group number",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kGroupAvoidSameNumberId)),
        GetModuleHandleW(nullptr), nullptr);
    c.groupAvoidSamePartnersCheck = CreateWindowExW(
        0, L"BUTTON", L"Avoid repeat classmates",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kGroupAvoidSamePartnersId)),
        GetModuleHandleW(nullptr), nullptr);
    c.groupAvoidSameFullGroupCheck = CreateWindowExW(
        0, L"BUTTON", L"No exact repeats",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kGroupAvoidSameFullGroupId)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c.groupAvoidSameNumberCheck,   BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(c.groupAvoidSamePartnersCheck, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(c.groupAvoidSameFullGroupCheck,BM_SETCHECK, BST_CHECKED, 0);
    // Default: group rules mirror the seating rules (synced from saved state later).
    SendMessageW(c.groupApartSameChk,    BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(c.groupTogetherSameChk, BM_SETCHECK, BST_CHECKED, 0);

    // ---- Tab control -------------------------------------------------------
    c.tabControl = CreateWindowExW(0, WC_TABCONTROL, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_HOTTRACK,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kTabControlId)),
        GetModuleHandleW(nullptr), nullptr);
    if (c.tabControl) {
        TCITEMW ti{};
        ti.mask = TCIF_TEXT;
        ti.pszText = const_cast<wchar_t*>(L"Students"); TabCtrl_InsertItem(c.tabControl, 0, &ti);
        ti.pszText = const_cast<wchar_t*>(L"Rules");    TabCtrl_InsertItem(c.tabControl, 1, &ti);
        ti.pszText = const_cast<wchar_t*>(L"Room");     TabCtrl_InsertItem(c.tabControl, 2, &ti);
        ti.pszText = const_cast<wchar_t*>(L"Groups");   TabCtrl_InsertItem(c.tabControl, 3, &ti);
        TabCtrl_SetCurSel(c.tabControl, 2);
    }

    // ---- Tooltips ----------------------------------------------------------
    HWND tip = CreateTooltipWnd(p);

    AddTip(tip, c.importRoster,
        L"Paste the clipboard as the roster\n"
        L"One name per line. Names can include tags like Alice [Behavior].");
    AddTip(tip, c.loadRoster,
        L"Load a roster from a .txt, .csv, or .tsv file\n"
        L"One name per line — first column used for CSV/TSV");
    AddTip(tip, c.saveNow,
        L"Save the current state immediately\n"
        L"Shortcut: Ctrl+S\n"
        L"(The app also auto-saves a few seconds after each change)");
    AddTip(tip, c.autoAssign,
        L"Run the constraint-based auto-assign solver\n"
        L"Apply rules in the box below first, then click here.\n"
        L"Affinity satisfaction % is shown in the header after the solve.");
    AddTip(tip, c.quickFillSeats,
        L"Randomly place unassigned students into empty seats\n"
        L"Does not apply rules — use for a quick first draft or when rules are not needed");
    AddTip(tip, c.clearAllSeats,
        L"Remove all student assignments from every seat\n"
        L"(Undo with Ctrl+Z)");
    AddTip(tip, c.rosterView,
        L"Students table\n"
        L"Double-click to edit, drag a row onto a seat, or right-click for actions.");
    AddTip(tip, c.assignSelectedRoster,
        L"Assign the selected student to the focused seat on the chart\n"
        L"Tip: click a seat on the chart first, then select a student row here");
    AddTip(tip, c.bulkTag,
        L"Apply or remove a tag for all selected students in the table\n"
        L"Select multiple names with Ctrl+click, then click here");
    AddTip(tip, c.showLastNamesBtn,
        L"Toggle whether names are shown with their last name everywhere in the app");

    AddTip(tip, c.addSmartboard,   L"Add a smartboard / whiteboard to the layout");
    AddTip(tip, c.addTrap,         L"Add a trapezoid desk (single seat, angled)");
    AddTip(tip, c.addDesk,         L"Add a rectangular single-student desk");
    AddTip(tip, c.addTable,        L"Add a 4-seat table");
    AddTip(tip, c.addBigTable,     L"Add a large table with configurable seat count");
    AddTip(tip, c.addBlock,        L"Add a label block (no seats — for room labels)");
    AddTip(tip, c.addTrapPair,     L"Add a trapezoid pair (2 seats facing each other)");
    AddTip(tip, c.addTrapPod,      L"Add a trapezoid pod (4 seats in a cluster)");
    AddTip(tip, c.deleteLayout,    L"Delete selected item(s)\nShortcut: Delete key");
    AddTip(tip, c.toggleVisible,
        L"Toggle visibility of selected item(s)\n"
        L"Hidden items are not shown on print/export");
    AddTip(tip, c.rotateCW,
        L"Rotate selected item(s) 90\xB0 clockwise\nShortcut: R");
    AddTip(tip, c.rotateCCW,
        L"Rotate selected item(s) 90\xB0 counter-clockwise\nShortcut: Shift+R");
    AddTip(tip, c.flipH,
        L"Flip selected item(s) horizontally\nShortcut: F");
    AddTip(tip, c.lockItem,
        L"Lock / unlock selected item\n"
        L"Locked items cannot be moved or resized accidentally\n"
        L"Shortcut: L");
    AddTip(tip, c.selectAllLayout,
        L"Select all layout items\nShortcut: Ctrl+A");
    AddTip(tip, c.duplicateLayoutItem,
        L"Duplicate the selected item (also Ctrl+D)");
    AddTip(tip, c.sendLayoutBack,
        L"Move selected item one step towards the back (below other items)");
    AddTip(tip, c.bringLayoutFront,
        L"Move selected item one step towards the front (above other items)");

    AddTip(tip, c.groupResetBtn,
        L"Forget past groupings so students can be paired together again (the round counter restarts)");
    AddTip(tip, c.groupAvoidSameNumberCheck,
        L"Keeps students rotating through different group numbers — no one lands in the same numbered group (Group 1, Group 2, …) twice");
    AddTip(tip, c.groupAvoidSamePartnersCheck,
        L"In each new group a student shares at most one classmate with any of their past groups, so partners keep changing");
    AddTip(tip, c.groupAvoidSameFullGroupCheck,
        L"The exact same set of students will never be grouped together again");
}

void SetFooterProgressRange(HWND progress, int minValue, int maxValue) {
    if (!progress) return;
    if (auto* state = FooterProgressFromHwnd(progress)) {
        state->minValue = std::min(minValue, maxValue);
        state->maxValue = std::max(minValue, maxValue);
        state->pos = std::clamp(state->pos, state->minValue, state->maxValue);
        InvalidateRect(progress, nullptr, FALSE);
        return;
    }
    SendMessageW(progress, PBM_SETRANGE32, minValue, maxValue);
}

void SetFooterProgressPos(HWND progress, int pos) {
    if (!progress) return;
    if (auto* state = FooterProgressFromHwnd(progress)) {
        state->pos = std::clamp(pos, state->minValue, state->maxValue);
        InvalidateRect(progress, nullptr, FALSE);
        return;
    }
    SendMessageW(progress, PBM_SETPOS, pos, 0);
}

void SetFooterProgressTheme(HWND progress, FooterProgressTheme theme,
                            const ThemeColors& appTheme) {
    if (!progress) return;
    if (auto* state = FooterProgressFromHwnd(progress)) {
        state->theme    = theme;
        state->appTheme = appTheme;
        InvalidateRect(progress, nullptr, TRUE);
    }
}

// ---------------------------------------------------------------------------
// ApplyFontsToControls
// ---------------------------------------------------------------------------

void ApplyFontsToControls(const ControlHandles& c, const Renderer& r) {
    auto set = [](HWND h, HFONT f) {
        if (h && f) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
    };

    // Common UI font
    for (HWND h : {
        c.importRoster, c.loadRoster, c.saveNow, c.autoAssign,
        c.clearAllSeats, c.assignSelectedRoster, c.bulkTag, c.showLastNamesBtn,
        c.rosterListLabel, c.rosterList, c.rosterView, c.addStudentBtn,
        c.keepApartHeader, c.keepApartDesc, c.keepApartList,
        c.addKeepApartBtn, c.remKeepApartBtn,
        c.keepTogetherHeader, c.keepTogetherDesc, c.keepTogetherList,
        c.addKeepTogetherBtn, c.remKeepTogetherBtn,
        c.deskTagHeader, c.deskTagDesc, c.deskTagList,
        c.addDeskTagRuleBtn, c.remDeskTagRuleBtn,
        c.layoutToolsLabel, c.addSmartboard, c.addTrap, c.addDesk,
        c.addTable, c.addBigTable, c.addBlock, c.addTrapPair, c.addTrapPod,
        c.layoutTransformLabel,
        c.deleteLayout, c.duplicateLayoutItem, c.lockItem,
        c.rotateCW, c.rotateCCW, c.flipH, c.selectAllLayout,
        c.toggleVisible, c.sendLayoutBack, c.bringLayoutFront,
        c.quickFillSeats, c.showAllObjects,
        c.layoutInspectorLabel,
        c.layoutNameLabel, c.layoutLabelEdit,
        c.layoutXLabel, c.layoutXEdit,
        c.layoutYLabel, c.layoutYEdit,
        c.layoutWidthLabel, c.layoutWidthEdit,
        c.layoutHeightLabel, c.layoutHeightEdit,
        c.layoutCapacityLabel, c.layoutCapacityEdit,
        c.applyLayoutItem,
        c.groupSizeLabel, c.groupSizeCombo,
        c.groupSizeMinus, c.groupSizeValue, c.groupSizePlus,
        c.groupOrLabel, c.groupOrValLabel, c.groupSummaryLabel,
        c.shuffleGroupsBtn, c.groupResetBtn,
        c.groupKeepApartToggle, c.groupKeepTogetherToggle,
        c.groupApartSameChk, c.groupTogetherSameChk,
        c.groupAvoidSameNumberCheck, c.groupAvoidSamePartnersCheck,
        c.groupAvoidSameFullGroupCheck,
        c.summaryLabel, c.statusLabel, c.footerMetaLabel,
        c.tabControl,
    }) set(h, r.UiFont());

    set(c.titleLabel, r.TitleFont());
    for (HWND h : {c.rosterListLabel, c.layoutToolsLabel, c.layoutTransformLabel,
                   c.layoutInspectorLabel, c.statusLabel, c.footerMetaLabel})
        set(h, r.SectionFont());
}

// ---------------------------------------------------------------------------
// Sync helpers
// ---------------------------------------------------------------------------

void SyncRosterEditFromRoster(const AppState& s, const ControlHandles& c) {
    if (!c.rosterEdit) return;
    std::wstring text;
    for (const auto& n : s.roster) { text += n; text += L"\r\n"; }
    SetWindowTextW(c.rosterEdit, text.c_str());
}

void SyncRestrictionEditFromRules(const AppState& s, const ControlHandles& c) {
    if (!c.restrictionEdit) return;
    std::wstring text;
    for (const auto& r : s.restrictions) {
        text += r.first + L" | " + r.second;
        if (r.radius > 0) text += L" @" + std::to_wstring(r.radius);
        text += L"\r\n";
    }
    for (const auto& a : s.affinities)
        text += a.first + L" + " + a.second + L"\r\n";
    for (const auto& t : s.mustTogether)
        text += t.first + L" == " + t.second + L"\r\n";
    SetWindowTextW(c.restrictionEdit, text.c_str());
}

void SyncGroupRulesEditFromState(const AppState& s, const ControlHandles& c) {
    if (!c.groupRulesEdit) return;
    std::wstring text;
    for (const auto& g : s.groupAffinities) {
        if (g.empty()) continue;
        text += L"Group: ";
        for (size_t i = 0; i < g.size(); ++i) {
            if (i > 0) text += L" ";
            text += g[i];
        }
        text += L"\r\n";
    }
    SetWindowTextW(c.groupRulesEdit, text.c_str());
}

void RefreshRosterList(const AppState& s, const ControlHandles& c) {
    if (!c.rosterList) return;
    SendMessageW(c.rosterList, LB_RESETCONTENT, 0, 0);
    for (const auto& n : s.roster)
        SendMessageW(c.rosterList, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(DisplayStudentName(n, s.showLastNames).c_str()));
}

void SyncRosterView(const AppState& s, const ControlHandles& c) {
    if (!c.rosterView) return;
    ListView_DeleteAllItems(c.rosterView);
    for (int i = 0; i < static_cast<int>(s.roster.size()); ++i) {
        const auto& name = s.roster[static_cast<size_t>(i)];
        const size_t sp  = name.find(L' ');
        const std::wstring first = (sp != std::wstring::npos) ? name.substr(0, sp) : name;
        const std::wstring last  = (s.showLastNames && sp != std::wstring::npos)
                                   ? name.substr(sp + 1) : L"";
        LVITEMW lvi{};
        lvi.mask     = LVIF_TEXT;
        lvi.iItem    = i;
        lvi.iSubItem = 0;
        std::wstring num = std::to_wstring(i + 1);
        lvi.pszText  = num.data();
        const int idx = ListView_InsertItem(c.rosterView, &lvi);
        if (idx >= 0) {
            ListView_SetItemText(c.rosterView, idx, 1, const_cast<wchar_t*>(first.c_str()));
            ListView_SetItemText(c.rosterView, idx, 2, const_cast<wchar_t*>(last.c_str()));
        }
    }

    // No permanent ghost/blank rows: new students are added via the "+" row
    // button under the table, or by Enter/Tab while typing in the last row
    // (BeginAddStudentRow inserts a temporary row only for the duration of the
    // edit and removes it again if nothing is committed).
}

void SyncRulesLists(const AppState& s, const ControlHandles& c,
                    const std::vector<Restriction>& apartRules,
                    const std::vector<Restriction>& togetherRules) {
    // Ghost rows: total displayed = max(3, realCount + 1) so there is always at
    // least one empty row to click into.  When all visible rows are filled the
    // +1 ensures a fresh blank row appears at the bottom automatically.
    // The apart/together vectors are passed in (not read from `s`) so the Groups
    // tab can show its own rule set when "Same as seating" is unchecked.
    auto fillList = [](HWND lv, const std::vector<Restriction>& rules) {
        if (!lv) return;
        ListView_DeleteAllItems(lv);
        const int realCount = static_cast<int>(rules.size());
        const int totalRows = std::max(3, realCount + 1);
        for (int i = 0; i < totalRows; ++i) {
            const bool ghost = (i >= realCount);
            LVITEMW lvi{};
            lvi.mask     = LVIF_TEXT;
            lvi.iItem    = i;
            lvi.iSubItem = 0;
            lvi.pszText  = ghost ? const_cast<wchar_t*>(L"")
                                 : const_cast<wchar_t*>(rules[static_cast<size_t>(i)].first.c_str());
            const int idx = ListView_InsertItem(lv, &lvi);
            if (idx >= 0)
                ListView_SetItemText(lv, idx, 1,
                    ghost ? const_cast<wchar_t*>(L"")
                          : const_cast<wchar_t*>(rules[static_cast<size_t>(i)].second.c_str()));
        }
    };
    fillList(c.keepApartList,    apartRules);
    fillList(c.keepTogetherList, togetherRules);

    if (c.deskTagList) {
        ListView_DeleteAllItems(c.deskTagList);
        int row = 0;
        for (const auto& name : s.roster) {
            const StudentInfo* info = s.FindStudent(name);
            if (!info) continue;
            for (const auto& tag : info->forbiddenDesks) {
                LVITEMW lvi{};
                lvi.mask = LVIF_TEXT; lvi.iItem = row; lvi.iSubItem = 0;
                lvi.pszText = const_cast<wchar_t*>(name.c_str());
                const int idx = ListView_InsertItem(c.deskTagList, &lvi);
                if (idx >= 0)
                    ListView_SetItemText(c.deskTagList, idx, 1,
                        const_cast<wchar_t*>(tag.c_str()));
                ++row;
            }
        }
    }
}

void SyncLayoutInspectorWithSelection(const AppState& s, const ControlHandles& c) {
    // Inspector edit fields are retired — all handles are nullptr; these calls are no-ops.
    const bool valid = s.selectedLayoutItem.has_value() &&
                       *s.selectedLayoutItem < static_cast<int>(s.layoutItems.size());
    if (!valid) {
        for (HWND h : {c.layoutLabelEdit, c.layoutXEdit, c.layoutYEdit,
                       c.layoutWidthEdit, c.layoutHeightEdit, c.layoutCapacityEdit})
            if (h) SetWindowTextW(h, L"");
        return;
    }
    const auto& item = s.layoutItems[static_cast<size_t>(*s.selectedLayoutItem)];
    if (c.layoutLabelEdit) SetWindowTextW(c.layoutLabelEdit, item.label.c_str());
    auto si = [](HWND h, int v) { if (h) SetWindowTextW(h, std::to_wstring(v).c_str()); };
    si(c.layoutXEdit,      item.bounds.left);
    si(c.layoutYEdit,      item.bounds.top);
    si(c.layoutWidthEdit,  item.bounds.right  - item.bounds.left);
    si(c.layoutHeightEdit, item.bounds.bottom - item.bounds.top);
    if (item.type == LayoutItemType::BigTable) {
        const int cap = item.capacity > 0 ? item.capacity : LayoutItemDefaultCapacity(item.type);
        si(c.layoutCapacityEdit, cap);
    } else if (c.layoutCapacityEdit) {
        SetWindowTextW(c.layoutCapacityEdit, L"");
    }
    if (c.lockItem) SetWindowTextW(c.lockItem, item.locked ? L"Unlock" : L"Lock");
}

void UpdateSidebarText(const AppState& s, const ControlHandles& c) {
    if (c.titleLabel) {
        const std::wstring title = s.className.empty() ? L"Seating Chart" : s.className;
        SetWindowTextW(c.titleLabel, title.c_str());
    }
    if (c.summaryLabel) {
        int layoutSeats = 0, layoutAssigned = 0;
        for (const auto& item : s.layoutItems) {
            layoutSeats += LayoutItemSeats(item);
            for (const auto& occ : item.occupants) if (!occ.empty()) ++layoutAssigned;
        }
        const bool isLayout = (s.chartMode == ChartMode::Layout);

        std::wstring line1 = isLayout ? L"Room: " : L"Seats: ";
        if (isLayout) {
            line1 += std::to_wstring(static_cast<int>(s.layoutItems.size())) + L" items";
            if (layoutSeats > 0)
                line1 += L" | " + std::to_wstring(layoutSeats) + L" seats";
        } else {
            line1 += std::to_wstring(layoutAssigned) + L"/" +
                     std::to_wstring(std::max(0, layoutSeats)) + L" filled";
            if (!s.roster.empty())
                line1 += L" | " + std::to_wstring(static_cast<int>(s.roster.size())) + L" students";
            const int unassigned = static_cast<int>(s.roster.size()) - layoutAssigned;
            if (unassigned > 0)
                line1 += L" | " + std::to_wstring(unassigned) + L" unplaced";
        }

        std::wstring next;
        if (layoutSeats == 0) {
            next = L"Next: add desks in Room.";
        } else if (s.roster.empty()) {
            next = L"Next: add students.";
        } else if (layoutAssigned < std::min(layoutSeats, static_cast<int>(s.roster.size()))) {
            next = L"Next: drag names or use Seat Automatically.";
        } else {
            next = L"Ready: print, export, or make groups.";
        }

        std::wstring sum = line1 + L"\n" + next;
        SetWindowTextW(c.summaryLabel, sum.c_str());
    }
    if (c.statusLabel) SetWindowTextW(c.statusLabel, s.status.c_str());
    // roomWidthEdit / roomHeightEdit / frontEdgeButton are retired (always nullptr).
}

void UpdateButtonState(const AppState& s, const ControlHandles& c, bool aaRunning) {
    const BOOL seats        = (s.chartMode == ChartMode::Seats);
    const BOOL layout       = (s.chartMode == ChartMode::Layout);
    const BOOL hasRoster    = !s.roster.empty();
    int assignedSeats = 0;
    for (const auto& item : s.layoutItems)
        for (const auto& occ : item.occupants)
            if (!occ.empty()) ++assignedSeats;
    const BOOL hasSeats     = TotalLayoutSeats(s.layoutItems) > 0;
    const BOOL hasAssigned  = assignedSeats > 0;
    const BOOL hasFocusSeat = s.selectedLayoutSeat.has_value();
    const BOOL hasItem      = s.selectedLayoutItem.has_value();
    const BOOL hasAny       = !s.selectedLayoutItems.empty();
    const bool itemLocked   = hasItem &&
        s.layoutItems[static_cast<size_t>(*s.selectedLayoutItem)].locked;

    EnableWindow(c.importRoster, TRUE);
    EnableWindow(c.loadRoster,   TRUE);
    EnableWindow(c.saveNow,      TRUE);
    EnableWindow(c.showLastNamesBtn, TRUE);
    EnableWindow(c.groupSizeCombo, TRUE);
    EnableWindow(c.groupKeepApartToggle,    TRUE);
    EnableWindow(c.groupKeepTogetherToggle, TRUE);
    EnableWindow(c.groupAvoidSameNumberCheck,   TRUE);
    EnableWindow(c.groupAvoidSamePartnersCheck, TRUE);
    EnableWindow(c.groupAvoidSameFullGroupCheck, TRUE);
    EnableWindow(c.groupResetBtn, TRUE);
    EnableWindow(c.rosterList,    FALSE);
    EnableWindow(c.autoAssign,    seats && hasRoster && hasSeats && !aaRunning);
    EnableWindow(c.quickFillSeats, seats && hasRoster && hasSeats);
    EnableWindow(c.clearAllSeats,  seats && hasAssigned);

    const bool rosterSel = c.rosterView &&
        ListView_GetNextItem(c.rosterView, -1, LVNI_SELECTED) != -1;
    EnableWindow(c.assignSelectedRoster, seats && hasFocusSeat && rosterSel);
    EnableWindow(c.bulkTag, rosterSel);

    // Arrange tab — add buttons
    EnableWindow(c.addSmartboard, layout);
    EnableWindow(c.addTrap,       layout);
    EnableWindow(c.addDesk,       layout);
    EnableWindow(c.addTable,      layout);
    EnableWindow(c.addBigTable,   layout);
    EnableWindow(c.addTrapPair,   layout);
    EnableWindow(c.addTrapPod,    layout);
    EnableWindow(c.addBlock,      layout);
    EnableWindow(c.deleteLayout,  layout && hasAny);
    EnableWindow(c.toggleVisible, layout && hasAny);
    EnableWindow(c.selectAllLayout, layout && !s.layoutItems.empty());
    EnableWindow(c.showAllObjects, layout && !s.layoutItems.empty());

    // Dynamic label: "Hide Selected" when all selected are visible, else "Show Selected"
    if (c.toggleVisible) {
        bool allVisible = true;
        if (hasAny) {
            for (int idx : s.selectedLayoutItems) {
                if (idx >= 0 && idx < static_cast<int>(s.layoutItems.size()) &&
                        !s.layoutItems[static_cast<size_t>(idx)].visible) {
                    allVisible = false;
                    break;
                }
            }
        }
        SetWindowTextW(c.toggleVisible,
            (!hasAny || allVisible) ? L"Hide Selected" : L"Show Selected");
    }
    if (c.addDeskTagRuleBtn) {
        int deskRules = 0;
        for (const auto& entry : s.studentInfo)
            deskRules += static_cast<int>(entry.second.forbiddenDesks.size());
        SetWindowTextW(c.addDeskTagRuleBtn,
            deskRules > 0 ? L"+ Desk Rule" : L"Desk Rules...");
    }

    // Transform — unlocked single or multi-select
    const BOOL canTransform = layout && hasAny && !itemLocked;
    EnableWindow(c.rotateCW,  canTransform);
    EnableWindow(c.rotateCCW, canTransform);
    EnableWindow(c.flipH,     canTransform);
    EnableWindow(c.lockItem,  layout && hasItem);

    // Duplicate + z-order (single selection)
    EnableWindow(c.duplicateLayoutItem, layout && hasItem && !itemLocked);
    EnableWindow(c.sendLayoutBack,
        layout && hasItem && *s.selectedLayoutItem > 0);
    EnableWindow(c.bringLayoutFront,
        layout && hasItem &&
        *s.selectedLayoutItem < static_cast<int>(s.layoutItems.size()) - 1);

    SendMessageW(c.showLastNamesBtn, BM_SETCHECK,
                 s.showLastNames ? BST_CHECKED : BST_UNCHECKED, 0);
    UpdateSidebarText(s, c);
}
