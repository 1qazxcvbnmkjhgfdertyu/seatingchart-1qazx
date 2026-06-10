#include "Controls.h"
#include "Renderer.h"
#include "Utils.h"
#include <algorithm>
#include <commctrl.h>

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
    c.footerMetaLabel = MakeLabel(p, L"Permutations left: —");
    c.footerProgress  = CreateWindowExW(0, PROGRESS_CLASS, nullptr,
        WS_CHILD | WS_VISIBLE,
        0,0,0,0, p, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (c.footerProgress) {
        SendMessageW(c.footerProgress, PBM_SETRANGE32, 0, 100);
        SendMessageW(c.footerProgress, PBM_SETPOS, 0, 0);
    }

    // ---- Roster tab --------------------------------------------------------
    c.importRoster    = MakeButton(p, L"Import",    kImportRosterId);
    c.loadRoster      = MakeButton(p, L"Load File", kLoadRosterId);
    c.saveNow         = MakeButton(p, L"Save Now",  kSaveNowId);
    c.autoAssign      = MakeButton(p, L"Smart Auto-Assign (Rules)", kAutoAssignId);
    c.clearAllSeats   = MakeButton(p, L"Clear all seats", kClearAllSeatsId);
    c.rosterListLabel = MakeLabel(p, L"Roster");
    c.rosterList      = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_EXTENDEDSEL,
        0,0,0,0, p, reinterpret_cast<HMENU>(kRosterListId),
        GetModuleHandleW(nullptr), nullptr);
    c.assignSelectedRoster = MakeButton(p, L"Assign Selected", kAssignSelectedRosterId);
    c.bulkTag              = MakeButton(p, L"Bulk Tag",        kBulkTagId);

    // Two-column student ListView
    c.rosterView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kRosterViewId)),
        GetModuleHandleW(nullptr), nullptr);
    if (c.rosterView) {
        ListView_SetExtendedListViewStyle(c.rosterView,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW lvc{};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
        lvc.fmt  = LVCFMT_CENTER;
        lvc.iSubItem = 0; lvc.cx = Scale(28);
        lvc.pszText = const_cast<wchar_t*>(L"#");
        ListView_InsertColumn(c.rosterView, 0, &lvc);
        // Win32 ignores LVCFMT_CENTER on column 0 at insert time; apply it again
        // after insertion — this is honoured on Windows Vista+.
        lvc.mask = LVCF_FMT; lvc.fmt = LVCFMT_CENTER;
        ListView_SetColumn(c.rosterView, 0, &lvc);
        lvc.iSubItem = 1; lvc.cx = Scale(100);
        lvc.pszText = const_cast<wchar_t*>(L"First Name");
        ListView_InsertColumn(c.rosterView, 1, &lvc);
        lvc.iSubItem = 2; lvc.cx = Scale(100);
        lvc.pszText = const_cast<wchar_t*>(L"Last Name");
        ListView_InsertColumn(c.rosterView, 2, &lvc);
    }
    c.showLastNamesBtn = MakeButton(p, L"Show Last Names", kShowLastNamesId,
                                    BS_AUTOCHECKBOX | BS_PUSHLIKE);

    // ---- Rules tab ---------------------------------------------------------
    c.keepApartHeader = MakeLabel(p, L"Keep Apart Rules");
    c.keepApartDesc   = MakeLabel(p, L"Keep students away from each other.");
    c.keepApartList   = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSCROLL,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kKeepApartListId)),
        GetModuleHandleW(nullptr), nullptr);
    if (c.keepApartList) {
        ListView_SetExtendedListViewStyle(c.keepApartList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
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
    c.addKeepApartBtn = MakeButton(p, L"+ Add",  kAddKeepApartId);
    c.remKeepApartBtn = MakeButton(p, L"Remove", kRemKeepApartId);

    c.keepTogetherHeader = MakeLabel(p, L"Keep Together Rules");
    c.keepTogetherDesc   = MakeLabel(p, L"Keep groups of students near each other.");
    c.keepTogetherList   = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSCROLL,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kKeepTogetherListId)),
        GetModuleHandleW(nullptr), nullptr);
    if (c.keepTogetherList) {
        ListView_SetExtendedListViewStyle(c.keepTogetherList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
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
    c.addKeepTogetherBtn = MakeButton(p, L"+ Add",  kAddKeepTogetherId);
    c.remKeepTogetherBtn = MakeButton(p, L"Remove", kRemKeepTogetherId);

    c.deskTagHeader = MakeLabel(p, L"Desk Tag Rules");
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
    c.addDeskTagRuleBtn = MakeButton(p, L"+ Tag Rule", kAddDeskTagRuleId);
    c.remDeskTagRuleBtn = MakeButton(p, L"Remove",     kRemDeskTagRuleId);

    // ---- Arrange tab -------------------------------------------------------
    c.layoutToolsLabel = MakeLabel(p, L"Furniture");
    c.addSmartboard    = MakeButton(p, L"Smartboard",    kAddSmartboardId);
    c.addTrap          = MakeButton(p, L"Trapezoid",     kAddTrapezoidId);
    c.addDesk          = MakeButton(p, L"Desk",          kAddDeskId);
    c.addTable         = MakeButton(p, L"Table (4)",     kAddTableId);
    c.addBigTable      = MakeButton(p, L"Big Table",     kAddBigTableId);
    c.addBlock         = MakeButton(p, L"Block",         kAddBlockId);
    c.addTrapPair      = MakeButton(p, L"Trap Pair (2)", kAddTrapPairId);
    c.addTrapPod       = MakeButton(p, L"Trap Pod (4)",  kAddTrapPodId);

    c.layoutTransformLabel = MakeLabel(p, L"Arrangement Tools");
    c.deleteLayout        = MakeButton(p, L"Delete",       kDeleteLayoutItemId);
    c.duplicateLayoutItem = MakeButton(p, L"Duplicate",    kDuplicateLayoutItemId);
    c.lockItem            = MakeButton(p, L"Lock",         kLockItemId);
    c.rotateCW            = MakeButton(p, L"Rot ↻",   kRotateCWId);
    c.rotateCCW           = MakeButton(p, L"Rot ↺",   kRotateCCWId);
    c.flipH               = MakeButton(p, L"Flip H",       kFlipHId);
    c.selectAllLayout     = MakeButton(p, L"Select All",   kSelectAllLayoutId);
    c.toggleVisible       = MakeButton(p, L"Toggle Visible", kToggleVisibleId);
    c.sendLayoutBack      = MakeButton(p, L"Send to Back",   kSendLayoutBackId);
    c.bringLayoutFront    = MakeButton(p, L"Bring to Front", kBringLayoutFrontId);
    c.quickFillSeats      = MakeButton(p, L"Quick Fill",     kQuickFillSeatsId);
    c.showAllObjects      = MakeButton(p, L"Show All Objects", kShowAllObjectsId);

    // ---- Layout inspector (Arrange tab) ------------------------------------
    c.layoutInspectorLabel = MakeLabel(p, L"Selected Object");
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
    c.applyLayoutItem      = MakeButton(p, L"Apply Changes", kApplyLayoutItemId);

    // ---- Groups tab --------------------------------------------------------
    c.groupSizeLabel    = MakeLabel(p, L"Groups of:");
    c.groupSizeCombo    = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kGroupSizeComboId)),
        GetModuleHandleW(nullptr), nullptr);
    c.groupOrLabel      = MakeLabel(p, L"or");
    c.groupOrValLabel   = MakeLabel(p, L"—");
    c.groupSummaryLabel = MakeLabel(p,
        L"Pick a group size to see the active pattern and exact count.");
    c.shuffleGroupsBtn  = MakeButton(p, L"Shuffle Groups",      kShuffleGroupsId);
    c.groupResetBtn     = MakeButton(p, L"Reset Shuffle Memory", kGroupResetId);
    c.groupKeepApartToggle    = MakeButton(p, L"Show Keep Apart Rules",
                                           kGroupKeepApartToggleId);
    c.groupKeepTogetherToggle = MakeButton(p, L"Show Keep Together Rules",
                                           kGroupKeepTogetherToggleId);
    c.groupAvoidSameNumberCheck = CreateWindowExW(
        0, L"BUTTON", L"Avoid same numbered group",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kGroupAvoidSameNumberId)),
        GetModuleHandleW(nullptr), nullptr);
    c.groupAvoidSamePartnersCheck = CreateWindowExW(
        0, L"BUTTON", L"Allow at most 1 repeated classmate",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kGroupAvoidSamePartnersId)),
        GetModuleHandleW(nullptr), nullptr);
    c.groupAvoidSameFullGroupCheck = CreateWindowExW(
        0, L"BUTTON", L"Avoid exact same full group",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kGroupAvoidSameFullGroupId)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c.groupAvoidSameNumberCheck,   BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(c.groupAvoidSamePartnersCheck, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(c.groupAvoidSameFullGroupCheck,BM_SETCHECK, BST_CHECKED, 0);

    // ---- Tab control -------------------------------------------------------
    c.tabControl = CreateWindowExW(0, WC_TABCONTROL, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_HOTTRACK,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kTabControlId)),
        GetModuleHandleW(nullptr), nullptr);
    if (c.tabControl) {
        TCITEMW ti{};
        ti.mask = TCIF_TEXT;
        ti.pszText = const_cast<wchar_t*>(L"Roster");  TabCtrl_InsertItem(c.tabControl, 0, &ti);
        ti.pszText = const_cast<wchar_t*>(L"Rules");   TabCtrl_InsertItem(c.tabControl, 1, &ti);
        ti.pszText = const_cast<wchar_t*>(L"Arrange"); TabCtrl_InsertItem(c.tabControl, 2, &ti);
        ti.pszText = const_cast<wchar_t*>(L"Groups");  TabCtrl_InsertItem(c.tabControl, 3, &ti);
        TabCtrl_SetCurSel(c.tabControl, 2);
    }

    // ---- Tooltips ----------------------------------------------------------
    HWND tip = CreateTooltipWnd(p);

    AddTip(tip, c.importRoster,
        L"Parse the text box above and apply as the roster\n"
        L"One name per line. Duplicates are flagged.");
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
    AddTip(tip, c.assignSelectedRoster,
        L"Assign the selected roster name to the focused seat on the chart\n"
        L"Tip: click a seat on the chart first, then select a name here");
    AddTip(tip, c.bulkTag,
        L"Apply or remove a tag for all selected students in the roster list\n"
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
        L"Clear remembered shuffle history so the next group shuffle can reuse the same pairings");
    AddTip(tip, c.groupAvoidSameNumberCheck,
        L"When checked, a student cannot return to the same numbered group they have already used");
    AddTip(tip, c.groupAvoidSamePartnersCheck,
        L"When checked, a student may repeat with at most one classmate from any prior group");
    AddTip(tip, c.groupAvoidSameFullGroupCheck,
        L"When checked, the app will not reuse an identical full group composition from shuffle history");
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
        c.rosterListLabel, c.rosterList, c.rosterView,
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
        c.groupOrLabel, c.groupOrValLabel, c.groupSummaryLabel,
        c.shuffleGroupsBtn, c.groupResetBtn,
        c.groupKeepApartToggle, c.groupKeepTogetherToggle,
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

    // Ghost row — clicking its name cell starts inline add without any button press.
    {
        const int ghostIdx = static_cast<int>(s.roster.size());
        LVITEMW lvi{};
        lvi.mask     = LVIF_TEXT;
        lvi.iItem    = ghostIdx;
        lvi.iSubItem = 0;
        std::wstring num = std::to_wstring(ghostIdx + 1);
        lvi.pszText  = num.data();
        const int idx = ListView_InsertItem(c.rosterView, &lvi);
        if (idx >= 0) {
            wchar_t empty[] = L"";
            ListView_SetItemText(c.rosterView, idx, 1, empty);
            ListView_SetItemText(c.rosterView, idx, 2, empty);
        }
    }
}

