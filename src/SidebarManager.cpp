#include "SidebarManager.h"
#include "Utils.h"
#include <algorithm>
#include <commctrl.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// DeferFixed — for the three FIXED header/footer controls (not scrolled).
void SidebarManager::DeferFixed(HDWP& dwp, HWND child, int x, int y, int w, int h) {
    if (!child || !dwp) return;
    dwp = DeferWindowPos(dwp, child, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

// Defer — for ALL scrolled content controls.
// Controls completely outside the visible scroll band are moved off-screen.
// Controls that straddle an edge are clamped so only their visible slice shows.
// The fixed header/tab/footer controls are raised to HWND_TOP at the end of
// Recalculate(), so they always paint on top of any clamped scrollable content.
void SidebarManager::Defer(HDWP& dwp, HWND child, int x, int y, int w, int h) {
    if (!child || !dwp) return;
    if (viewH_ > buttonH_) {
        const int scrollBand = headerH_ + tabH_;
        const int scrollBot  = scrollBand + viewH_;

        // Completely outside the viewport — move off-screen.
        if (y + h <= scrollBand || y >= scrollBot) {
            dwp = DeferWindowPos(dwp, child, nullptr, x, -(h + 4), w, h,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
            return;
        }
        // Partially above the top edge: push y down to scrollBand, shrink h.
        if (y < scrollBand) {
            h -= (scrollBand - y);
            y  = scrollBand;
        }
        // Partially below the bottom edge: shrink h so bottom == scrollBot.
        if (y + h > scrollBot) {
            h = scrollBot - y;
        }
        // Degenerate after clamping (shouldn't happen, but guard anyway).
        if (h <= 0) {
            dwp = DeferWindowPos(dwp, child, nullptr, x, -(1 + 4), w, 1,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
            return;
        }
    }
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
    headerH_    = std::max(lineH_ * 5 + gap_ * 3, Scale(96));
    tabH_       = std::max(lineH_ + Scale(10), Scale(28)); // tab strip row height
    statusH_    = std::max(lineH_ * 7 + gap_ * 5, Scale(120));
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
// PlaceButtons — shared button-grid helper for all three panel functions
// ---------------------------------------------------------------------------
void SidebarManager::PlaceButtons(HDWP& dwp, std::initializer_list<HWND> hs,
                                   int px, int pw, int& y, int minW) {
    const int count = static_cast<int>(hs.size());
    if (count <= 0) return;
    const int cols = std::max(1, std::min(count, std::max(1, (pw + gap_) / (minW + gap_))));
    const int cw   = std::max(1, (pw - gap_ * (cols - 1)) / cols);
    int col = 0;
    for (HWND h : hs) {
        Defer(dwp, h, px + col * (cw + gap_), y,
              col == cols - 1 ? pw - col * (cw + gap_) : cw, buttonH_);
        if (++col == cols) { col = 0; y += buttonH_ + gap_; }
    }
    if (col != 0) y += buttonH_ + gap_;
}

// ---------------------------------------------------------------------------
// LayoutRosterPanel  (tab 0)
// ---------------------------------------------------------------------------

int SidebarManager::LayoutRosterPanel(HDWP& dwp, const ControlHandles& c,
                                       int px, int pw, int base, int scrollUsed) {
    int y = base;
    auto rec = [&](int absY) { sectionDividers_.push_back(absY + scrollUsed); };

    // Two-column student ListView — sized to show every row without an internal
    // scrollbar.  SyncRosterView has already run, so GetItemCount is accurate.
    // The sidebar's own scroll handles navigation; the list never needs to scroll itself.
    rec(y);
    {
        const int nItems = c.rosterView ? ListView_GetItemCount(c.rosterView) : 8;
        // Use the ListView's own metric to get the exact row height at current DPI/font.
        // ListView_ApproximateViewRect returns client-area height for item rows only
        // (header excluded in LVS_REPORT mode), so we add lineH_*2 to cover the header
        // row and the WS_EX_CLIENTEDGE border chrome at any DPI.
        const DWORD approx = c.rosterView
            ? ListView_ApproximateViewRect(c.rosterView, pw, -1, std::max(8, nItems))
            : 0;
        const int listH = approx
            ? (static_cast<int>(HIWORD(approx)) + lineH_ * 2)
            : lineH_ * (std::max(8, nItems) + 3) + Scale(8);
        Defer(dwp, c.rosterView, px, y, pw, listH); y += listH + gap_;
    }

    // Show last names toggle
    PlaceButtons(dwp, {c.showLastNamesBtn}, px, pw, y, Scale(110));
    y += sectionGap_;

    // Import / Load / Save
    rec(y);
    PlaceButtons(dwp, {c.importRoster, c.loadRoster, c.saveNow}, px, pw, y, Scale(80));
    y += sectionGap_;

    // Auto-assign / Clear
    PlaceButtons(dwp, {c.autoAssign, c.clearAllSeats}, px, pw, y, Scale(120));
    y += sectionGap_;

    // Roster list assignment helper
    Defer(dwp, c.rosterListLabel, px, y, pw, labelH_); y += labelH_ + gap_;
    const int smallListH = std::max(lineH_ * 4, Scale(64));
    Defer(dwp, c.rosterList, px, y, pw, smallListH); y += smallListH + gap_;
    PlaceButtons(dwp, {c.assignSelectedRoster, c.bulkTag}, px, pw, y, Scale(100));
    y += sectionGap_;

    return y;
}

// ---------------------------------------------------------------------------
// LayoutRulesPanel  (tab 1)
// ---------------------------------------------------------------------------

int SidebarManager::LayoutRulesPanel(HDWP& dwp, const ControlHandles& c,
                                      int px, int pw, int base, int scrollUsed,
                                      const AppState& state) {
    int y = base;
    auto rec = [&](int absY) { sectionDividers_.push_back(absY + scrollUsed); };

    // Dynamic list height: size each ListView to show all its rows without internal
    // scrolling.  totalRows = max(3, realCount + 1) mirrors what SyncRulesLists inserts.
    // Use ListView_ApproximateViewRect for the exact row height at current DPI/font.
    // LVS_NOSCROLL means the list never shows an internal scrollbar even if clipped.
    auto rulesListH = [&](const std::vector<Restriction>& rules, HWND lv) -> int {
        const int n = std::max(3, static_cast<int>(rules.size()) + 1);
        if (lv) {
            const DWORD approx = ListView_ApproximateViewRect(lv, pw, -1, n);
            if (approx) return static_cast<int>(HIWORD(approx)) + lineH_ * 2;
        }
        return lineH_ * (n + 2) + Scale(8);
    };

    // Keep Apart section
    rec(y);
    Defer(dwp, c.keepApartHeader, px, y, pw, labelH_); y += labelH_ + gap_;
    {
        const int h = rulesListH(state.restrictions, c.keepApartList);
        Defer(dwp, c.keepApartList, px, y, pw, h); y += h + gap_;
    }
    PlaceButtons(dwp, {c.remKeepApartBtn}, px, pw, y, Scale(80));
    y += sectionGap_;

    // Keep Together section
    rec(y);
    Defer(dwp, c.keepTogetherHeader, px, y, pw, labelH_); y += labelH_ + gap_;
    {
        const int h = rulesListH(state.affinities, c.keepTogetherList);
        Defer(dwp, c.keepTogetherList, px, y, pw, h); y += h + gap_;
    }
    PlaceButtons(dwp, {c.remKeepTogetherBtn}, px, pw, y, Scale(80));
    y += sectionGap_;

    // Desk Tag Rules placeholder (unchanged)
    rec(y);
    Defer(dwp, c.deskTagHeader, px, y, pw, labelH_); y += labelH_ + gap_;
    Defer(dwp, c.deskTagDesc,   px, y, pw, labelH_ * 2); y += labelH_ * 2 + gap_;
    const int deskListH = std::max(lineH_ * 4, Scale(60));
    Defer(dwp, c.deskTagList,   px, y, pw, deskListH); y += deskListH + gap_;
    PlaceButtons(dwp, {c.addDeskTagRuleBtn, c.remDeskTagRuleBtn}, px, pw, y, Scale(92));
    y += sectionGap_;

    return y;
}

// ---------------------------------------------------------------------------
// LayoutArrangePanel  (tab 2)
// ---------------------------------------------------------------------------

int SidebarManager::LayoutArrangePanel(HDWP& dwp, const ControlHandles& c,
                                        int px, int pw, int base, int scrollUsed) {
    int y = base;
    auto rec = [&](int absY) { sectionDividers_.push_back(absY + scrollUsed); };

    rec(y);
    Defer(dwp, c.layoutToolsLabel, px, y, pw, labelH_); y += labelH_ + gap_;
    PlaceButtons(dwp, {c.addSmartboard, c.addTrap, c.addDesk},   px, pw, y, Scale(80));
    PlaceButtons(dwp, {c.addTable, c.addBigTable, c.addBlock},   px, pw, y, Scale(80));
    PlaceButtons(dwp, {c.addTrapPair, c.addTrapPod},             px, pw, y, Scale(110));
    y += sectionGap_;

    rec(y);
    Defer(dwp, c.layoutTransformLabel, px, y, pw, labelH_); y += labelH_ + gap_;
    PlaceButtons(dwp, {c.deleteLayout, c.duplicateLayoutItem, c.lockItem}, px, pw, y, Scale(80));
    PlaceButtons(dwp, {c.rotateCW, c.rotateCCW, c.flipH},                 px, pw, y, Scale(80));
    PlaceButtons(dwp, {c.selectAllLayout, c.toggleVisible},               px, pw, y, Scale(100));
    PlaceButtons(dwp, {c.sendLayoutBack, c.bringLayoutFront},             px, pw, y, Scale(100));
    y += sectionGap_;

    rec(y);
    PlaceButtons(dwp, {c.quickFillSeats, c.showAllObjects}, px, pw, y, Scale(110));
    y += sectionGap_;

    rec(y);
    Defer(dwp, c.layoutInspectorLabel, px, y, pw, labelH_); y += labelH_ + gap_;
    {
        const int lw    = Scale(42);
        const int spinW = Scale(18);
        auto spinRow = [&](HWND lbl, HWND edt, HWND spn) {
            const int ew = pw - lw - gap_ - spinW;
            Defer(dwp, lbl, px,                      y, lw,    editH_);
            Defer(dwp, edt, px + lw + gap_,          y, ew,    editH_);
            Defer(dwp, spn, px + lw + gap_ + ew,     y, spinW, editH_);
            y += editH_ + gap_;
        };
        auto editRow = [&](HWND lbl, HWND edt) {
            const int ew = pw - lw - gap_;
            Defer(dwp, lbl, px,             y, lw, editH_);
            Defer(dwp, edt, px + lw + gap_, y, ew, editH_);
            y += editH_ + gap_;
        };
        editRow(c.layoutNameLabel,     c.layoutLabelEdit);
        spinRow(c.layoutXLabel,        c.layoutXEdit,      c.layoutXSpin);
        spinRow(c.layoutYLabel,        c.layoutYEdit,      c.layoutYSpin);
        spinRow(c.layoutWidthLabel,    c.layoutWidthEdit,  c.layoutWSpin);
        spinRow(c.layoutHeightLabel,   c.layoutHeightEdit, c.layoutHSpin);
        editRow(c.layoutCapacityLabel, c.layoutCapacityEdit);
    }
    Defer(dwp, c.applyLayoutItem, px, y, pw, buttonH_); y += buttonH_ + gap_;
    y += sectionGap_;

    return y;
}

// ---------------------------------------------------------------------------
// LayoutGroupsPanel  (tab 3)
// ---------------------------------------------------------------------------

int SidebarManager::LayoutGroupsPanel(HDWP& dwp, const ControlHandles& c,
                                       int px, int pw, int base, int scrollUsed) {
    int y = base;
    auto rec = [&](int absY) { sectionDividers_.push_back(absY + scrollUsed); };
    const int pairListH = std::max(lineH_ * 4, Scale(60));

    rec(y);
    // "Groups of: [combo]  or [label]"
    const int comboLblW = Scale(62);
    const int comboW    = Scale(52);
    const int orLblW    = Scale(24);
    const int orValW    = Scale(40);
    Defer(dwp, c.groupSizeLabel,  px,                              y, comboLblW, buttonH_);
    // Combo boxes need extra height for the dropped list; buttonH_ alone leaves
    // the control looking clickable but unable to open.
    Defer(dwp, c.groupSizeCombo,  px + comboLblW + gap_,           y, comboW,    buttonH_ + lineH_ * 10);
    Defer(dwp, c.groupOrLabel,    px + comboLblW + gap_ + comboW + gap_, y, orLblW, buttonH_);
    Defer(dwp, c.groupOrValLabel, px + comboLblW + gap_ + comboW + gap_ + orLblW + gap_, y,
          std::max(4, pw - comboLblW - comboW - orLblW - gap_ * 4), buttonH_);
    y += buttonH_ + gap_ + sectionGap_;
    Defer(dwp, c.groupSummaryLabel, px, y, pw, lineH_ * 3); y += lineH_ * 3 + gap_;

    // Shuffle controls
    rec(y);
    PlaceButtons(dwp, {c.shuffleGroupsBtn, c.groupResetBtn}, px, pw, y, Scale(100));
    Defer(dwp, c.groupAvoidSameNumberCheck, px, y, pw, buttonH_); y += buttonH_ + gap_;
    Defer(dwp, c.groupAvoidSamePartnersCheck, px, y, pw, buttonH_); y += buttonH_ + gap_;
    Defer(dwp, c.groupAvoidSameFullGroupCheck, px, y, pw, buttonH_); y += buttonH_ + gap_;
    y += sectionGap_;

    // Keep Apart section (collapsed by default so students do not see active rules)
    rec(y);
    Defer(dwp, c.groupKeepApartToggle, px, y, pw, buttonH_); y += buttonH_ + gap_;
    if (!groupKeepApartCollapsed_) {
        Defer(dwp, c.keepApartList, px, y, pw, pairListH); y += pairListH + gap_;
        PlaceButtons(dwp, {c.addKeepApartBtn, c.remKeepApartBtn}, px, pw, y, Scale(80));
        y += sectionGap_;
    }

    // Keep Together section
    rec(y);
    Defer(dwp, c.groupKeepTogetherToggle, px, y, pw, buttonH_); y += buttonH_ + gap_;
    if (!groupKeepTogetherCollapsed_) {
        Defer(dwp, c.keepTogetherList, px, y, pw, pairListH); y += pairListH + gap_;
        PlaceButtons(dwp, {c.addKeepTogetherBtn, c.remKeepTogetherBtn}, px, pw, y, Scale(80));
        y += sectionGap_;
    }

    return y;
}

// ---------------------------------------------------------------------------
// ResizeListViewColumns — call after panel layout to auto-size Last Name col
// ---------------------------------------------------------------------------

void SidebarManager::ResizeListViewColumns(const ControlHandles& c, int /*panelWidth*/) const {
    // Use the ListView's own client rect so column totals never exceed it,
    // which prevents the horizontal scrollbar from appearing.
    auto clientW = [](HWND lv) -> int {
        if (!lv) return 0;
        RECT rc{}; GetClientRect(lv, &rc);
        return std::max(1, static_cast<int>(rc.right - rc.left));
    };

    if (c.rosterView) {
        const int avail = clientW(c.rosterView);
        const int numW  = Scale(28);
        const int nameW = std::max(Scale(50), (avail - numW) / 2);
        ListView_SetColumnWidth(c.rosterView, 0, numW);
        ListView_SetColumnWidth(c.rosterView, 1, nameW);
        ListView_SetColumnWidth(c.rosterView, 2, avail - numW - nameW);
    }

    auto resizePair = [&](HWND lv) {
        if (!lv) return;
        const int avail = clientW(lv);
        const int colW  = std::max(Scale(60), avail / 2);
        ListView_SetColumnWidth(lv, 0, colW);
        ListView_SetColumnWidth(lv, 1, avail - colW);
    };
    resizePair(c.keepApartList);
    resizePair(c.keepTogetherList);
    resizePair(c.deskTagList);
}

// ---------------------------------------------------------------------------
// UpdateControlVisibility — tab-based: show only the active tab's controls
// ---------------------------------------------------------------------------

void SidebarManager::UpdateControlVisibility(const ControlHandles& c, ChartMode /*mode*/) {
    const bool onRoster  = (activeTab_ == 0);
    const bool onRules   = (activeTab_ == 1);
    const bool onArrange = (activeTab_ == 2);
    const bool onGroups  = (activeTab_ == 3);

    // Roster tab
    for (HWND h : {c.rosterView,
                   c.importRoster, c.loadRoster, c.saveNow,
                   c.autoAssign, c.clearAllSeats,
                   c.rosterListLabel, c.rosterList,
                   c.assignSelectedRoster, c.bulkTag,
                   c.showLastNamesBtn})
        if (h) ShowWindow(h, onRoster ? SW_SHOW : SW_HIDE);

    // Rules tab — the + Add buttons are intentionally omitted here; ghost rows handle
    // new-rule creation inline.  They are handled in the explicit blocks below so
    // they remain visible only in the Groups tab (where they're still needed).
    for (HWND h : {c.keepApartHeader, c.keepApartDesc, c.keepApartList,
                   c.remKeepApartBtn,
                   c.keepTogetherHeader, c.keepTogetherDesc, c.keepTogetherList,
                   c.remKeepTogetherBtn,
                   c.deskTagHeader, c.deskTagDesc, c.deskTagList,
                   c.addDeskTagRuleBtn, c.remDeskTagRuleBtn})
        if (h) ShowWindow(h, onRules ? SW_SHOW : SW_HIDE);

    // Arrange tab
    for (HWND h : {c.layoutToolsLabel, c.addSmartboard, c.addTrap, c.addDesk,
                   c.addTable, c.addBigTable, c.addBlock, c.addTrapPair, c.addTrapPod,
                   c.layoutTransformLabel, c.deleteLayout, c.duplicateLayoutItem, c.lockItem,
                   c.rotateCW, c.rotateCCW, c.flipH, c.selectAllLayout,
                   c.toggleVisible, c.sendLayoutBack, c.bringLayoutFront,
                   c.quickFillSeats, c.showAllObjects,
                   c.layoutInspectorLabel,
                   c.layoutNameLabel, c.layoutLabelEdit,
                   c.layoutXLabel, c.layoutXEdit, c.layoutXSpin,
                   c.layoutYLabel, c.layoutYEdit, c.layoutYSpin,
                   c.layoutWidthLabel, c.layoutWidthEdit, c.layoutWSpin,
                   c.layoutHeightLabel, c.layoutHeightEdit, c.layoutHSpin,
                   c.layoutCapacityLabel, c.layoutCapacityEdit,
                   c.applyLayoutItem})
        if (h) ShowWindow(h, onArrange ? SW_SHOW : SW_HIDE);

    // Groups tab
    for (HWND h : {c.groupSizeLabel, c.groupSizeCombo, c.groupOrLabel, c.groupOrValLabel, c.groupSummaryLabel,
                   c.shuffleGroupsBtn, c.groupResetBtn,
                   c.groupAvoidSameNumberCheck, c.groupAvoidSamePartnersCheck, c.groupAvoidSameFullGroupCheck,
                   c.groupKeepApartToggle, c.groupKeepTogetherToggle})
        if (h) ShowWindow(h, onGroups ? SW_SHOW : SW_HIDE);
    if (c.keepApartList) ShowWindow(c.keepApartList,
        (onRules || (onGroups && !groupKeepApartCollapsed_)) ? SW_SHOW : SW_HIDE);
    // + Add buttons: only on Groups tab (rules tab uses ghost rows instead).
    if (c.addKeepApartBtn) ShowWindow(c.addKeepApartBtn,
        (onGroups && !groupKeepApartCollapsed_) ? SW_SHOW : SW_HIDE);
    if (c.remKeepApartBtn) ShowWindow(c.remKeepApartBtn,
        (onRules || (onGroups && !groupKeepApartCollapsed_)) ? SW_SHOW : SW_HIDE);
    if (c.keepTogetherList) ShowWindow(c.keepTogetherList,
        (onRules || (onGroups && !groupKeepTogetherCollapsed_)) ? SW_SHOW : SW_HIDE);
    if (c.addKeepTogetherBtn) ShowWindow(c.addKeepTogetherBtn,
        (onGroups && !groupKeepTogetherCollapsed_) ? SW_SHOW : SW_HIDE);
    if (c.remKeepTogetherBtn) ShowWindow(c.remKeepTogetherBtn,
        (onRules || (onGroups && !groupKeepTogetherCollapsed_)) ? SW_SHOW : SW_HIDE);

    // Retired controls are never created (handles are nullptr), so no hide loops needed.
}

// ---------------------------------------------------------------------------
// Recalculate — top-level orchestrator
// ---------------------------------------------------------------------------

void SidebarManager::Recalculate(HWND sidebar, const AppState& state,
                                   const ControlHandles& c, const Renderer& renderer) {
    RebuildMetrics(sidebar, renderer);
    RECT rc{}; GetClientRect(sidebar, &rc);

    const int padding = pad_;
    const int pw = std::max(1, static_cast<int>(rc.right - rc.left) - padding * 2);
    const int px = padding;
    const int statusH = statusH_;

    // Sync active tab with the canvas mode when mode changes externally.
    // Tab 3 (Groups) is mode-neutral — don't override it.
    if (activeTab_ != 3) {
        if (state.chartMode == ChartMode::Layout) {
            activeTab_ = 2;
        } else if (activeTab_ == 2) {
            activeTab_ = 0;
        }
    }
    if (c.tabControl) TabCtrl_SetCurSel(c.tabControl, activeTab_);

    // Fixed zones (not scrolled):
    //   [0 .. headerH_)         — title + summary
    //   [headerH_ .. headerH_+tabH_) — tab strip
    //   [headerH_+tabH_ .. scrollBot) — scrollable content
    //   [scrollBot .. rc.bottom)     — status
    const int scrollTop = headerH_ + tabH_;
    const int scrollBot = std::max(scrollTop + buttonH_ + gap_,
                                   static_cast<int>(rc.bottom) - statusH);
    const int viewH     = std::max(1, scrollBot - scrollTop);

    viewH_ = viewH; // set before Defer() is called

    scroll_ = std::clamp(scroll_, 0, std::max(0, contentH_ - viewH));
    const int base       = scrollTop + padding - scroll_;
    const int scrollUsed = scroll_;

    sectionDividers_.clear();

    HDWP dwp = BeginDeferWindowPos(100);

    // Fixed: title, summary, footer — bypass scrollable-region clamp
    DeferFixed(dwp, c.titleLabel,   padding, padding,                     pw, lineH_ * 2);
    DeferFixed(dwp, c.summaryLabel, padding, padding + lineH_ * 2 + gap_, pw, lineH_ * 3);
    const int footerTop    = std::max(padding, static_cast<int>(rc.bottom) - statusH + gap_);
    const int statusSlot   = lineH_ * 2;          // 2-line height for status text
    const int metaSlot     = lineH_ * 2;          // 2-line height for meta/hint text
    DeferFixed(dwp, c.statusLabel,     padding, footerTop,                                    pw, statusSlot);
    DeferFixed(dwp, c.footerMetaLabel, padding, footerTop + statusSlot + gap_,               pw, metaSlot);
    DeferFixed(dwp, c.footerProgress,  padding, footerTop + statusSlot + gap_ + metaSlot + gap_, pw, std::max(Scale(12), lineH_));

    // Fixed: tab control spans the full panel width, flush below the header
    DeferFixed(dwp, c.tabControl, 0, headerH_,
               static_cast<int>(rc.right - rc.left), tabH_);

    // Scrollable tab content
    int contentBottom = base;
    switch (activeTab_) {
    case 0: contentBottom = LayoutRosterPanel (dwp, c, px, pw, base, scrollUsed); break;
    case 1: contentBottom = LayoutRulesPanel  (dwp, c, px, pw, base, scrollUsed, state); break;
    case 2: contentBottom = LayoutArrangePanel(dwp, c, px, pw, base, scrollUsed); break;
    case 3: contentBottom = LayoutGroupsPanel (dwp, c, px, pw, base, scrollUsed); break;
    }

    UpdateControlVisibility(c, state.chartMode);

    if (dwp) EndDeferWindowPos(dwp);

    // Auto-resize ListView columns to fit available width
    ResizeListViewColumns(c, static_cast<int>(rc.right - rc.left));

    // Fixed elements always on top
    for (HWND h : {c.titleLabel, c.summaryLabel, c.statusLabel, c.footerMetaLabel, c.footerProgress, c.tabControl})
        if (h) SetWindowPos(h, HWND_TOP, 0, 0, 0, 0,
                            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    const int contentH = contentBottom - base + padding;
    UpdateScrollBar(sidebar, contentH, viewH);
    ClampScroll(sidebar);

    if (scroll_ != scrollUsed) {
        Recalculate(sidebar, state, c, renderer);
        return;
    }

    // Invalidate without forcing an erase-first on every child.  Dropping RDW_ERASE
    // prevents the white-flash each child produces during fast trackpad/touch scrolling.
    // Controls repaint at their new positions via the invalidation DeferWindowPos raises.
    RedrawWindow(sidebar, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

// ---------------------------------------------------------------------------
// Scroll helpers
// ---------------------------------------------------------------------------

void SidebarManager::ScrollTo(HWND sidebar, int newPos) {
    const int maxScroll = std::max(0, contentH_ - viewH_);
    newPos = std::clamp(newPos, 0, maxScroll);
    if (newPos == scroll_) return;

    scroll_ = newPos;

    // Just update the scrollbar position. Recalculate() (always called by the
    // caller immediately after) repositions all child HWNDs and redraws, so
    // ScrollWindowEx is unnecessary and only produces extra repaint artifacts.
    SCROLLINFO si{sizeof(si), SIF_POS, 0, 0, 0, scroll_};
    SetScrollInfo(sidebar, SB_VERT, &si, TRUE);
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
