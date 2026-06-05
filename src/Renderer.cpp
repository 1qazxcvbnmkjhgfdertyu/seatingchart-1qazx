#include "Renderer.h"
#include "Utils.h"
#include "FileIO.h"
#include <algorithm>
#include <commdlg.h>
#include <memory>
#include <string_view>

// ---------------------------------------------------------------------------
// RAII helpers (file-local)
// ---------------------------------------------------------------------------

namespace {

struct BitmapDeleter { void operator()(HBITMAP h) const { if (h) DeleteObject(h); } };
using UniqueBitmap = std::unique_ptr<std::remove_pointer<HBITMAP>::type, BitmapDeleter>;

class ScopedSelect {
public:
    ScopedSelect(HDC dc, HGDIOBJ obj)
        : dc_(dc), old_(obj ? SelectObject(dc, obj) : nullptr) {}
    ~ScopedSelect() { if (dc_ && old_) SelectObject(dc_, old_); }
    ScopedSelect(const ScopedSelect&)            = delete;
    ScopedSelect& operator=(const ScopedSelect&) = delete;
private:
    HDC dc_ = nullptr; HGDIOBJ old_ = nullptr;
};

// Set a GDI world transform that rotates by deg degrees (clockwise on a y-down
// screen) about screen point (cx, cy). Caller must SaveDC first and RestoreDC
// afterwards. Returns false (and changes nothing) when deg == 0.
inline bool ApplyRotationTransform(HDC hdc, double deg, int cx, int cy) {
    if (deg == 0.0) return false;
    SetGraphicsMode(hdc, GM_ADVANCED);
    const double rad = deg * 3.14159265358979323846 / 180.0;
    const float  cs  = static_cast<float>(std::cos(rad));
    const float  sn  = static_cast<float>(std::sin(rad));
    XFORM xf{};
    xf.eM11 = cs;  xf.eM12 = sn;
    xf.eM21 = -sn; xf.eM22 = cs;
    xf.eDx  = static_cast<float>(cx) - (static_cast<float>(cx) * cs - static_cast<float>(cy) * sn);
    xf.eDy  = static_cast<float>(cy) - (static_cast<float>(cx) * sn + static_cast<float>(cy) * cs);
    SetWorldTransform(hdc, &xf);
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

static bool ReadRegDword(HKEY root, const wchar_t* sub,
                          const wchar_t* val, DWORD* out) {
    DWORD v = 0, sz = sizeof(v), type = 0;
    if (RegGetValueW(root, sub, val, RRF_RT_REG_DWORD, &type, &v, &sz) != ERROR_SUCCESS)
        return false;
    *out = v;
    return true;
}

void Renderer::ApplyThemeFromSystem() {
    DWORD light = 1;
    ReadRegDword(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", &light);

    if (light == 0) { // dark
        theme_ = { RGB(28,28,30), RGB(38,38,42), RGB(235,235,240), RGB(175,175,182),
                   RGB(50,50,56),  RGB(68,96,150),  RGB(92,122,190),  RGB(90,90,98),
                   RGB(110,143,220), RGB(64,72,96), RGB(92,128,200), RGB(44,44,48) };
    } else {           // light
        theme_ = { RGB(248,250,252), RGB(255,255,255), RGB(30,30,34), RGB(95,100,110),
                   RGB(245,247,250), RGB(214,232,252), RGB(189,214,255), RGB(110,120,135),
                   RGB(45,105,200), RGB(230,235,245), RGB(74,116,178), RGB(244,244,246) };
    }
    RebuildResources();
}

void Renderer::DestroyResources() {
    auto db = [](HBRUSH& b) { if (b) { DeleteObject(b); b = nullptr; } };
    auto dp = [](HPEN&   p) { if (p) { DeleteObject(p); p = nullptr; } };
    db(res_.windowBrush);  db(res_.panelBrush);  db(res_.inputBrush);
    db(res_.seatEmptyBrush); db(res_.seatOccupiedBrush); db(res_.seatSelectedBrush);
    db(res_.paperBrush);   db(res_.furnitureBrush); db(res_.furnitureSelectedBrush);
    db(res_.badgeBrush);   db(res_.lockBrush);
    dp(res_.borderPen);    dp(res_.selectedPen);
    dp(res_.furniturePen); dp(res_.furnitureSelectedPen); dp(res_.roomPen);
    dp(res_.rubberBandPen); dp(res_.gridPen);
    for (auto& entry : colorBrushCache_)
        if (entry.second) DeleteObject(entry.second);
    colorBrushCache_.clear();
}

void Renderer::DestroyBackBuffer() const {
    if (backBuffer_.dc && backBuffer_.oldBitmap)
        SelectObject(backBuffer_.dc, backBuffer_.oldBitmap);
    if (backBuffer_.bitmap) DeleteObject(backBuffer_.bitmap);
    if (backBuffer_.dc) DeleteDC(backBuffer_.dc);
    backBuffer_ = {};
}

HBRUSH Renderer::CachedColorBrush(COLORREF color) const {
    const auto it = colorBrushCache_.find(color);
    if (it != colorBrushCache_.end()) return it->second;
    HBRUSH brush = CreateSolidBrush(color);
    colorBrushCache_.emplace(color, brush);
    return brush;
}

void Renderer::RebuildResources() {
    DestroyResources();
    res_.windowBrush            = CreateSolidBrush(theme_.window);
    res_.panelBrush             = CreateSolidBrush(theme_.panel);
    res_.inputBrush             = CreateSolidBrush(theme_.window);
    res_.seatEmptyBrush         = CreateSolidBrush(theme_.seatEmpty);
    res_.seatOccupiedBrush      = CreateSolidBrush(theme_.seatOccupied);
    res_.seatSelectedBrush      = CreateSolidBrush(theme_.seatSelected);
    res_.paperBrush             = CreateSolidBrush(theme_.paper);
    res_.furnitureBrush         = CreateSolidBrush(theme_.furniture);
    res_.furnitureSelectedBrush = CreateSolidBrush(theme_.furnitureSelected);
    res_.badgeBrush             = CreateSolidBrush(theme_.accent);
    res_.lockBrush              = CreateSolidBrush(RGB(210, 80, 60));
    res_.borderPen              = CreatePen(PS_SOLID, 1, theme_.border);
    res_.selectedPen            = CreatePen(PS_SOLID, std::max(2, Scale(2)), theme_.accent);
    res_.furniturePen           = CreatePen(PS_SOLID, 1, theme_.accent);
    res_.furnitureSelectedPen   = CreatePen(PS_SOLID, std::max(2, Scale(2)), theme_.accent);
    res_.roomPen                = CreatePen(PS_SOLID, 2, theme_.border);
    res_.rubberBandPen          = CreatePen(PS_DASH,  1, theme_.accent);
    // Subtle grid: 25% blend of border toward paper
    const COLORREF gc = RGB(
        (GetRValue(theme_.border) * 1 + GetRValue(theme_.paper) * 3) / 4,
        (GetGValue(theme_.border) * 1 + GetGValue(theme_.paper) * 3) / 4,
        (GetBValue(theme_.border) * 1 + GetBValue(theme_.paper) * 3) / 4);
    res_.gridPen = CreatePen(PS_SOLID, 1, gc);
}

static HFONT MakeBalooFont(int pts, UINT dpi, int weight = FW_NORMAL) {
    const int h = -MulDiv(pts, static_cast<int>(dpi), 72);
    return CreateFontW(h, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Baloo 2");
}

void Renderer::DestroyFonts() {
    auto df = [](HFONT& f) { if (f) { DeleteObject(f); f = nullptr; } };
    df(uiFont_); df(titleFont_); df(sectionFont_);
}

void Renderer::RebuildFonts(UINT dpi) {
    DestroyFonts();
    uiFont_      = MakeBalooFont(9,  dpi);
    titleFont_   = MakeBalooFont(15, dpi, FW_SEMIBOLD);
    sectionFont_ = MakeBalooFont(9,  dpi, FW_SEMIBOLD);
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

void Renderer::DrawTextCentered(HDC hdc, const RECT& rc,
                                  std::wstring_view text, COLORREF color) {
    const int bk = SetBkMode(hdc, TRANSPARENT);
    const COLORREF oc = SetTextColor(hdc, color);
    RECT r = rc;
    DrawTextW(hdc, text.data(), static_cast<int>(text.size()), &r,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SetTextColor(hdc, oc);
    SetBkMode(hdc, bk);
}

std::wstring Renderer::SeatInitials(std::wstring_view name) {
    if (name.empty()) return {};
    std::wstring init; bool take = true;
    for (wchar_t c : name) {
        if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') { take = true; continue; }
        if (take) { init += static_cast<wchar_t>(towupper(c)); take = false; }
    }
    if (init.empty()) init += static_cast<wchar_t>(towupper(name.front()));
    if (init.size() > 2) init.resize(2);
    return init;
}

// ---------------------------------------------------------------------------
// DrawRoomGrid — light grid overlay inside the room boundary
// ---------------------------------------------------------------------------

void Renderer::DrawRoomGrid(HDC hdc, const LayoutViewTransform& tx) const {
    if (!res_.gridPen) return;
    constexpr int kGridStep = 100; // room-local units between grid lines
    HGDIOBJ op = SelectObject(hdc, res_.gridPen);

    // Clip drawing to room rect so lines don't bleed outside
    HRGN clip = CreateRectRgn(tx.roomScreenRect.left, tx.roomScreenRect.top,
                               tx.roomScreenRect.right, tx.roomScreenRect.bottom);
    SelectClipRgn(hdc, clip);

    for (int x = kGridStep; x < tx.roomW; x += kGridStep) {
        const int sx = tx.roomScreenRect.left + static_cast<int>(x * tx.scale);
        MoveToEx(hdc, sx, tx.roomScreenRect.top,    nullptr);
        LineTo  (hdc, sx, tx.roomScreenRect.bottom);
    }
    for (int y = kGridStep; y < tx.roomH; y += kGridStep) {
        const int sy = tx.roomScreenRect.top + static_cast<int>(y * tx.scale);
        MoveToEx(hdc, tx.roomScreenRect.left,  sy, nullptr);
        LineTo  (hdc, tx.roomScreenRect.right, sy);
    }

    SelectClipRgn(hdc, nullptr);
    DeleteObject(clip);
    SelectObject(hdc, op);
}

// ---------------------------------------------------------------------------
// DrawFrontIndicator — accent strip + "FRONT" label along the front edge so a
// room's orientation is unambiguous (and matches the front-row seat ranking).
// ---------------------------------------------------------------------------

void Renderer::DrawFrontIndicator(HDC hdc, const LayoutViewTransform& tx,
                                  RoomEdge front) const {
    const RECT& room = tx.roomScreenRect;
    if (room.right - room.left < 8 || room.bottom - room.top < 8) return;

    const bool vertical = (front == RoomEdge::Left || front == RoomEdge::Right);
    const int  band = vertical ? std::max(Scale(22), 18) : std::max(Scale(20), 16);

    RECT strip = room;
    switch (front) {
    case RoomEdge::Top:    strip.bottom = room.top    + band; break;
    case RoomEdge::Bottom: strip.top    = room.bottom - band; break;
    case RoomEdge::Left:   strip.right  = room.left   + band; break;
    case RoomEdge::Right:  strip.left   = room.right  - band; break;
    }

    HBRUSH stripBrush = CreateSolidBrush(theme_.accent);
    FillRect(hdc, &strip, stripBrush);
    DeleteObject(stripBrush);

    const COLORREF prevColor = SetTextColor(hdc, RGB(255, 255, 255));
    const int      prevBk    = SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ        prevFont   = SelectObject(hdc, sectionFont_ ? sectionFont_ : uiFont_);

    if (!vertical) {
        RECT lr = strip;
        DrawTextW(hdc, L"FRONT", 5, &lr,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    } else {
        // Stack the letters down the centre of the vertical strip.
        const wchar_t* word = L"FRONT";
        const int n  = 5;
        const int cx = (strip.left + strip.right) / 2;
        TEXTMETRICW tm{}; GetTextMetricsW(hdc, &tm);
        const int lh     = std::max(1L, tm.tmHeight);
        const int startY = (room.top + room.bottom) / 2 - (n * lh) / 2;
        const UINT prevAlign = SetTextAlign(hdc, TA_CENTER | TA_TOP);
        for (int i = 0; i < n; ++i)
            TextOutW(hdc, cx, startY + i * lh, &word[i], 1);
        SetTextAlign(hdc, prevAlign);
    }

    SelectObject(hdc, prevFont);
    SetBkMode(hdc, prevBk);
    SetTextColor(hdc, prevColor);
}

// ---------------------------------------------------------------------------
// DrawKeepApartRings — when a furniture seat is focused, draw the keep-apart
// radius of its occupant's distance-restrictions as a dashed ring around the
// seat (the "circle" inside which the paired student may not sit).
// ---------------------------------------------------------------------------

void Renderer::DrawKeepApartRings(HDC hdc, const AppState& state,
                                  const LayoutViewTransform& tx) const {
    if (!state.selectedLayoutSeat) return;
    const int it = state.selectedLayoutSeat->first;
    const int sl = state.selectedLayoutSeat->second;
    if (it < 0 || it >= static_cast<int>(state.layoutItems.size())) return;
    const LayoutItem& item = state.layoutItems[static_cast<size_t>(it)];
    if (sl < 0 || sl >= static_cast<int>(item.occupants.size())) return;
    const std::wstring& occ = item.occupants[static_cast<size_t>(sl)];
    if (occ.empty()) return;
    const std::wstring occCanon = CanonicalName(occ);

    const auto centres = LayoutSeatSlotScreenCenters(item, tx);
    if (sl >= static_cast<int>(centres.size())) return;
    const POINT c = centres[static_cast<size_t>(sl)];

    HPEN ringPen = CreatePen(PS_DASH, 1, RGB(214, 69, 69));
    HGDIOBJ oldPen   = SelectObject(hdc, ringPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    const int      prevBk = SetBkMode(hdc, TRANSPARENT);
    const COLORREF prevTx = SetTextColor(hdc, RGB(214, 69, 69));

    for (const auto& r : state.restrictions) {
        if (r.radius <= 0) continue;
        std::wstring other;
        if (CanonicalName(r.first)  == occCanon) other = r.second;
        else if (CanonicalName(r.second) == occCanon) other = r.first;
        else continue;
        const int rr = static_cast<int>(r.radius * tx.scale);
        if (rr <= 1) continue;
        Ellipse(hdc, c.x - rr, c.y - rr, c.x + rr, c.y + rr);
        if (!other.empty()) {
            RECT lbl{ c.x - rr, c.y - rr - Scale(15), c.x + rr, c.y - rr - Scale(1) };
            const std::wstring disp = DisplayStudentName(other, state.showLastNames);
            DrawTextW(hdc, disp.c_str(), -1, &lbl,
                      DT_CENTER | DT_SINGLELINE | DT_NOCLIP | DT_BOTTOM);
        }
    }

    SetTextColor(hdc, prevTx);
    SetBkMode(hdc, prevBk);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(ringPen);
}

void Renderer::PaintStudentDragPreview(HDC hdc, const AppState& state,
                                       const LayoutViewTransform& tx,
                                       const DragPreviewState& preview) const {
    if (!preview.active || preview.studentName.empty()) return;

    auto drawGhostText = [&](RECT textRect, COLORREF textColor) {
        const int prevBk = SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ oldFont = SelectObject(hdc, sectionFont_ ? sectionFont_ : uiFont_);
        RECT shadow = OffsetRectCopy(textRect, Scale(1), Scale(1));
        const std::wstring disp = DisplayStudentName(preview.studentName, state.showLastNames);
        SetTextColor(hdc, RGB(255, 255, 255));
        DrawTextW(hdc, disp.c_str(), -1, &shadow,
                  DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS | DT_EDITCONTROL);
        SetTextColor(hdc, textColor);
        DrawTextW(hdc, disp.c_str(), -1, &textRect,
                  DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS | DT_EDITCONTROL);
        SelectObject(hdc, oldFont);
        SetBkMode(hdc, prevBk);
    };

    if (!preview.targetSeat) {
        if (!preview.overChart) return;
        const int w = std::max(Scale(96), Scale(18) * static_cast<int>(std::min<size_t>(preview.studentName.size(), 14)));
        const int h = Scale(32);
        RECT box{ preview.cursorPt.x + Scale(14), preview.cursorPt.y + Scale(14),
                  preview.cursorPt.x + Scale(14) + w, preview.cursorPt.y + Scale(14) + h };
        if (box.right > tx.chartBounds.right - Scale(4)) {
            OffsetRect(&box, -(box.right - (tx.chartBounds.right - Scale(4))), 0);
        }
        if (box.bottom > tx.chartBounds.bottom - Scale(4)) {
            OffsetRect(&box, 0, -(box.bottom - (tx.chartBounds.bottom - Scale(4))));
        }

        HBRUSH brush = CreateSolidBrush(RGB(250, 250, 250));
        HPEN pen = CreatePen(PS_DASH, 1, theme_.accent);
        HGDIOBJ oldBrush = SelectObject(hdc, brush);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        RoundRect(hdc, box.left, box.top, box.right, box.bottom, Scale(8), Scale(8));
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);

        RECT tr{ box.left + Scale(6), box.top + Scale(3),
                 box.right - Scale(6), box.bottom - Scale(3) };
        drawGhostText(tr, theme_.text);
        return;
    }

    const int itemIndex = preview.seat.first;
    const int slotIndex = preview.seat.second;
    if (itemIndex < 0 || itemIndex >= static_cast<int>(state.layoutItems.size())) return;
    const LayoutItem& item = state.layoutItems[static_cast<size_t>(itemIndex)];
    const auto slots = LayoutSeatSlots(item);
    if (slotIndex < 0 || slotIndex >= static_cast<int>(slots.size()) ||
        slotIndex >= static_cast<int>(item.occupants.size())) return;

    const RECT itemScreen = RoomToScreenRect(item.bounds, tx);
    const RECT slotScreen = RoomToScreenRect(slots[static_cast<size_t>(slotIndex)], tx);
    const POINT itemCenter{ (itemScreen.left + itemScreen.right) / 2,
                            (itemScreen.top + itemScreen.bottom) / 2 };
    const POINT rawCenter{ (slotScreen.left + slotScreen.right) / 2,
                           (slotScreen.top + slotScreen.bottom) / 2 };
    const POINT ctr = RotatePointAround(rawCenter, itemCenter, static_cast<double>(item.rotation));

    const int bw = std::clamp(static_cast<int>(slotScreen.right - slotScreen.left) - Scale(4), Scale(46), Scale(170));
    const int bh = std::clamp(static_cast<int>(slotScreen.bottom - slotScreen.top) - Scale(4), Scale(24), Scale(74));
    const RECT box{ ctr.x - bw / 2, ctr.y - bh / 2, ctr.x + bw / 2, ctr.y + bh / 2 };

    const bool locked = item.locked;
    const bool occupied = !item.occupants[static_cast<size_t>(slotIndex)].empty();
    COLORREF fill = RGB(211, 247, 222);
    COLORREF border = RGB(36, 135, 70);
    COLORREF text = RGB(18, 90, 46);
    if (!preview.valid) {
        if (locked) {
            fill = RGB(255, 221, 216);
            border = RGB(190, 58, 48);
            text = RGB(135, 35, 28);
        } else if (occupied) {
            fill = RGB(255, 238, 203);
            border = RGB(183, 115, 32);
            text = RGB(112, 74, 18);
        } else {
            fill = RGB(255, 226, 226);
            border = RGB(180, 65, 65);
            text = RGB(120, 38, 38);
        }
    }

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_DASH, std::max(1, Scale(2)), border);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, box.left, box.top, box.right, box.bottom, Scale(8), Scale(8));
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);

    RECT tr{ box.left + Scale(4), box.top + Scale(2),
             box.right - Scale(4), box.bottom - Scale(2) };
    drawGhostText(tr, text);
}

// ---------------------------------------------------------------------------
// DrawLayoutItemShape — renders one item's shape into screenBounds.
// screenBounds is already in screen/DC coordinates.
// ---------------------------------------------------------------------------

void Renderer::DrawLayoutItemShape(HDC hdc, const LayoutItem& item,
                                    const RECT& rc, bool sel) const {
    const int L = rc.left, R = rc.right, T = rc.top, B = rc.bottom;
    // Trapezoid slant proportional to height (≈ a real half-hexagon desk), so
    // the top edge is clearly shorter than the bottom and pods tessellate.
    const int slant = std::min((R - L) / 3, std::max(6, (B - T) / 2));

    HBRUSH brush = sel ? res_.furnitureSelectedBrush : res_.furnitureBrush;
    HPEN   pen   = sel ? res_.furnitureSelectedPen   : res_.furniturePen;
    HGDIOBJ obr = SelectObject(hdc, brush);
    HGDIOBJ op  = SelectObject(hdc, pen);

    switch (item.type) {
    case LayoutItemType::Smartboard:
        Rectangle(hdc, L, T, R, B);
        break;

    case LayoutItemType::RectangleDesk:
        RoundRect(hdc, L, T, R, B, 6, 6);
        break;

    case LayoutItemType::TrapezoidDesk: {
        // Real-classroom shape (from reference photos): a fan/shield desk.
        // The BACK (wide) edge has clearly rounded corners; the FRONT (narrow)
        // angled sides meet at softer corners. We approximate with a 6-point
        // cut-corner polygon — better than sharp-corner Polygon() and fits
        // within standard Win32 GDI without needing BeginPath/ArcTo.
        //
        // Base orientation: wide edge at BOTTOM (back of desk), narrow at TOP
        // (front, pointing toward the centre of any pod arrangement).
        // Flip inverts this so wide edge is at TOP.
        const int cr = std::max(Scale(3), std::min((R - L) / 7, (B - T) / 5));
        POINT pts[6];
        if (!item.flipped) {
            // Wide back at bottom, narrow front at top
            pts[0] = { L + slant, T       };   // top-left  (angled front corner)
            pts[1] = { R - slant, T       };   // top-right (angled front corner)
            pts[2] = { R,         B - cr  };   // right side → bottom-right
            pts[3] = { R - cr,    B       };   // bottom-right cut (rounded)
            pts[4] = { L + cr,    B       };   // bottom-left cut  (rounded)
            pts[5] = { L,         B - cr  };   // left side  → bottom-left
        } else {
            // Flipped: wide back at top, narrow front at bottom
            pts[0] = { L + cr,    T       };
            pts[1] = { R - cr,    T       };
            pts[2] = { R,         T + cr  };
            pts[3] = { R - slant, B       };
            pts[4] = { L + slant, B       };
            pts[5] = { L,         T + cr  };
        }
        Polygon(hdc, pts, 6);
        break;
    }

    case LayoutItemType::Table4:
        RoundRect(hdc, L, T, R, B, 14, 14);
        SelectObject(hdc, res_.furniturePen);
        MoveToEx(hdc,(L+R)/2,T,nullptr); LineTo(hdc,(L+R)/2,B);
        MoveToEx(hdc,L,(T+B)/2,nullptr); LineTo(hdc,R,(T+B)/2);
        break;

    case LayoutItemType::BigTable: {
        RoundRect(hdc, L, T, R, B, 18, 18);
        const int cap = item.capacity > 0 ? item.capacity : LayoutItemDefaultCapacity(item.type);
        if (cap >= 2) {
            const int W = R - L, H = B - T;
            const int perim  = 2 * (W + H);
            const int bw = Scale(8), bh = Scale(5);
            const int topSeats    = std::max(0, (cap * W) / perim);
            const int bottomSeats = topSeats;
            const int sideSeats   = std::max(0, (cap - topSeats - bottomSeats) / 2);
            const int leftSeats   = sideSeats;
            const int rightSeats  = cap - topSeats - bottomSeats - leftSeats;

            auto drawBumps = [&](int count, bool horiz, int edgeX, int edgeY,
                                  int span, bool flipDir) {
                if (count <= 0) return;
                const int step = span / (count + 1);
                for (int k = 1; k <= count; ++k) {
                    RECT br{};
                    if (horiz) {
                        const int cx = edgeX + k * step;
                        br = { cx-bw/2, flipDir ? edgeY-bh : edgeY,
                               cx+bw/2, flipDir ? edgeY    : edgeY+bh };
                    } else {
                        const int cy = edgeY + k * step;
                        br = { flipDir ? edgeX-bh : edgeX, cy-bw/2,
                               flipDir ? edgeX    : edgeX+bh, cy+bw/2 };
                    }
                    HGDIOBJ ob2 = SelectObject(hdc, brush);
                    RoundRect(hdc, br.left, br.top, br.right, br.bottom, 2, 2);
                    SelectObject(hdc, ob2);
                }
            };
            drawBumps(topSeats,    true,  L, T, W, true);
            drawBumps(bottomSeats, true,  L, B, W, false);
            drawBumps(leftSeats,   false, L, T, H, true);
            drawBumps(rightSeats,  false, R, T, H, false);
        }
        // Capacity label is drawn unrotated by the caller (see PaintChart) so it
        // stays readable and consistent with other furniture labels.
        break;
    }

    case LayoutItemType::TrapPair: {
        // Two trapezoid desks pushed together with their NARROW ends meeting at
        // the centre seam — forming a bowtie / butterfly shape.
        //
        // Reference photos confirm: the outer edges (top & bottom) are the WIDE
        // ends of each individual desk; the shared centre seam is the NARROW end.
        // The previous rendering had this backwards (barrel/hexagon wider in the
        // middle), which is now corrected.
        //
        // Geometry:
        //   Top desk:    wide outer top  →  narrow centre seam
        //   Bottom desk: narrow centre seam  →  wide outer bottom
        //
        //   ┌────────────────────┐  ← top (full width)
        //    \                  /
        //     ──────────────────    ← centre seam (inset sx on each side)
        //    /                  \
        //   └────────────────────┘  ← bottom (full width)
        const int W = R - L, H = B - T, midY = (T + B) / 2;
        // sx: how far the seam is inset from each side. ≈ 22% of W gives a
        // seam ≈ 56% as wide as the outer edge, which matches the photos.
        const int sx = W * 22 / 100;
        const POINT TL{L,    T   }, TR{R,    T   };   // wide outer top
        const POINT ML{L+sx, midY}, MR{R-sx, midY};  // narrow centre seam
        const POINT BL{L,    B   }, BR{R,    B   };   // wide outer bottom
        POINT top[4]    = { TL, TR, MR, ML };
        POINT bottom[4] = { ML, MR, BR, BL };
        Polygon(hdc, top,    4);
        Polygon(hdc, bottom, 4);
        // Explicit seam line (the shared edge drawn by Polygon borders is
        // sometimes anti-aliased away at small scales; draw it explicitly).
        SelectObject(hdc, pen);
        MoveToEx(hdc, ML.x, ML.y, nullptr);
        LineTo(hdc,   MR.x, MR.y);
        break;
    }

    case LayoutItemType::TrapPod: {
        // Four fan/trapezoid desks arranged in a cross-pod (N, S, W, E) with
        // all narrow ends meeting at the centre — matching the classroom photos.
        //
        // Each desk is wide at the outer edge and narrow at the shared centre.
        // The centre is a true point (or very small gap), not a wide seam.
        //
        // Coordinate fractions (x, y) in [0,1] relative to the bounding rect:
        //
        //        ┌──── TL ────┬──── TR ────┐   y = 0.08 (outer top)
        //         \     tA  tB     /
        //          \  pNL──pNR  /
        //           ╲   pCentre  ╱
        //           /   ╲   /   \
        //          /  pWR──pER   \
        //         /    lA  rB     \
        //        └──── BL ────┴──── BR ────┘   y = 0.92 (outer bottom)
        //
        // North desk (top):  TL → TR → pNR → pCentre → pNL
        // South desk (bot):  BL → BR → pSR → pCentre → pSL   (same centre point)
        // West desk (left):  TL → pNL → pCentre → pSL → BL
        // East desk (right): TR → pNR → pCentre → pSR → BR

        const int W = R - L;
        const int H = B - T;
        auto px = [&](double x) -> LONG { return static_cast<LONG>(std::lround(L + x * W)); };
        auto py = [&](double y) -> LONG { return static_cast<LONG>(std::lround(T + y * H)); };

        // Outer corners — wide back edges of the four desks.
        const POINT TL = { px(0.10), py(0.08) }, TR = { px(0.90), py(0.08) };
        const POINT BL = { px(0.10), py(0.92) }, BR = { px(0.90), py(0.92) };

        // Centre point — where all four narrow tips converge.
        const POINT PC = { px(0.50), py(0.50) };

        // Inner seam corners for each desk (the narrow edge adjacent to centre).
        // North-desk inner corners sit just above centre.
        const POINT pNL = { px(0.32), py(0.32) };  // north-desk inner-left
        const POINT pNR = { px(0.68), py(0.32) };  // north-desk inner-right
        // South-desk inner corners just below centre.
        const POINT pSL = { px(0.32), py(0.68) };  // south-desk inner-left
        const POINT pSR = { px(0.68), py(0.68) };  // south-desk inner-right

        // Each desk: 5-point polygon (outer-left, outer-right, inner-right,
        //            centre-tip, inner-left) — fan shape matching the photos.
        POINT northDesk[5] = { TL,  TR,  pNR, PC,  pNL };
        POINT southDesk[5] = { BL,  BR,  pSR, PC,  pSL };
        POINT westDesk[5]  = { TL,  pNL, PC,  pSL, BL  };
        POINT eastDesk[5]  = { TR,  pNR, PC,  pSR, BR  };

        Polygon(hdc, northDesk, 5);
        Polygon(hdc, southDesk, 5);
        Polygon(hdc, westDesk,  5);
        Polygon(hdc, eastDesk,  5);

        // Seam lines — connect inner corners through the centre.
        SelectObject(hdc, pen);
        MoveToEx(hdc, pNL.x, pNL.y, nullptr); LineTo(hdc, PC.x, PC.y);
        MoveToEx(hdc, pNR.x, pNR.y, nullptr); LineTo(hdc, PC.x, PC.y);
        MoveToEx(hdc, pSL.x, pSL.y, nullptr); LineTo(hdc, PC.x, PC.y);
        MoveToEx(hdc, pSR.x, pSR.y, nullptr); LineTo(hdc, PC.x, PC.y);

        break;
    }
    } // end switch

    SelectObject(hdc, op);
    SelectObject(hdc, obr);
}

// ---------------------------------------------------------------------------
// PaintChart
// ---------------------------------------------------------------------------

void Renderer::PaintChart(HDC hdc, const AppState& state,
                            const RECT& chartBounds, HoverState hover,
                            RECT rubberBand,
                            const DragPreviewState& dragPreview) const {
    HFONT font = uiFont_ ? uiFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    ScopedSelect fontSel(hdc, static_cast<HGDIOBJ>(font));
    HGDIOBJ oldPen = SelectObject(hdc, res_.borderPen);

    {
        // ------------------------------------------------------------------
        // Unified chart — always the furniture room (rows×cols grid retired).
        // Each furniture item owns its seats; occupants render as initials.
        // chartMode only selects the interaction tool (Arrange vs Assign).
        // room-local → screen transform
        // ------------------------------------------------------------------
        const LayoutViewTransform tx =
            ComputeLayoutViewTransform(chartBounds, state.roomW, state.roomH);

        // Room background
        {
            HGDIOBJ ob = SelectObject(hdc, res_.paperBrush);
            HGDIOBJ op = SelectObject(hdc, res_.roomPen);
            Rectangle(hdc,
                      tx.roomScreenRect.left,  tx.roomScreenRect.top,
                      tx.roomScreenRect.right, tx.roomScreenRect.bottom);
            SelectObject(hdc, op); SelectObject(hdc, ob);
        }

        // Optional grid overlay
        DrawRoomGrid(hdc, tx);

        // Front-edge banner (room orientation).
        DrawFrontIndicator(hdc, tx, state.frontEdge);

        // Empty-state hint — shown only when there is no furniture so a
        // teacher opening the app for the first time gets a clear next step
        // rather than a blank canvas.
        // (Deep Research Report: "actionability — help teachers move from
        //  awareness to action"; ISTE 1.1 Value Proposition.)
        if (state.layoutItems.empty()) {
            const wchar_t* hint =
                (state.chartMode == ChartMode::Layout)
                ? L"Use “Add furniture” in the sidebar to build your room layout\n"
                  L"Then switch to Assign to place students"
                : L"Switch to  Arrange  mode (top of sidebar)\n"
                  L"to add desks, tables, and chairs";

            RECT msgRect = tx.roomScreenRect;
            // Vertically center a text block in the middle third of the room.
            const int cx = (msgRect.left + msgRect.right) / 2;
            const int cy = (msgRect.top  + msgRect.bottom) / 2;
            const int hw = (msgRect.right - msgRect.left) / 3;  // half-width of text box
            RECT textRc{ cx - hw, cy - Scale(28), cx + hw, cy + Scale(28) };

            const int oldBk     = SetBkMode(hdc, TRANSPARENT);
            const COLORREF oldC = SetTextColor(hdc, theme_.mutedText);
            DrawTextW(hdc, hint, -1, &textRc,
                      DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
            SetTextColor(hdc, oldC);
            SetBkMode(hdc, oldBk);
        }

        // Furniture items (back-to-front)
        for (size_t i = 0; i < state.layoutItems.size(); ++i) {
            const LayoutItem& item = state.layoutItems[i];
            if (!item.visible) continue;

            // Convert room-local bounds to screen bounds for this render target.
            const RECT sr = RoomToScreenRect(item.bounds, tx);

            const bool inMultiSel = std::find(state.selectedLayoutItems.begin(),
                                              state.selectedLayoutItems.end(),
                                              static_cast<int>(i))
                                    != state.selectedLayoutItems.end();
            const bool isHover = (static_cast<int>(i) == hover.layoutItem);
            const bool sel = item.selected || inMultiSel || isHover;

            // Draw the shape, rotated about its centre via a world transform.
            {
                const int saved = SaveDC(hdc);
                const POINT c{ (sr.left + sr.right) / 2, (sr.top + sr.bottom) / 2 };
                ApplyRotationTransform(hdc, static_cast<double>(item.rotation), c.x, c.y);
                DrawLayoutItemShape(hdc, item, sr, sel);
                RestoreDC(hdc, saved);
            }

            // Seat occupants (rotation-aware centres, unrotated initials).
            const auto slots = LayoutSeatSlots(item);
            std::vector<POINT> seatCenters;
            seatCenters.reserve(slots.size());
            const POINT itemCenter{ (sr.left + sr.right) / 2, (sr.top + sr.bottom) / 2 };
            for (const RECT& slot : slots) {
                const RECT ssr = RoomToScreenRect(slot, tx);
                const POINT center{ (ssr.left + ssr.right) / 2,
                                    (ssr.top + ssr.bottom) / 2 };
                seatCenters.push_back(RotatePointAround(center, itemCenter,
                                                        static_cast<double>(item.rotation)));
            }
            const int  seatCount   = static_cast<int>(item.occupants.size());
            const bool singleSeatDesk =
                (item.type == LayoutItemType::RectangleDesk ||
                 item.type == LayoutItemType::TrapezoidDesk);
            bool anyOccupied = false;
            for (const auto& o : item.occupants) if (!o.empty()) { anyOccupied = true; break; }

            // Labels are kept unrotated and centred over the item. A single-seat
            // desk shows its occupant instead of the type label when filled.
            const bool isPod = (item.type == LayoutItemType::TrapPair ||
                                item.type == LayoutItemType::TrapPod);
            if (item.type == LayoutItemType::BigTable) {
                const int cap = item.capacity > 0 ? item.capacity
                                                  : LayoutItemDefaultCapacity(item.type);
                DrawTextCentered(hdc, sr, std::to_wstring(cap) + L" seats", theme_.text);
            } else if (!isPod && !(seatCount > 0 && anyOccupied)) {
                // Seating items hand the centre over to occupant names once filled.
                DrawTextCentered(hdc, sr, item.label, theme_.text);
            }
            (void)singleSeatDesk;

            // Seat occupants — full name centred on each seat (wrapped to 2 lines
            // with ellipsis on overflow) on a rounded pill for legibility. Empty
            // seats show a faint marker so they read as assignable.
            {
                ScopedSelect nameFont(hdc, static_cast<HGDIOBJ>(uiFont_ ? uiFont_ : font));
                for (int s = 0; s < seatCount &&
                                 s < static_cast<int>(seatCenters.size()); ++s) {
                    const POINT ctr = seatCenters[static_cast<size_t>(s)];
                    const std::wstring& occ = item.occupants[static_cast<size_t>(s)];
                    const std::wstring disp = DisplayStudentName(occ, state.showLastNames);
                    const bool focused = state.selectedLayoutSeat &&
                        state.selectedLayoutSeat->first  == static_cast<int>(i) &&
                        state.selectedLayoutSeat->second == s;

                    // Label box sized to the seat slot (with sensible fallback).
                    int bw = Scale(64), bh = Scale(34);
                    if (s < static_cast<int>(slots.size())) {
                        const RECT ssr = RoomToScreenRect(slots[static_cast<size_t>(s)], tx);
                        bw = std::clamp(static_cast<int>(ssr.right  - ssr.left) - Scale(4), Scale(46), Scale(160));
                        bh = std::clamp(static_cast<int>(ssr.bottom - ssr.top)  - Scale(4), Scale(22), Scale(70));
                    }
                    const RECT box{ ctr.x - bw/2, ctr.y - bh/2, ctr.x + bw/2, ctr.y + bh/2 };

                    if (occ.empty()) {
                        const int rr = std::max(3, Scale(3));
                        HGDIOBJ ob = SelectObject(hdc, res_.panelBrush);
                        HGDIOBJ op = SelectObject(hdc, res_.borderPen);
                        Ellipse(hdc, ctr.x-rr, ctr.y-rr, ctr.x+rr, ctr.y+rr);
                        if (focused) {
                            SelectObject(hdc, GetStockObject(NULL_BRUSH));
                            SelectObject(hdc, res_.selectedPen);
                            RoundRect(hdc, box.left, box.top, box.right, box.bottom, Scale(6), Scale(6));
                        }
                        SelectObject(hdc, op); SelectObject(hdc, ob);
                        continue;
                    }

                    // Pill background — use the student's group colour if set,
                    // otherwise the default occupied colour. Border highlights focus.
                    const uint32_t scol = state.StudentColor(occ);
                    HBRUSH colBrush = nullptr;
                    if (scol != 0) colBrush = CachedColorBrush(static_cast<COLORREF>(scol & 0x00FFFFFF));
                    HGDIOBJ ob = SelectObject(hdc, colBrush ? colBrush : res_.seatOccupiedBrush);
                    HGDIOBJ op = SelectObject(hdc, focused ? res_.selectedPen : res_.borderPen);
                    RoundRect(hdc, box.left, box.top, box.right, box.bottom, Scale(6), Scale(6));
                    SelectObject(hdc, op); SelectObject(hdc, ob);

                    // Pick a readable text colour for the pill (dark on light fills).
                    COLORREF txt = theme_.text;
                    if (scol != 0) {
                        const int rr = (scol >> 16) & 0xFF, gg = (scol >> 8) & 0xFF, bb = scol & 0xFF;
                        const int lum = (rr*299 + gg*587 + bb*114) / 1000;
                        txt = (lum < 140) ? RGB(255,255,255) : RGB(20,20,20);
                    }

                    // Full name: wrap within the box, then vertically centre it.
                    RECT tr{ box.left + Scale(3), box.top + Scale(2),
                             box.right - Scale(3), box.bottom - Scale(2) };
                    RECT calc = tr;
                    DrawTextW(hdc, disp.c_str(), -1, &calc,
                              DT_CENTER | DT_WORDBREAK | DT_CALCRECT | DT_EDITCONTROL);
                    const int avail = tr.bottom - tr.top;
                    const int th = std::min<int>(calc.bottom - calc.top, avail);
                    RECT draw{ tr.left, tr.top + (avail - th) / 2, tr.right, tr.top + (avail - th) / 2 + th };
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, txt);
                    DrawTextW(hdc, disp.c_str(), -1, &draw,
                              DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS | DT_EDITCONTROL);
                }
            }

            // Selection handles — only for the single focused item
            if (item.selected && state.selectedLayoutItems.size() == 1) {
                const auto g = ComputeLayoutHandleGeometry(item.bounds, item.rotation, tx);
                if (!item.locked) {
                    // Rotation handle: connector line + circular grip above top centre.
                    HGDIOBJ olp = SelectObject(hdc, res_.selectedPen);
                    MoveToEx(hdc, g.topCenter.x, g.topCenter.y, nullptr);
                    LineTo  (hdc, g.rotate.x,    g.rotate.y);
                    HGDIOBJ olb = SelectObject(hdc, res_.badgeBrush);
                    const int rr = std::max(4, Scale(5));
                    Ellipse(hdc, g.rotate.x-rr, g.rotate.y-rr, g.rotate.x+rr, g.rotate.y+rr);
                    // Corner resize handles.
                    const int hz = std::max(10, Scale(10));
                    for (const POINT& p : g.corners)
                        Rectangle(hdc, p.x-hz/2, p.y-hz/2, p.x+hz/2, p.y+hz/2);
                    SelectObject(hdc, olb); SelectObject(hdc, olp);
                } else {
                    // Locked: just outline the (rotated) footprint.
                    HGDIOBJ olp = SelectObject(hdc, res_.selectedPen);
                    HGDIOBJ olb = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                    const POINT poly[4]{ g.corners[0], g.corners[1], g.corners[3], g.corners[2] };
                    Polygon(hdc, poly, 4);
                    SelectObject(hdc, olb); SelectObject(hdc, olp);
                }
            }
            if (inMultiSel && state.selectedLayoutItems.size() > 1) {
                // Thicker dashed outline for every multi-selected item.
                const auto g = ComputeLayoutHandleGeometry(item.bounds, item.rotation, tx);
                HPEN multiPen = CreatePen(PS_DASH, std::max(2, Scale(2)), theme_.accent);
                HGDIOBJ ohp = SelectObject(hdc, multiPen);
                HGDIOBJ ohb = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                const POINT poly[4]{ g.corners[0], g.corners[1], g.corners[3], g.corners[2] };
                Polygon(hdc, poly, 4);
                SelectObject(hdc, ohb); SelectObject(hdc, ohp);
                DeleteObject(multiPen);
            }

            // Lock indicator (red "L" badge top-right)
            if (item.locked) {
                const int bsz = Scale(14);
                const RECT lk{ sr.right - bsz - Scale(2), sr.top + Scale(2),
                               sr.right - Scale(2), sr.top + Scale(2) + bsz };
                HGDIOBJ olb = SelectObject(hdc, res_.lockBrush);
                HGDIOBJ olp = SelectObject(hdc, res_.borderPen);
                Rectangle(hdc, lk.left, lk.top, lk.right, lk.bottom);
                SelectObject(hdc, olp); SelectObject(hdc, olb);
                DrawTextCentered(hdc, lk, L"L", RGB(255,255,255));
            }

            // Rotation indicator (small text top-left when non-zero)
            if (item.rotation != 0) {
                const std::wstring rotLabel = std::to_wstring(item.rotation) + L"°";
                RECT rl{ sr.left + Scale(2), sr.top + Scale(2),
                         sr.left + Scale(30), sr.top + Scale(14) };
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, theme_.mutedText);
                DrawTextW(hdc, rotLabel.c_str(), static_cast<int>(rotLabel.size()),
                          &rl, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
        }

        // Keep-apart radius rings for the focused seat's occupant.
        DrawKeepApartRings(hdc, state, tx);
        PaintStudentDragPreview(hdc, state, tx, dragPreview);

        // Rubber-band selection rectangle (screen coords)
        if (rubberBand.right > rubberBand.left && rubberBand.bottom > rubberBand.top) {
            HGDIOBJ op = SelectObject(hdc, res_.rubberBandPen);
            HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, rubberBand.left, rubberBand.top,
                           rubberBand.right, rubberBand.bottom);
            SelectObject(hdc, ob); SelectObject(hdc, op);
        }
    }
    SelectObject(hdc, oldPen);
}

// ---------------------------------------------------------------------------
// PaintWindowBuffered
// ---------------------------------------------------------------------------

void Renderer::PaintWindowBuffered(HWND hwnd, HDC hdc, const AppState& state,
                                    const RECT& chartBounds, HoverState hover,
                                    RECT rubberBand,
                                    const DragPreviewState& dragPreview) const {
    RECT client{}; GetClientRect(hwnd, &client);
    const int w = client.right - client.left, h = client.bottom - client.top;
    if (w <= 0 || h <= 0) return;

    if (!backBuffer_.dc || !backBuffer_.bitmap ||
        backBuffer_.width != w || backBuffer_.height != h) {
        DestroyBackBuffer();
        backBuffer_.dc = CreateCompatibleDC(hdc);
        backBuffer_.bitmap = CreateCompatibleBitmap(hdc, w, h);
        if (!backBuffer_.dc || !backBuffer_.bitmap) {
            DestroyBackBuffer();
            return;
        }
        backBuffer_.oldBitmap = SelectObject(backBuffer_.dc, backBuffer_.bitmap);
        backBuffer_.width = w;
        backBuffer_.height = h;
    }
    HDC memDC = backBuffer_.dc;
    if (!memDC) {
        return;
    }

    HBRUSH back = res_.windowBrush ? res_.windowBrush : CreateSolidBrush(theme_.window);
    FillRect(memDC, &client, back);
    if (!res_.windowBrush) DeleteObject(back);

    PaintChart(memDC, state, chartBounds, hover, rubberBand, dragPreview);
    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
}

// ---------------------------------------------------------------------------
// PaintInfoPanel
// ---------------------------------------------------------------------------

void Renderer::PaintInfoPanel(HDC hdc, HWND sidebar, const AppLayout& layout,
                               int scrollOffset,
                               const std::vector<int>& sectionDividers,
                               int headerH, int statusH) const {
    RECT client{}; GetClientRect(sidebar, &client);
    HBRUSH pb = res_.panelBrush ? res_.panelBrush : CreateSolidBrush(theme_.panel);
    FillRect(hdc, &client, pb);
    if (!res_.panelBrush) DeleteObject(pb);

    HPEN bp = res_.borderPen ? res_.borderPen : CreatePen(PS_SOLID, 1, theme_.border);
    HGDIOBJ op = SelectObject(hdc, bp);

    MoveToEx(hdc, client.left, client.top, nullptr);
    LineTo(hdc, client.left, client.bottom);

    const int px          = layout.info.left;
    const int pw          = std::max(1, static_cast<int>(client.right - Margin() * 2));
    const int headerBottom = headerH;
    const int statusTop    = std::max(headerBottom,
                                      static_cast<int>(client.bottom) - statusH);

    MoveToEx(hdc, px, headerBottom, nullptr); LineTo(hdc, px + pw, headerBottom);
    MoveToEx(hdc, px, statusTop,    nullptr); LineTo(hdc, px + pw, statusTop);

    for (int absY : sectionDividers) {
        const int lineY = absY - scrollOffset;
        if (lineY <= headerBottom || lineY >= statusTop) continue;
        MoveToEx(hdc, px, lineY, nullptr);
        LineTo(hdc, px + pw, lineY);
    }

    SelectObject(hdc, op);
    if (!res_.borderPen) DeleteObject(bp);
}

// ---------------------------------------------------------------------------
// CopyChartToClipboard
// ---------------------------------------------------------------------------

bool Renderer::CopyChartToClipboard(HWND hwnd,
                                      const AppState& state,
                                      const RECT& chartBounds) const {
    const int w = chartBounds.right - chartBounds.left;
    const int h = chartBounds.bottom - chartBounds.top;
    if (w <= 0 || h <= 0) return false;

    HDC screen = GetDC(nullptr);
    if (!screen) return false;

    HDC       memDC = CreateCompatibleDC(screen);
    UniqueBitmap bmp(CreateCompatibleBitmap(screen, w, h));
    if (!memDC || !bmp) { if (memDC) DeleteDC(memDC); ReleaseDC(nullptr, screen); return false; }

    {
        ScopedSelect sel(memDC, static_cast<HGDIOBJ>(bmp.get()));
        const RECT rb{0, 0, w, h};
        HBRUSH back = res_.windowBrush ? res_.windowBrush : CreateSolidBrush(theme_.window);
        FillRect(memDC, &rb, back);
        if (!res_.windowBrush) DeleteObject(back);
        // Render at {0,0,w,h} — transform is recomputed inside PaintChart,
        // so layout items appear correctly at the destination size.
        PaintChart(memDC, state, rb, {});
    }
    DeleteDC(memDC);
    ReleaseDC(nullptr, screen);

    if (!OpenClipboard(hwnd)) return false;
    EmptyClipboard();
    if (!SetClipboardData(CF_BITMAP, static_cast<HANDLE>(bmp.get()))) {
        CloseClipboard(); return false;
    }
    bmp.release();
    CloseClipboard();
    return true;
}

// ---------------------------------------------------------------------------
// ExportChartToFile  (BMP)
// ---------------------------------------------------------------------------

bool Renderer::ExportChartToFile(HWND hwnd,
                                   const AppState& state,
                                   const RECT& chartBounds) const {
    const int w = chartBounds.right - chartBounds.left;
    const int h = chartBounds.bottom - chartBounds.top;
    if (w <= 0 || h <= 0) return false;

    wchar_t fileName[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFile   = fileName;    ofn.nMaxFile  = MAX_PATH;
    ofn.lpstrFilter = L"Bitmap\0*.bmp\0All Files\0*.*\0\0";
    ofn.lpstrDefExt = L"bmp";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return false;

    HDC screen = GetDC(nullptr);
    if (!screen) return false;

    BITMAPINFOHEADER bih{};
    bih.biSize = sizeof(bih); bih.biWidth = w; bih.biHeight = -h;
    bih.biPlanes = 1; bih.biBitCount = 24; bih.biCompression = BI_RGB;
    const DWORD stride = (static_cast<DWORD>(w) * 3 + 3) & ~3u;
    bih.biSizeImage = stride * static_cast<DWORD>(h);

    void* bits = nullptr;
    BITMAPINFO bi{}; bi.bmiHeader = bih;
    HDC   memDC = CreateCompatibleDC(screen);
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);

    if (!memDC || !dib) { if (memDC) DeleteDC(memDC); if (dib) DeleteObject(dib); return false; }

    {
        ScopedSelect sel(memDC, static_cast<HGDIOBJ>(dib));
        const RECT rb{0, 0, w, h};
        HBRUSH back = res_.windowBrush ? res_.windowBrush : CreateSolidBrush(theme_.window);
        FillRect(memDC, &rb, back);
        if (!res_.windowBrush) DeleteObject(back);
        PaintChart(memDC, state, rb, {});
        GdiFlush();
    }

    BITMAPFILEHEADER bfh{};
    bfh.bfType    = 0x4D42;
    bfh.bfOffBits = sizeof(bfh) + sizeof(bih);
    bfh.bfSize    = bfh.bfOffBits + bih.biSizeImage;

    std::vector<unsigned char> bytes(bfh.bfSize);
    memcpy(bytes.data(),               &bfh, sizeof(bfh));
    memcpy(bytes.data() + sizeof(bfh), &bih, sizeof(bih));
    if (bits && bih.biSizeImage)
        memcpy(bytes.data() + bfh.bfOffBits, bits, bih.biSizeImage);

    DeleteObject(dib); DeleteDC(memDC);
    return WriteAllBytesAtomic(std::wstring(fileName), bytes);
}
