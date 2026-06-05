#include "SidebarManager.h"
#include "Utils.h"
#include <algorithm>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void SidebarManager::Defer(HDWP& dwp, HWND child, int x, int y, int w, int h) {
    if (!child || !dwp) return;
    dwp = DeferWindowPos(dwp, child, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void SidebarManager::RebuildMetrics(HWND sidebar, const Renderer& renderer) {
    const WindowUiMetrics m = ComputeWindowUiMetrics(sidebar, renderer.UiFont());
    pad_ = m.pad;
    gap_ = m.gap;
    lineH_ = m.lineH;
    labelH_ = m.labelH;
    buttonH_ = m.buttonH;
    editH_ = m.editH;
    sectionGap_ = m.sectionGap;
    headerH_ = std::max(lineH_ * 5 + gap_ * 3, Scale(96));
    statusH_ = std::max(lineH_ * 3 + gap_ * 2, Scale(54));
    scrollLine_ = buttonH_ + gap_;
}

void SidebarManager::UpdateScrollBar(HWND sidebar, int contentH, int viewH) {
    contentH_ = std::max(contentH, viewH);
    viewH_    = viewH;
    SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0,
                  std::max(0, contentH_ - 1),
                  static_cast<UINT>(std::max(1, viewH)), scroll_};
    SetScrollInfo(sidebar, SB_VERT, &si, TRUE);
}

void SidebarManager::ClampScroll(HWND sidebar) {
    const int maxScroll = std::max(0, contentH_ - viewH_);
    scroll_ = std::clamp(scroll_, 0, maxScroll);
    SCROLLINFO si{sizeof(si), SIF_POS, 0, 0, 0, scroll_};
    SetScrollInfo(sidebar, SB_VERT, &si, TRUE);
}

// ---------------------------------------------------------------------------
// LayoutCommonStrip — mode buttons, capture/print, templates
// (called first in both seat and layout modes)
// ---------------------------------------------------------------------------

int SidebarManager::LayoutCommonStrip(HDWP& dwp, const ControlHandles& c,
                                       int px, int pw, int base, int scrollUsed) {
    int y = base;
    auto rec = [&](int absY) { sectionDividers_.push_back(absY + scrollUsed); };
    auto buttons = [&](std::initializer_list<HWND> hs, int minW) {
        const int count = static_cast<int>(hs.size());
        if (count <= 0) return;
        const int cols = std::max(1, std::min(count, std::max(1, (pw + gap_) / (minW + gap_))));
        const int cw = std::max(1, (pw - gap_ * (cols - 1)) / cols);
        int col = 0;
        for (HWND h : hs) {
            Defer(dwp, h, px + col * (cw + gap_), y,
                  col == cols - 1 ? pw - col * (cw + gap_) : cw, buttonH_);
            if (++col == cols) {
                col = 0;
                y += buttonH_ + gap_;
            }
        }
        if (col != 0) y += buttonH_ + gap_;
    };

    // Mode is switched via the Arrange/Assign menu items — no sidebar buttons needed.

    return y;
}

// ---------------------------------------------------------------------------
// Sub-layout: Seats mode
// ---------------------------------------------------------------------------

