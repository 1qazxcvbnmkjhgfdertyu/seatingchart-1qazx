#pragma once
#include "AppState.h"
#include "Utils.h"
#include <windows.h>
#include <unordered_map>
#include <vector>

struct ThemeResources {
    HBRUSH windowBrush = nullptr, panelBrush = nullptr, inputBrush = nullptr;
    HBRUSH seatEmptyBrush = nullptr, seatOccupiedBrush = nullptr, seatSelectedBrush = nullptr;
    HBRUSH paperBrush = nullptr, furnitureBrush = nullptr, furnitureSelectedBrush = nullptr;
    HBRUSH badgeBrush = nullptr, lockBrush = nullptr;
    HPEN   borderPen = nullptr, selectedPen = nullptr;
    HPEN   furniturePen = nullptr, furnitureSelectedPen = nullptr, roomPen = nullptr;
    HPEN   rubberBandPen = nullptr, gridPen = nullptr;
};

struct HoverState {
    int layoutItem = -1;
};

struct DragPreviewState {
    bool active = false;
    bool valid = false;
    bool overChart = false;
    bool targetSeat = false;
    LayoutSeatRef seat{ -1, -1 };
    POINT cursorPt{};
    std::wstring studentName;
};

// ---------------------------------------------------------------------------
// Renderer — owns theme colors, GDI resources, and fonts.
// Pass by reference wherever painting or color queries are needed.
// ---------------------------------------------------------------------------
struct BackBuffer {
    HDC     dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    int     width = 0;
    int     height = 0;
};

class Renderer {
public:
    Renderer()  = default;
    ~Renderer() { DestroyResources(); DestroyFonts(); DestroyBackBuffer(); }

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    // --- Theme / fonts (call when DPI or Windows theme changes) ---
    void ApplyThemeFromSystem();
    void RebuildFonts(UINT dpi);

    // --- Accessors used by sidebar WM_CTLCOLORxxx handlers ---
    COLORREF TextColor()   const { return theme_.text; }
    COLORREF PanelColor()  const { return theme_.panel; }
    COLORREF WindowColor() const { return theme_.window; }
    HBRUSH   PanelBrush()  const { return res_.panelBrush  ? res_.panelBrush  : static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)); }
    HBRUSH   InputBrush()  const { return res_.inputBrush  ? res_.inputBrush  : static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)); }
    HFONT    UiFont()      const { return uiFont_; }
    HFONT    TitleFont()   const { return titleFont_; }
    HFONT    SectionFont() const { return sectionFont_; }
    const ThemeColors&    Theme()     const { return theme_; }
    const ThemeResources& Resources() const { return res_; }

    // --- Paint ---
    // chartBounds: rect (in dest DC coords) to draw the chart into.
    // hover: optional item index for hover highlight; pass {} for export/print.
    // rubberBand: active rubber-band selection rect in screen coords (zeroed = none).
    void PaintChart(HDC hdc, const AppState& state,
                    const RECT& chartBounds, HoverState hover,
                    RECT rubberBand = {},
                    const DragPreviewState& dragPreview = {}) const;

    void PaintWindowBuffered(HWND hwnd, HDC hdc, const AppState& state,
                             const RECT& chartBounds, HoverState hover,
                             RECT rubberBand = {},
                             const DragPreviewState& dragPreview = {}) const;

    // sectionDividers: absolute content-top-relative Y positions set by SidebarManager.
    void PaintInfoPanel(HDC hdc, HWND sidebar, const AppLayout& layout,
                        int scrollOffset,
                        const std::vector<int>& sectionDividers,
                        int headerH, int statusH) const;

    // --- Export ---
    [[nodiscard]] bool CopyChartToClipboard(HWND hwnd,
                                             const AppState& state,
                                             const RECT& chartBounds) const;
    [[nodiscard]] bool ExportChartToFile(HWND hwnd,
                                          const AppState& state,
                                          const RECT& chartBounds) const;

private:
    ThemeColors    theme_{};
    ThemeResources res_{};
    HFONT          uiFont_ = nullptr, titleFont_ = nullptr, sectionFont_ = nullptr;
    mutable BackBuffer backBuffer_{};
    mutable std::unordered_map<COLORREF, HBRUSH> colorBrushCache_;

    void RebuildResources();
    void DestroyResources();
    void DestroyFonts();
    void DestroyBackBuffer() const;
    HBRUSH CachedColorBrush(COLORREF color) const;

    static void DrawTextCentered(HDC hdc, const RECT& rc,
                                  std::wstring_view text, COLORREF color);
    static std::wstring SeatInitials(std::wstring_view name);

    // Draws a single layout item's shape into screenBounds (no handles/overlays).
    void DrawLayoutItemShape(HDC hdc, const LayoutItem& item,
                              const RECT& screenBounds, bool sel) const;

    // Draws an optional light grid overlay inside the room.
    void DrawRoomGrid(HDC hdc, const LayoutViewTransform& tx) const;

    // Draws the "FRONT" banner along the room's designated front edge.
    void DrawFrontIndicator(HDC hdc, const LayoutViewTransform& tx, RoomEdge front) const;

    // Draws keep-apart radius rings around the focused seat's occupant.
    void DrawKeepApartRings(HDC hdc, const AppState& state,
                            const LayoutViewTransform& tx) const;

    // Draws the live roster-drag target overlay after furniture/seats paint.
    void PaintStudentDragPreview(HDC hdc, const AppState& state,
                                 const LayoutViewTransform& tx,
                                 const DragPreviewState& preview) const;
};
