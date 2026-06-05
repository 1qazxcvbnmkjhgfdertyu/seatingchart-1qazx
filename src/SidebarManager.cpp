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
// Any control whose Y range overlaps the fixed header (y < headerH_) or extends
// past the scrollable bottom (y + h > headerH_ + viewH_) is moved above y = 0.
// Win32 clips child windows to the parent's client area, so y < 0 means invisible —
// the control is never destroyed, just temporarily out of view.
void SidebarManager::Defer(HDWP& dwp, HWND child, int x, int y, int w, int h) {
    if (!child || !dwp) return;
    if (viewH_ > buttonH_) { // scrollable region is meaningful
        const int scrollBand = headerH_ + tabH_; // content starts below header + tab strip
        const int scrollBot  = scrollBand + viewH_;
        // Only hide when completely outside the visible band — partial overlap is fine;
        // Win32 clips the child to the parent client area and the fixed-header controls
        // (tab strip, title) paint on top due to higher Z-order.
        if (y + h <= scrollBand || y >= scrollBot) {
            dwp = DeferWindowPos(dwp, child, nullptr, x, -(h + 4), w, h,
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
    statusH_    = std::max(lineH_ * 5 + gap_ * 4, Scale(86));
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

    // Two-column student ListView — fills most of the panel
    rec(y);
    const int listH = std::max(lineH_ * 8, viewH_ - Scale(160));
    Defer(dwp, c.rosterView, px, y, pw, std::max(Scale(60), listH)); y += std::max(Scale(60), listH) + gap_;

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
                                      int px, int pw, int base, int scrollUsed) {
    int y = base;
    auto rec = [&](int absY) { sectionDividers_.push_back(absY + scrollUsed); };
    const int pairListH = std::max(lineH_ * 4, Scale(60));

    // Keep Apart section
    rec(y);
    Defer(dwp, c.keepApartHeader, px, y, pw, labelH_); y += labelH_ + gap_;
    Defer(dwp, c.keepApartDesc,   px, y, pw, labelH_); y += labelH_ + gap_;
    Defer(dwp, c.keepApartList,   px, y, pw, pairListH); y += pairListH + gap_;
    PlaceButtons(dwp, {c.addKeepApartBtn, c.remKeepApartBtn}, px, pw, y, Scale(80));
    y += sectionGap_;

    // Keep Together section
    rec(y);
    Defer(dwp, c.keepTogetherHeader, px, y, pw, labelH_); y += labelH_ + gap_;
    Defer(dwp, c.keepTogetherDesc,   px, y, pw, labelH_); y += labelH_ + gap_;
    Defer(dwp, c.keepTogetherList,   px, y, pw, pairListH); y += pairListH + gap_;
    PlaceButtons(dwp, {c.addKeepTogetherBtn, c.remKeepTogetherBtn}, px, pw, y, Scale(80));
    y += sectionGap_;

    // Desk Tag Rules placeholder
    rec(y);
    Defer(dwp, c.deskTagHeader, px, y, pw, labelH_); y += labelH_ + gap_;
    Defer(dwp, c.deskTagDesc,   px, y, pw, labelH_ * 2); y += labelH_ * 2 + gap_;
    Defer(dwp, c.deskTagList,   px, y, pw, pairListH); y += pairListH + gap_;
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
    PlaceButtons(dwp, {c.quickFillSeats}, px, pw, y, Scale(120));
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

    // Rules tab
    for (HWND h : {c.keepApartHeader, c.keepApartDesc, c.keepApartList,
                   c.addKeepApartBtn, c.remKeepApartBtn,
                   c.keepTogetherHeader, c.keepTogetherDesc, c.keepTogetherList,
                   c.addKeepTogetherBtn, c.remKeepTogetherBtn,
                   c.deskTagHeader, c.deskTagDesc, c.deskTagList,
                   c.addDeskTagRuleBtn, c.remDeskTagRuleBtn})
        if (h) ShowWindow(h, onRules ? SW_SHOW : SW_HIDE);

    // Arrange tab
    for (HWND h : {c.layoutToolsLabel, c.addSmartboard, c.addTrap, c.addDesk,
                   c.addTable, c.addBigTable, c.addBlock, c.addTrapPair, c.addTrapPod,
                   c.layoutTransformLabel, c.deleteLayout, c.duplicateLayoutItem, c.lockItem,
                   c.rotateCW, c.rotateCCW, c.flipH, c.selectAllLayout,
                   c.toggleVisible, c.sendLayoutBack, c.bringLayoutFront,
                   c.quickFillSeats})
        if (h) ShowWindow(h, onArrange ? SW_SHOW : SW_HIDE);

    // Groups tab
    for (HWND h : {c.groupSizeLabel, c.groupSizeCombo, c.groupOrLabel, c.groupOrValLabel, c.groupSummaryLabel,
                   c.shuffleGroupsBtn, c.groupResetBtn,
                   c.groupAvoidSameNumberCheck, c.groupAvoidSamePartnersCheck, c.groupAvoidSameFullGroupCheck,
                   c.groupKeepApartToggle, c.groupKeepTogetherToggle})
        if (h) ShowWindow(h, onGroups ? SW_SHOW : SW_HIDE);
    if (c.keepApartList) ShowWindow(c.keepApartList,
        (onRules || (onGroups && !groupKeepApartCollapsed_)) ? SW_SHOW : SW_HIDE);
    if (c.addKeepApartBtn) ShowWindow(c.addKeepApartBtn,
        (onRules || (onGroups && !groupKeepApartCollapsed_)) ? SW_SHOW : SW_HIDE);
    if (c.remKeepApartBtn) ShowWindow(c.remKeepApartBtn,
        (onRules || (onGroups && !groupKeepApartCollapsed_)) ? SW_SHOW : SW_HIDE);
    if (c.keepTogetherList) ShowWindow(c.keepTogetherList,
        (onRules || (onGroups && !groupKeepTogetherCollapsed_)) ? SW_SHOW : SW_HIDE);
    if (c.addKeepTogetherBtn) ShowWindow(c.addKeepTogetherBtn,
        (onRules || (onGroups && !groupKeepTogetherCollapsed_)) ? SW_SHOW : SW_HIDE);
    if (c.remKeepTogetherBtn) ShowWindow(c.remKeepTogetherBtn,
        (onRules || (onGroups && !groupKeepTogetherCollapsed_)) ? SW_SHOW : SW_HIDE);

    // Always hidden — old text-box controls replaced by structured UI
    for (HWND h : {c.rosterEdit, c.rosterFilter, c.restrictionEdit, c.applyRules,
                   c.rosterLabel,
                   // Add/Remove buttons replaced by right-click context menu
                   c.addStudentBtn, c.removeStudentBtn,
                   // Bottom edit fields replaced by true inline cell editing
                   c.inlineFirstEdit, c.inlineLastEdit, c.saveStudentEdit,
                   // Export/template buttons removed from Arrange tab (still in File menu)
                   c.captureChart, c.exportChart, c.printChart, c.exportCsv,
                   c.seatingReport, c.exportHtml, c.saveTemplateBtn, c.loadTemplateBtn,
                   // Old groups size spinner replaced by combobox
                   c.groupSizeEdit, c.groupSizeSpin, c.groupConfigList,
                   // Retired text/list based groups rule UI
                   c.groupsOutputList, c.groupRulesLabel, c.groupRulesEdit, c.groupRulesApply})
        if (h) ShowWindow(h, SW_HIDE);

    // Always hidden — retired sidebar controls
    for (HWND h : {c.modeLabel, c.seatMode, c.layoutMode,
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
    const int footerTop = std::max(padding, static_cast<int>(rc.bottom) - statusH + gap_);
    DeferFixed(dwp, c.statusLabel,  padding, footerTop, pw, lineH_ + gap_);
    DeferFixed(dwp, c.footerMetaLabel, padding, footerTop + lineH_ + gap_, pw, lineH_ + gap_);
    DeferFixed(dwp, c.footerProgress, padding, footerTop + (lineH_ + gap_) * 2, pw, std::max(Scale(12), lineH_));

    // Fixed: tab control spans the full panel width, flush below the header
    DeferFixed(dwp, c.tabControl, 0, headerH_,
               static_cast<int>(rc.right - rc.left), tabH_);

    // Scrollable tab content
    int contentBottom = base;
    switch (activeTab_) {
    case 0: contentBottom = LayoutRosterPanel (dwp, c, px, pw, base, scrollUsed); break;
    case 1: contentBottom = LayoutRulesPanel  (dwp, c, px, pw, base, scrollUsed); break;
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
