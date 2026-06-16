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

// DeferScrollList — see header.  Positions the list at its FULL height and true
// (possibly negative or past-bottom) Y, then clips it to the visible scroll band
// with a window region.  Full height ⇒ all rows fit ⇒ no internal scrollbar; the
// region reveals only the band-slice at the correct content offset, so scrolling
// the sidebar scrolls the rows (top rows leave at the top, not the bottom).
void SidebarManager::DeferScrollList(HDWP& dwp, HWND child, int x, int y, int w, int h) {
    if (!child || !dwp) return;
    dwp = DeferWindowPos(dwp, child, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);

    // SetWindowRgn takes a region in window-local coords (0,0 = window top-left)
    // and assumes ownership (the OS frees the previous one), so there's no leak
    // and the region travels with the window when DeferWindowPos commits the move.
    if (viewH_ <= buttonH_) {            // viewport too small to bother clipping
        SetWindowRgn(child, nullptr, TRUE);
        return;
    }
    const int scrollBand = headerH_ + tabH_;
    const int scrollBot  = scrollBand + viewH_;
    const int top    = std::max(0, scrollBand - y);   // local y of band top
    const int bottom = std::min(h, scrollBot - y);    // local y of band bottom
    if (bottom <= top) {
        // Entirely outside the band — an empty region hides it completely.
        SetWindowRgn(child, CreateRectRgn(0, 0, 0, 0), TRUE);
    } else if (top == 0 && bottom == h) {
        // Entirely within the band — clear any prior clip so it shows in full.
        SetWindowRgn(child, nullptr, TRUE);
    } else {
        // Straddling an edge — show only the visible slice.
        SetWindowRgn(child, CreateRectRgn(0, top, w, bottom), TRUE);
    }
}

void SidebarManager::RebuildMetrics(HWND sidebar, const Renderer& renderer) {
    const WindowUiMetrics m = ComputeWindowUiMetrics(sidebar, renderer.UiFont());
    pad_ = m.pad;
    gap_ = m.gap;
    lineH_ = m.lineH;
    labelH_ = m.labelH;
    buttonH_ = m.buttonH;
    editH_ = m.editH;
    sectionGap_ = std::max(m.sectionGap, Scale(18));
    headerH_    = std::max(lineH_ * 5 + gap_ * 2, Scale(96));
    tabH_       = std::max(lineH_ + Scale(14), Scale(36)); // segmented workflow row
    statusH_    = std::max(lineH_ * 6 + gap_ * 4, Scale(104));
    scrollLine_ = buttonH_ + gap_;
}