void SyncRulesLists(const AppState& s, const ControlHandles& c) {
    // Ghost rows: total displayed = max(3, realCount + 1) so there is always at
    // least one empty row to click into.  When all visible rows are filled the
    // +1 ensures a fresh blank row appears at the bottom automatically.
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
    fillList(c.keepApartList,    s.restrictions);
    fillList(c.keepTogetherList, s.affinities);

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
    if (c.titleLabel) SetWindowTextW(c.titleLabel, L"Seating Chart");
    if (c.summaryLabel) {
        int layoutSeats = 0, layoutAssigned = 0;
        for (const auto& item : s.layoutItems) {
            layoutSeats += LayoutItemSeats(item);
            for (const auto& occ : item.occupants) if (!occ.empty()) ++layoutAssigned;
        }
        const bool isLayout = (s.chartMode == ChartMode::Layout);

        std::wstring line1 = isLayout ? L"Arrange" : L"Assign";
        line1 += L"  \xB7  " + std::to_wstring(static_cast<int>(s.layoutItems.size()))
              + L" items";
        if (layoutSeats > 0) {
            line1 += L"  \xB7  "
                  + std::to_wstring(layoutAssigned) + L"/" + std::to_wstring(layoutSeats)
                  + L" seats";
            if (!s.roster.empty()) {
                const int unassigned = static_cast<int>(s.roster.size()) - layoutAssigned;
                if (unassigned > 0)
                    line1 += L"  (" + std::to_wstring(unassigned) + L" unplaced)";
            }
        }

        std::wstring line2;
        if (!s.roster.empty())
            line2 += L"Roster " + std::to_wstring(s.roster.size());
        if (!s.restrictions.empty()) {
            if (!line2.empty()) line2 += L"  \xB7  ";
            line2 += std::to_wstring(s.restrictions.size()) + L" rules";
        }
        if (s.lastAffinitySatisfaction > 0.0) {
            if (!line2.empty()) line2 += L"  \xB7  ";
            int pct = static_cast<int>(s.lastAffinitySatisfaction * 100.0 + 0.5);
            line2 += L"Affinity " + std::to_wstring(pct) + L"%";
        }

        const std::wstring sum = line2.empty() ? line1 : (line1 + L"\n" + line2);
        SetWindowTextW(c.summaryLabel, sum.c_str());
    }
    if (c.statusLabel) SetWindowTextW(c.statusLabel, s.status.c_str());
    // roomWidthEdit / roomHeightEdit / frontEdgeButton are retired (always nullptr).
}

void UpdateButtonState(const AppState& s, const ControlHandles& c, bool aaRunning) {
    const BOOL seats        = (s.chartMode == ChartMode::Seats);
    const BOOL layout       = (s.chartMode == ChartMode::Layout);
    const BOOL hasRoster    = !s.roster.empty();
    const BOOL hasSeats     = TotalLayoutSeats(s.layoutItems) > 0;
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
    EnableWindow(c.rosterList,    seats);
    EnableWindow(c.autoAssign,    seats && hasRoster && hasSeats && !aaRunning);
    EnableWindow(c.quickFillSeats, seats && hasRoster && hasSeats);
    EnableWindow(c.clearAllSeats,  seats && hasSeats);

    const bool rosterSel = c.rosterList &&
        SendMessageW(c.rosterList, LB_GETCURSEL, 0, 0) != LB_ERR;
    EnableWindow(c.assignSelectedRoster, seats && hasFocusSeat && rosterSel);
    bool hasRosterSel = false;
    if (c.rosterList) {
        int count = static_cast<int>(SendMessageW(c.rosterList, LB_GETSELCOUNT, 0, 0));
        hasRosterSel = count > 0;
    }
    EnableWindow(c.bulkTag, seats && hasRosterSel);

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