int SidebarManager::LayoutSeatsModePanel(HDWP& dwp, const ControlHandles& c,
                                          int px, int pw, int base, int scrollUsed) {
    int y = base;
    auto rec = [&](int absY) { sectionDividers_.push_back(absY + scrollUsed); };
    auto buttons = [&](std::initializer_list<HWND> hs, int minW) {
        const int count = static_cast<int>(hs.size());
        if (count <= 0) return;
        const int cols = std::max(1, std::min(count, std::max(1, (pw + gap_) / (minW + gap_))));
        const int cw = std::max(1, (pw - gap_ * (cols - 1)) / cols);
        int col = 0;
        for (HWND h : hs) {
            Defer(dwp, h, px + col * (cw + gap_), y,
                  col == cols - 1 ? pw - col * (cw + gap_) : cw, buttonH_);
            if (++col == cols) {
                col = 0;
                y += buttonH_ + gap_;
            }
        }
        if (col != 0) y += buttonH_ + gap_;
    };

    // Assign tool: double-click a seat on the chart to place a student. This
    // panel holds the roster, restrictions and auto-fill tools. (The old
    // rows×cols grid and per-seat manual override are gone — furniture owns
    // the seats now; those controls are hidden by UpdateControlVisibility.)

    rec(y);
    // Roster input
    Defer(dwp, c.rosterLabel,  px, y, pw, labelH_); y += labelH_ + gap_;
    const int rosterEditH = std::max(lineH_ * 5, Scale(76));
    Defer(dwp, c.rosterEdit,   px, y, pw, rosterEditH); y += rosterEditH + gap_;
    buttons({c.importRoster, c.loadRoster, c.saveNow}, Scale(92));
    buttons({c.autoAssign, c.quickFillSeats}, Scale(122));
    buttons({c.clearAllSeats}, Scale(142));
    y += sectionGap_;

    rec(y);
    // Roster filter + list
    Defer(dwp, c.rosterFilter,         px, y, pw, editH_); y += editH_ + gap_;
    Defer(dwp, c.rosterListLabel,      px, y, pw, labelH_); y += labelH_ + gap_;
    const int listH = std::max(lineH_ * 7, Scale(96));
    Defer(dwp, c.rosterList,           px, y, pw, listH); y += listH + gap_;
    buttons({c.assignSelectedRoster, c.bulkTag}, Scale(126));
    y += sectionGap_;

    rec(y);
    // Restrictions
    Defer(dwp, c.restrictionLabel, px, y, pw, labelH_ * 2); y += labelH_ * 2 + gap_;
    const int rulesH = std::max(lineH_ * 4, Scale(68));
    Defer(dwp, c.restrictionEdit,  px, y, pw, rulesH); y += rulesH + gap_;
    buttons({c.applyRules}, Scale(140));
    y += sectionGap_;

    return y;
}

// ---------------------------------------------------------------------------
// Sub-layout: Layout mode
// ---------------------------------------------------------------------------

int SidebarManager::LayoutLayoutModePanel(HDWP& dwp, const ControlHandles& c,
                                           int px, int pw, int base, int scrollUsed) {
    int y = base;
    auto rec = [&](int absY) { sectionDividers_.push_back(absY + scrollUsed); };
    auto buttons = [&](std::initializer_list<HWND> hs, int minW) {
        const int count = static_cast<int>(hs.size());
        if (count <= 0) return;
        const int cols = std::max(1, std::min(count, std::max(1, (pw + gap_) / (minW + gap_))));
        const int cw = std::max(1, (pw - gap_ * (cols - 1)) / cols);
        int col = 0;
        for (HWND h : hs) {
            Defer(dwp, h, px + col * (cw + gap_), y,
                  col == cols - 1 ? pw - col * (cw + gap_) : cw, buttonH_);
            if (++col == cols) {
                col = 0;
                y += buttonH_ + gap_;
            }
        }
        if (col != 0) y += buttonH_ + gap_;
    };
    auto labelEdit = [&](HWND label, HWND edit, int labelMinW = Scale(54)) {
        if (pw < Scale(210)) {
            Defer(dwp, label, px, y, pw, labelH_);
            y += labelH_;
            Defer(dwp, edit, px, y, pw, editH_);
            y += editH_ + gap_;
            return;
        }
        const int lw = std::min(std::max(labelMinW, pw / 4), Scale(86));
        Defer(dwp, label, px, y, lw, editH_);
        Defer(dwp, edit, px + lw + gap_, y, pw - lw - gap_, editH_);
        y += editH_ + gap_;
    };

    // Direct manipulation remains primary, but the grouped sidebar keeps common
    // arrangement and inspector actions visible for discoverability.

    // --- Add furniture ---
    rec(y);
    Defer(dwp, c.layoutToolsLabel, px, y, pw, labelH_); y += labelH_ + gap_;
    buttons({c.addSmartboard, c.addTrap, c.addDesk}, Scale(88));
    buttons({c.addTable, c.addBigTable, c.addBlock}, Scale(88));
    // Trapezoid collaborative pods — two half-width buttons (longer labels).
    buttons({c.addTrapPair, c.addTrapPod}, Scale(126));
    y += sectionGap_;

    // --- Selection (acts on the item(s) selected on the canvas) ---
    rec(y);
    Defer(dwp, c.layoutTransformLabel, px, y, pw, labelH_); y += labelH_ + gap_;
    buttons({c.deleteLayout, c.duplicateLayoutItem, c.lockItem}, Scale(88));
    buttons({c.rotateCW, c.rotateCCW, c.flipH}, Scale(88));
    buttons({c.selectAllLayout, c.toggleVisible}, Scale(126));
    buttons({c.sendLayoutBack, c.bringLayoutFront}, Scale(126));
    y += sectionGap_;

    return y;
}