namespace {

int DeskTagRuleCount(const AppState& state) {
    int count = 0;
    for (const auto& entry : state.studentInfo)
        count += static_cast<int>(entry.second.forbiddenDesks.size());
    return count;
}

} // namespace

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

    // Two-column student ListView — sized to fit its rows EXACTLY: no internal
    // scrollbar and no blank slack rows below the last student.  The sidebar's
    // own scroll handles navigation.  (ApproximateViewRect's HIWORD already
    // accounts for the header; adding lineH_ padding on top of it painted 2-3
    // empty "bars" after the roster.)
    rec(y);
    Defer(dwp, c.rosterListLabel, px, y, pw, labelH_); y += labelH_ + gap_;
    {
        const int nItems = c.rosterView ? ListView_GetItemCount(c.rosterView) : 0;
        const DWORD approx = (c.rosterView && nItems > 0)
            ? ListView_ApproximateViewRect(c.rosterView, pw, -1, nItems)
            : 0;
        const int listH = approx
            ? static_cast<int>(HIWORD(approx))
            : lineH_ + Scale(8); // empty roster: header only
        // Region-clip (not resize-clamp) so a tall roster scrolls its rows instead
        // of cutting the bottom ones off and growing its own scrollbars.
        DeferScrollList(dwp, c.rosterView, px, y, pw, listH); y += listH;

        // "+" button rendered as the table's final row — same width as the
        // table, one row tall.  Clicking it appends a student row and opens
        // the inline name editor.
        if (c.addStudentBtn) {
            const int rowH = buttonH_;
            Defer(dwp, c.addStudentBtn, px, y, pw, rowH); y += rowH;
        }
        y += gap_;
    }

    // Roster actions
    PlaceButtons(dwp, {c.showLastNamesBtn, c.assignSelectedRoster, c.bulkTag},
                 px, pw, y, Scale(100));
    y += sectionGap_;

    // Roster input / save
    rec(y);
    PlaceButtons(dwp, {c.importRoster, c.loadRoster, c.saveNow}, px, pw, y, Scale(80));
    y += sectionGap_;

    // Assignment actions
    PlaceButtons(dwp, {c.autoAssign}, px, pw, y, Scale(180));
    PlaceButtons(dwp, {c.quickFillSeats, c.clearAllSeats}, px, pw, y, Scale(110));
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

    // Dynamic height: size each list to show EVERY row it contains (SyncRulesLists
    // keeps the item count at max(3, ruleCount + 1), so an empty list still shows
    // 3 rows and the table simply grows as rules are added).  The list never needs
    // its own vertical scrollbar — the sidebar as a whole already scrolls.
    // ApproximateViewRect includes the header; do NOT add lineH_ padding on top
    // (that mistake previously made a "3-row" panel render ~5 visible rows).
    auto rulesListH = [&](HWND lv) -> int {
        int rows = 3;
        if (lv) {
            rows = std::max(3, static_cast<int>(ListView_GetItemCount(lv)));
            const DWORD approx = ListView_ApproximateViewRect(lv, pw, -1, rows);
            if (approx) return static_cast<int>(HIWORD(approx));
        }
        return lineH_ * (rows + 1) + Scale(8); // header + rows + small pad
    };

    // Keep Apart section — DeferScrollList so a tall rule list scrolls its rows
    // (same fix as the roster) instead of clipping the bottom + growing scrollbars.
    rec(y);
    Defer(dwp, c.keepApartHeader, px, y, pw, labelH_); y += labelH_ + gap_;
    Defer(dwp, c.keepApartDesc, px, y, pw, lineH_ * 2); y += lineH_ * 2 + gap_;
    {
        const int h = rulesListH(c.keepApartList);
        DeferScrollList(dwp, c.keepApartList, px, y, pw, h); y += h + gap_;
    }
    PlaceButtons(dwp, {c.addKeepApartBtn, c.remKeepApartBtn}, px, pw, y, Scale(80));
    y += sectionGap_;

    // Keep Together section
    rec(y);
    Defer(dwp, c.keepTogetherHeader, px, y, pw, labelH_); y += labelH_ + gap_;
    Defer(dwp, c.keepTogetherDesc, px, y, pw, lineH_ * 2); y += lineH_ * 2 + gap_;
    {
        const int h = rulesListH(c.keepTogetherList);
        DeferScrollList(dwp, c.keepTogetherList, px, y, pw, h); y += h + gap_;
    }
    PlaceButtons(dwp, {c.addKeepTogetherBtn, c.remKeepTogetherBtn}, px, pw, y, Scale(80));
    y += sectionGap_;

    const bool hasDeskRules = DeskTagRuleCount(state) > 0;
    rec(y);
    if (hasDeskRules) {
        Defer(dwp, c.deskTagHeader, px, y, pw, labelH_); y += labelH_ + gap_;
        const int deskListH = std::max(lineH_ * 4, Scale(60));
        Defer(dwp, c.deskTagList, px, y, pw, deskListH); y += deskListH + gap_;
        PlaceButtons(dwp, {c.addDeskTagRuleBtn, c.remDeskTagRuleBtn}, px, pw, y, Scale(92));
    } else {
        PlaceButtons(dwp, {c.addDeskTagRuleBtn}, px, pw, y, Scale(160));
    }
    y += sectionGap_;

    return y;
}

// ---------------------------------------------------------------------------
// LayoutArrangePanel  (tab 2)
// ---------------------------------------------------------------------------