// ---------------------------------------------------------------------------
// UpdateControlVisibility
// ---------------------------------------------------------------------------

void SidebarManager::UpdateControlVisibility(const ControlHandles& c, ChartMode mode) {
    const int sv = (mode == ChartMode::Seats)  ? SW_SHOW : SW_HIDE;
    const int lv = (mode == ChartMode::Layout) ? SW_SHOW : SW_HIDE;

    // Assign-tool panel (formerly Seats mode): roster, auto-fill, restrictions.
    for (HWND h : {c.rosterLabel, c.rosterEdit, c.importRoster, c.loadRoster, c.saveNow,
                   c.autoAssign, c.quickFillSeats, c.clearAllSeats,
                   c.rosterFilter, c.rosterListLabel, c.rosterList, c.assignSelectedRoster, c.bulkTag,
                   c.restrictionLabel, c.restrictionEdit, c.applyRules})
        if (h) ShowWindow(h, sv);

    // Layout-only (the simplified, always-visible-in-layout-mode panel)
    for (HWND h : {c.layoutToolsLabel, c.addSmartboard, c.addTrap, c.addDesk,
                   c.addTable, c.addBigTable, c.addBlock, c.addTrapPair, c.addTrapPod,
                   c.layoutTransformLabel, c.deleteLayout, c.duplicateLayoutItem, c.lockItem,
                   c.rotateCW, c.rotateCCW, c.flipH, c.selectAllLayout,
                   c.toggleVisible, c.sendLayoutBack, c.bringLayoutFront})
        if (h) ShowWindow(h, lv);

    // Merge is intentionally disabled while the grouping/copy workflows cover
    // the common teacher need without creating confusing synthetic furniture.
    for (HWND h : {c.captureChart, c.exportChart, c.printChart, c.exportCsv,
                   c.seatingReport, c.exportHtml, c.saveTemplateBtn, c.loadTemplateBtn,
                   c.mergeSelected,
                   c.alignLabel, c.alignLeft, c.alignRight, c.alignTop, c.alignBottom,
                   c.alignCenterH, c.alignCenterV, c.distributeH, c.distributeV,
                   c.presetRows, c.presetU, c.presetHorseshoe,
                   c.layoutInspectorLabel, c.layoutNameLabel, c.layoutLabelEdit,
                   c.layoutXLabel, c.layoutYLabel, c.layoutWidthLabel, c.layoutHeightLabel,
                   c.layoutXEdit, c.layoutYEdit, c.layoutWidthEdit, c.layoutHeightEdit,
                   c.layoutCapacityLabel, c.layoutCapacityEdit, c.applyLayoutItem,
                   c.layoutXSpin, c.layoutYSpin, c.layoutWSpin, c.layoutHSpin,
                   c.roomSizeLabel, c.roomWidthLabel, c.roomWidthEdit,
                   c.roomHeightLabel, c.roomHeightEdit, c.applyRoomSize,
                   c.frontEdgeLabel, c.frontEdgeButton})
        if (h) ShowWindow(h, SW_HIDE);

    // Mode buttons no longer live in the sidebar; keep them hidden always.
    for (HWND h : {c.modeLabel, c.seatMode, c.layoutMode})
        if (h) ShowWindow(h, SW_HIDE);
}

// ---------------------------------------------------------------------------
// Recalculate — top-level orchestrator
// ---------------------------------------------------------------------------

void SidebarManager::Recalculate(HWND sidebar, const AppState& state,
                                   const ControlHandles& c, const Renderer& renderer) {
    RebuildMetrics(sidebar, renderer);
    RECT rc{}; GetClientRect(sidebar, &rc);

    const int padding   = pad_;
    // headerH increased from Scale(88) → Scale(104) to accommodate the
    // two-line summary label (ISTE 1.4: at-a-glance stats, not a single
    // truncated horizontal line).
    const int headerH   = headerH_;
    const int statusH   = statusH_;
    const int scrollTop = headerH;
    const int scrollBot = std::max(scrollTop + buttonH_ + gap_,
                                   static_cast<int>(rc.bottom) - statusH);
    const int viewH     = std::max(1, scrollBot - scrollTop);

    scroll_ = std::clamp(scroll_, 0, std::max(0, contentH_ - viewH));
    const int pw       = std::max(1, static_cast<int>(rc.right - rc.left) - padding * 2);
    const int px       = padding;
    const int base     = scrollTop + padding - scroll_;
    const int scrollUsed = scroll_;

    sectionDividers_.clear();

    HDWP dwp = BeginDeferWindowPos(130);

    // Fixed header (not scrolled)
    Defer(dwp, c.titleLabel,   padding, padding,           pw, lineH_ * 2);
    Defer(dwp, c.summaryLabel, padding, padding + lineH_ * 2 + gap_, pw, lineH_ * 3);
    Defer(dwp, c.statusLabel,  padding,
          std::max(padding, static_cast<int>(rc.bottom) - statusH + gap_),
          pw, lineH_ * 2 + gap_);

    // Common strip (mode buttons, capture/print, templates) — always visible, scrolled
    int y = LayoutCommonStrip(dwp, c, px, pw, base, scrollUsed);

    // Mode-specific content
    int contentBottom = 0;
    if (state.chartMode == ChartMode::Seats)
        contentBottom = LayoutSeatsModePanel(dwp, c, px, pw, y, scrollUsed);
    else
        contentBottom = LayoutLayoutModePanel(dwp, c, px, pw, y, scrollUsed);

    UpdateControlVisibility(c, state.chartMode);

    if (dwp) EndDeferWindowPos(dwp);

    // Keep the fixed header/status bands above the scrolled content so they stay
    // readable and the content appears clipped behind them (rather than the
    // scrolled controls painting over the title/summary/status text).
    for (HWND h : {c.titleLabel, c.summaryLabel, c.statusLabel})
        if (h) SetWindowPos(h, HWND_TOP, 0, 0, 0, 0,
                            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    const int contentH = contentBottom - base + padding;
    UpdateScrollBar(sidebar, contentH, viewH);
    ClampScroll(sidebar);

    if (scroll_ != scrollUsed) {
        Recalculate(sidebar, state, c, renderer);
        return;
    }

    RedrawWindow(sidebar, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

// ---------------------------------------------------------------------------
// Scroll helpers
// ---------------------------------------------------------------------------

void SidebarManager::ScrollTo(HWND sidebar, int newPos) {
    const int maxScroll = std::max(0, contentH_ - viewH_);
    newPos = std::clamp(newPos, 0, maxScroll);
    const int delta = newPos - scroll_;
    if (delta == 0) return;

    RECT rc{}; GetClientRect(sidebar, &rc);
    const int top = headerH_, bot = std::max(top, static_cast<int>(rc.bottom - statusH_));
    RECT sr{0, top, rc.right, bot};

    scroll_ = newPos;
    SCROLLINFO si{sizeof(si), SIF_POS, 0, 0, 0, scroll_};
    SetScrollInfo(sidebar, SB_VERT, &si, TRUE);

    if (std::abs(delta) < std::max(1, viewH_)) {
        // SW_SCROLLCHILDREN is intentionally omitted — Recalculate (called by the
        // parent after ScrollTo) repositions all child HWNDs atomically, so we
        // only need to scroll the background pixels here. Including SW_SCROLLCHILDREN
        // would temporarily move the fixed header/footer labels into the scrollable
        // region before Recalculate corrects them, causing a visible clipping artifact.
        ScrollWindowEx(sidebar, 0, -delta, &sr, &sr, nullptr, nullptr,
                       SW_INVALIDATE | SW_ERASE);
        RedrawWindow(sidebar, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
}

void SidebarManager::HandleVScroll(HWND sidebar, WPARAM wParam,
                                    const AppState& state, const ControlHandles& c,
                                    const Renderer& renderer) {
    SCROLLINFO si{sizeof(si), SIF_ALL};
    GetScrollInfo(sidebar, SB_VERT, &si);
    int pos = scroll_;
    switch (LOWORD(wParam)) {
    case SB_LINEUP:    pos -= scrollLine_; break;
    case SB_LINEDOWN:  pos += scrollLine_; break;
    case SB_PAGEUP:    pos -= static_cast<int>(si.nPage); break;
    case SB_PAGEDOWN:  pos += static_cast<int>(si.nPage); break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: pos = si.nTrackPos; break;
    case SB_TOP:    pos = 0; break;
    case SB_BOTTOM: pos = std::max(0, contentH_ - viewH_); break;
    default: break;
    }
    ScrollTo(sidebar, pos);
    Recalculate(sidebar, state, c, renderer);
}

void SidebarManager::HandleMouseWheel(HWND sidebar, int wheelDelta,
                                       const AppState& state, const ControlHandles& c,
                                       const Renderer& renderer) {
    ScrollTo(sidebar, scroll_ - (wheelDelta / WHEEL_DELTA) * scrollLine_);
    Recalculate(sidebar, state, c, renderer);
}