int SidebarManager::LayoutArrangePanel(HDWP& dwp, const ControlHandles& c,
                                        int px, int pw, int base, int scrollUsed,
                                        const AppState& state) {
    int y = base;
    auto rec = [&](int absY) { sectionDividers_.push_back(absY + scrollUsed); };
    const bool hasItems = !state.layoutItems.empty();
    const bool hasAny = !state.selectedLayoutItems.empty();
    const bool hasItem = state.selectedLayoutItem.has_value() &&
        *state.selectedLayoutItem >= 0 &&
        *state.selectedLayoutItem < static_cast<int>(state.layoutItems.size());
    const bool hasSingleItem = hasItem && state.selectedLayoutItems.size() == 1;

    rec(y);
    Defer(dwp, c.layoutToolsLabel, px, y, pw, labelH_); y += labelH_ + gap_;
    PlaceButtons(dwp, {c.addSmartboard, c.addDesk, c.addTable, c.addBigTable},
                 px, pw, y, Scale(70));
    PlaceButtons(dwp, {c.addTrap, c.addTrapPair, c.addTrapPod, c.addBlock},
                 px, pw, y, Scale(70));
    y += sectionGap_;

    if (hasItems) {
        rec(y);
        PlaceButtons(dwp, {c.selectAllLayout, c.showAllObjects}, px, pw, y, Scale(110));
        y += sectionGap_;
    }

    if (hasAny) {
        rec(y);
        Defer(dwp, c.layoutTransformLabel, px, y, pw, labelH_); y += labelH_ + gap_;
        if (hasSingleItem) {
            PlaceButtons(dwp, {c.deleteLayout, c.duplicateLayoutItem, c.lockItem},
                         px, pw, y, Scale(80));
            PlaceButtons(dwp, {c.rotateCW, c.rotateCCW, c.flipH, c.toggleVisible},
                         px, pw, y, Scale(70));
            PlaceButtons(dwp, {c.sendLayoutBack, c.bringLayoutFront},
                         px, pw, y, Scale(100));
        } else {
            PlaceButtons(dwp, {c.deleteLayout, c.rotateCW, c.rotateCCW, c.flipH},
                         px, pw, y, Scale(70));
            PlaceButtons(dwp, {c.toggleVisible}, px, pw, y, Scale(120));
        }
        y += sectionGap_;
    }

    if (hasSingleItem) {
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
    }

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
    // "Groups of: [−] [2] [+]  or [label]" — stepper buttons replaced the old
    // dropdown, which could not be rendered readably in the dark sidebar.
    const int comboLblW = Scale(62);
    const int stepBtnW  = Scale(24);
    const int valW      = Scale(26);
    const int orLblW    = Scale(24);
    int x = px;
    Defer(dwp, c.groupSizeLabel, x, y, comboLblW, buttonH_); x += comboLblW + gap_;
    Defer(dwp, c.groupSizeMinus, x, y, stepBtnW,  buttonH_); x += stepBtnW + gap_;
    Defer(dwp, c.groupSizeValue, x, y + Scale(4), valW, buttonH_); x += valW + gap_;
    Defer(dwp, c.groupSizePlus,  x, y, stepBtnW,  buttonH_); x += stepBtnW + gap_ * 2;
    Defer(dwp, c.groupOrLabel,   x, y + Scale(4), orLblW, buttonH_); x += orLblW + gap_;
    Defer(dwp, c.groupOrValLabel, x, y + Scale(4),
          std::max(4, px + pw - x), buttonH_);
    y += buttonH_ + gap_ + sectionGap_;
    Defer(dwp, c.groupSummaryLabel, px, y, pw, lineH_ * 3); y += lineH_ * 3 + gap_;

    // Shuffle controls
    rec(y);
    PlaceButtons(dwp, {c.shuffleGroupsBtn, c.groupResetBtn}, px, pw, y, Scale(100));
    Defer(dwp, c.groupAvoidSameNumberCheck, px, y, pw, buttonH_); y += buttonH_ + gap_;
    Defer(dwp, c.groupAvoidSamePartnersCheck, px, y, pw, buttonH_); y += buttonH_ + gap_;
    Defer(dwp, c.groupAvoidSameFullGroupCheck, px, y, pw, buttonH_); y += buttonH_ + gap_;
    y += sectionGap_;

    // Keep Apart section (collapsed by default so students do not see active rules).
    // These lists are the SAME HWNDs the Rules tab shows full-height; route them
    // through DeferScrollList here too so any clip region the Rules tab set is
    // cleared (pairListH fits the band → region removed → normal internal scroll).
    rec(y);
    Defer(dwp, c.groupKeepApartToggle, px, y, pw, buttonH_); y += buttonH_ + gap_;
    if (!groupKeepApartCollapsed_) {
        Defer(dwp, c.groupApartSameChk, px, y, pw, buttonH_); y += buttonH_ + gap_;
        DeferScrollList(dwp, c.keepApartList, px, y, pw, pairListH); y += pairListH + gap_;
        PlaceButtons(dwp, {c.addKeepApartBtn, c.remKeepApartBtn}, px, pw, y, Scale(80));
        y += sectionGap_;
    }

    // Keep Together section
    rec(y);
    Defer(dwp, c.groupKeepTogetherToggle, px, y, pw, buttonH_); y += buttonH_ + gap_;
    if (!groupKeepTogetherCollapsed_) {
        Defer(dwp, c.groupTogetherSameChk, px, y, pw, buttonH_); y += buttonH_ + gap_;
        DeferScrollList(dwp, c.keepTogetherList, px, y, pw, pairListH); y += pairListH + gap_;
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

void SidebarManager::UpdateControlVisibility(const ControlHandles& c,
                                             const AppState& state) {
    const bool onRoster  = (activeTab_ == 0);
    const bool onRules   = (activeTab_ == 1);
    const bool onArrange = (activeTab_ == 2);
    const bool onGroups  = (activeTab_ == 3);
    const bool hasRoomItems = !state.layoutItems.empty();
    const bool hasAnySelection = !state.selectedLayoutItems.empty();
    const bool hasItemSelection = state.selectedLayoutItem.has_value() &&
        *state.selectedLayoutItem >= 0 &&
        *state.selectedLayoutItem < static_cast<int>(state.layoutItems.size());
    const bool hasSingleItemSelection =
        hasItemSelection && state.selectedLayoutItems.size() == 1;
    const bool hasDeskRules = DeskTagRuleCount(state) > 0;

    // Roster tab
    for (HWND h : {c.rosterView, c.addStudentBtn,
                   c.importRoster, c.loadRoster, c.saveNow,
                   c.autoAssign, c.quickFillSeats, c.clearAllSeats,
                   c.rosterListLabel,
                   c.assignSelectedRoster, c.bulkTag,
                   c.showLastNamesBtn})
        if (h) ShowWindow(h, onRoster ? SW_SHOW : SW_HIDE);
    if (c.rosterList) ShowWindow(c.rosterList, SW_HIDE);

    // Rules tab — the + Add buttons are intentionally omitted here; ghost rows handle
    // new-rule creation inline.  They are handled in the explicit blocks below so
    // they remain visible only in the Groups tab (where they're still needed).
    for (HWND h : {c.keepApartHeader, c.keepApartDesc, c.keepApartList,
                   c.remKeepApartBtn,
                   c.keepTogetherHeader, c.keepTogetherDesc, c.keepTogetherList,
                   c.remKeepTogetherBtn})
        if (h) ShowWindow(h, onRules ? SW_SHOW : SW_HIDE);
    for (HWND h : {c.deskTagHeader, c.deskTagList, c.remDeskTagRuleBtn})
        if (h) ShowWindow(h, (onRules && hasDeskRules) ? SW_SHOW : SW_HIDE);
    if (c.deskTagDesc) ShowWindow(c.deskTagDesc, SW_HIDE);
    if (c.addDeskTagRuleBtn) ShowWindow(c.addDeskTagRuleBtn, onRules ? SW_SHOW : SW_HIDE);

    // Arrange tab
    for (HWND h : {c.layoutToolsLabel, c.addSmartboard, c.addTrap, c.addDesk,
                   c.addTable, c.addBigTable, c.addBlock, c.addTrapPair, c.addTrapPod})
        if (h) ShowWindow(h, onArrange ? SW_SHOW : SW_HIDE);
    for (HWND h : {c.selectAllLayout, c.showAllObjects})
        if (h) ShowWindow(h, (onArrange && hasRoomItems) ? SW_SHOW : SW_HIDE);
    for (HWND h : {c.layoutTransformLabel, c.deleteLayout, c.rotateCW,
                   c.rotateCCW, c.flipH, c.toggleVisible})
        if (h) ShowWindow(h, (onArrange && hasAnySelection) ? SW_SHOW : SW_HIDE);
    for (HWND h : {c.duplicateLayoutItem, c.lockItem, c.sendLayoutBack,
                   c.bringLayoutFront})
        if (h) ShowWindow(h, (onArrange && hasSingleItemSelection) ? SW_SHOW : SW_HIDE);
    for (HWND h : {c.layoutInspectorLabel, c.layoutNameLabel, c.layoutLabelEdit,
                   c.layoutXLabel, c.layoutXEdit, c.layoutXSpin,
                   c.layoutYLabel, c.layoutYEdit, c.layoutYSpin,
                   c.layoutWidthLabel, c.layoutWidthEdit, c.layoutWSpin,
                   c.layoutHeightLabel, c.layoutHeightEdit, c.layoutHSpin,
                   c.layoutCapacityLabel, c.layoutCapacityEdit, c.applyLayoutItem})
        if (h) ShowWindow(h, (onArrange && hasSingleItemSelection) ? SW_SHOW : SW_HIDE);

    // Groups tab (the retired groupSizeCombo stays hidden everywhere)
    for (HWND h : {c.groupSizeLabel, c.groupSizeMinus, c.groupSizeValue, c.groupSizePlus,
                   c.groupOrLabel, c.groupOrValLabel, c.groupSummaryLabel,
                   c.shuffleGroupsBtn, c.groupResetBtn,
                   c.groupAvoidSameNumberCheck, c.groupAvoidSamePartnersCheck, c.groupAvoidSameFullGroupCheck,
                   c.groupKeepApartToggle, c.groupKeepTogetherToggle})
        if (h) ShowWindow(h, onGroups ? SW_SHOW : SW_HIDE);
    if (c.keepApartList) ShowWindow(c.keepApartList,
        (onRules || (onGroups && !groupKeepApartCollapsed_)) ? SW_SHOW : SW_HIDE);
    // "Same as seating" checkboxes: only in the expanded Groups rule sections.
    if (c.groupApartSameChk) ShowWindow(c.groupApartSameChk,
        (onGroups && !groupKeepApartCollapsed_) ? SW_SHOW : SW_HIDE);
    if (c.groupTogetherSameChk) ShowWindow(c.groupTogetherSameChk,
        (onGroups && !groupKeepTogetherCollapsed_) ? SW_SHOW : SW_HIDE);
    // + Add buttons: shown on the Rules tab (a discoverable picker dialog) and in
    // the Groups tab's expanded rule sections.
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

    // The bottom panel (status + counter + progress bar) exists ONLY on the
    // Groups tab (where it shows the possible-groupings counter) or while an
    // auto-assign is running (where it shows live progress).  Every other case
    // drops it entirely and reclaims the space for content (statusH = 0).
    const bool onGroupsTab = (activeTab_ == 3);
    const bool showFooter  = onGroupsTab || showFooterForAA_;
    const int  statusH     = showFooter ? statusH_ : 0;

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

    // Fixed: title, summary — bypass scrollable-region clamp
    DeferFixed(dwp, c.titleLabel,   padding, padding,                     pw, lineH_ * 2);
    DeferFixed(dwp, c.summaryLabel, padding, padding + lineH_ * 2 + gap_, pw, lineH_ * 2);
    // Footer (status + counter + progress) is placed and shown only when needed.
    if (showFooter) {
        const int footerTop    = std::max(padding, static_cast<int>(rc.bottom) - statusH + gap_);
        const int statusSlot   = lineH_ * 2;          // 2-line height for status text
        const int metaSlot     = lineH_ * 2;          // 2-line height for the counter
        DeferFixed(dwp, c.statusLabel,     padding, footerTop,                                    pw, statusSlot);
        DeferFixed(dwp, c.footerMetaLabel, padding, footerTop + statusSlot + gap_,               pw, metaSlot);
        DeferFixed(dwp, c.footerProgress,  padding, footerTop + statusSlot + gap_ + metaSlot + gap_, pw, std::max(Scale(12), lineH_));
    }
    for (HWND h : {c.statusLabel, c.footerMetaLabel, c.footerProgress})
        if (h) ShowWindow(h, showFooter ? SW_SHOW : SW_HIDE);

    // Fixed: tab control spans the full panel width, flush below the header
    DeferFixed(dwp, c.tabControl, 0, headerH_,
               static_cast<int>(rc.right - rc.left), tabH_);

    // Scrollable tab content
    int contentBottom = base;
    switch (activeTab_) {
    case 0: contentBottom = LayoutRosterPanel (dwp, c, px, pw, base, scrollUsed); break;
    case 1: contentBottom = LayoutRulesPanel  (dwp, c, px, pw, base, scrollUsed, state); break;
    case 2: contentBottom = LayoutArrangePanel(dwp, c, px, pw, base, scrollUsed, state); break;
    case 3: contentBottom = LayoutGroupsPanel (dwp, c, px, pw, base, scrollUsed); break;
    }

    UpdateControlVisibility(c, state);

    if (dwp) EndDeferWindowPos(dwp);

    // Belt-and-suspenders: after Defer may have shrunk a ListView during scroll,
    // force all sidebar ListViews to hide both scrollbars.  LVS_NOSCROLL prevents
    // the control from *adding* scrollbars internally, but if the OS briefly set
    // one during the resize window (e.g. during DeferWindowPos commit), this hides
    // it immediately.  This is called after EndDeferWindowPos so sizes are final.
    // Roster and desk-tag list: sidebar owns scrolling — suppress both internal scrollbars.
    for (HWND lv : {c.rosterView, c.deskTagList}) {
        if (lv) {
            ShowScrollBar(lv, SB_VERT, FALSE);
            ShowScrollBar(lv, SB_HORZ, FALSE);
        }
    }
    // Rule lists: fixed 3-row height with internal vertical scroll — suppress only horizontal.
    for (HWND lv : {c.keepApartList, c.keepTogetherList}) {
        if (lv) ShowScrollBar(lv, SB_HORZ, FALSE);
    }

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

    // Repaint the panel and every child SYNCHRONOUSLY (RDW_UPDATENOW) in the same
    // pass that repositioned them.  The child controls are moved by DeferWindowPos
    // and appear at their new spots immediately, but the parent-drawn section
    // dividers + background live in the sidebar's own WM_PAINT.  Without an
    // immediate paint, RDW_INVALIDATE only *queues* WM_PAINT, so on a fast
    // touch/trackpad pan many reposition frames land before a single repaint —
    // the dividers visibly lag the controls (the "weird scroll artifacts").
    // UPDATENOW forces dividers + controls to update together, every frame.
    // RDW_ERASE stays OFF: PaintInfoPanel fills the whole client itself, and the
    // erase would only add the white flash the previous code rightly avoided.
    RedrawWindow(sidebar, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
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
    // Accumulate the raw delta first.  A classic wheel sends ±WHEEL_DELTA (120)
    // per notch, but a precision touchpad sends a stream of much smaller deltas
    // (±8, ±16, …).  The old `wheelDelta / WHEEL_DELTA` integer-divided those to
    // zero, so two-finger trackpad scrolling did nothing.  One full notch still
    // scrolls exactly `scrollLine_` pixels; sub-notch deltas accumulate until they
    // add up to at least one pixel, and the leftover is carried to the next event.
    wheelAccum_ += wheelDelta;
    const int pixels = wheelAccum_ * scrollLine_ / WHEEL_DELTA;
    if (pixels == 0) return; // not enough accumulated yet — keep the residual
    wheelAccum_ -= pixels * WHEEL_DELTA / scrollLine_; // retain the sub-pixel remainder
    ScrollTo(sidebar, scroll_ - pixels);
    Recalculate(sidebar, state, c, renderer);
}
