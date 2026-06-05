#include "Controls.h"
#include "Renderer.h"
#include "Utils.h"
#include <algorithm>
#include <commctrl.h>

// ---------------------------------------------------------------------------
// Factory helpers
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Tooltip helpers
//
// CreateTooltipWnd  — creates a shared TOOLTIPS_CLASS window for the sidebar.
// AddTip            — registers one control's tooltip text with that window.
//
// Rationale (ISTE Teacher Ready 1.4 Interface Design + 1.5 Discoverability):
//   Teachers don't have time to explore — tooltips surface feature explanations
//   at the moment of need without adding visual clutter to the sidebar.
// ---------------------------------------------------------------------------

static HWND CreateTooltipWnd(HWND parent) {
    HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (tip) {
        SendMessageW(tip, TTM_SETMAXTIPWIDTH, 0, Scale(320));
        // Keep tooltip visible long enough to read multi-line text (8 s).
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
static HWND MakeRadio(HWND p, const wchar_t* text, int id, DWORD extra = 0) {
    return CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | BS_PUSHLIKE | extra, 0,0,0,0, p,
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

// ---------------------------------------------------------------------------
// CreateAllUIControls
// ---------------------------------------------------------------------------

void CreateAllUIControls(HWND parent, ControlHandles& c) {
    c.sidebar = CreateWindowExW(0, L"SeatingChartSidebar", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
        0,0,0,0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);

    HWND p = c.sidebar ? c.sidebar : parent;

    c.titleLabel   = MakeLabel(p, L"Seating Chart");
    c.summaryLabel = MakeLabel(p, L"");
    c.statusLabel  = MakeLabel(p, L"Ready");
    c.footerMetaLabel = MakeLabel(p, L"Permutations left: —");
    c.footerProgress = CreateWindowExW(0, PROGRESS_CLASS, nullptr,
        WS_CHILD | WS_VISIBLE,
        0,0,0,0, p, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (c.footerProgress) {
        SendMessageW(c.footerProgress, PBM_SETRANGE32, 0, 100);
        SendMessageW(c.footerProgress, PBM_SETPOS, 0, 0);
    }

    c.modeLabel    = MakeLabel(p, L"");
    c.layoutMode   = MakeRadio(p, L"Arrange", kLayoutModeId, WS_GROUP);
    c.seatMode     = MakeRadio(p, L"Assign",  kSeatModeId);
    c.captureChart  = MakeButton(p, L"Copy",    kCaptureChartId);
    c.exportChart   = MakeButton(p, L"PNG",     kExportChartId);
    c.printChart    = MakeButton(p, L"Print",   kPrintChartId);
    c.exportCsv     = MakeButton(p, L"CSV",     kExportCsvId);
    c.seatingReport = MakeButton(p, L"Report",  kSeatingReportId);
    c.exportHtml    = MakeButton(p, L"HTML",    kExportHtmlId);

    c.rosterLabel     = MakeLabel(p, L"Roster input");
    c.rosterEdit      = MakeEdit(p, kRosterEditId, ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
    c.importRoster    = MakeButton(p, L"Import",    kImportRosterId);
    c.loadRoster      = MakeButton(p, L"Load File", kLoadRosterId);
    c.saveNow         = MakeButton(p, L"Save Now",  kSaveNowId);
    c.autoAssign      = MakeButton(p, L"Smart Auto-Assign (Rules)", kAutoAssignId);
    c.quickFillSeats  = MakeButton(p, L"Quick Fill",    kQuickFillSeatsId);
    c.clearAllSeats   = MakeButton(p, L"Clear all seats", kClearAllSeatsId);
    c.rosterFilter    = MakeEdit(p, kRosterFilterId, ES_AUTOHSCROLL);
    // Cue-banner hint text (ISTE 1.5 Discoverability — teachers need to know
    // what to type here without reading a manual).
    SendMessageW(c.rosterFilter, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"Search name or tag…"));
    c.rosterListLabel = MakeLabel(p, L"Roster");
    c.rosterList      = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_EXTENDEDSEL,
        0,0,0,0, p, reinterpret_cast<HMENU>(kRosterListId),
        GetModuleHandleW(nullptr), nullptr);
    c.assignSelectedRoster = MakeButton(p, L"Assign Selected", kAssignSelectedRosterId);
    c.bulkTag = MakeButton(p, L"Bulk Tag", kBulkTagId);
    c.restrictionLabel = MakeLabel(p, L"Rules: A|B apart, A+B near @W weight, A==B together, Group: names (clusters)");
    c.restrictionEdit  = MakeEdit(p, kRestrictionEditId, ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
    c.applyRules       = MakeButton(p, L"Apply Rules", kApplyRulesId);
    c.saveTemplateBtn   = MakeButton(p, L"Save Tmpl", kSaveTemplateId);
    c.loadTemplateBtn   = MakeButton(p, L"Load Tmpl", kLoadTemplateId);

    // Layout mode — add items
    c.layoutToolsLabel    = MakeLabel(p, L"Furniture");
    c.addSmartboard       = MakeButton(p, L"Smartboard",  kAddSmartboardId);
    c.addTrap             = MakeButton(p, L"Trapezoid",   kAddTrapezoidId);
    c.addDesk             = MakeButton(p, L"Desk",        kAddDeskId);
    c.addTable            = MakeButton(p, L"Table (4)",   kAddTableId);
    c.addBigTable         = MakeButton(p, L"Big Table",   kAddBigTableId);
    c.addBlock            = MakeButton(p, L"Block",        kAddBlockId);
    c.addTrapPair         = MakeButton(p, L"Trap Pair (2)", kAddTrapPairId);
    c.addTrapPod          = MakeButton(p, L"Trap Pod (4)",  kAddTrapPodId);
    c.deleteLayout        = MakeButton(p, L"Delete",      kDeleteLayoutItemId);
    c.mergeSelected       = MakeButton(p, L"Merge blocks", kMergeSelectedId);
    c.presetRows          = MakeButton(p, L"Preset: Rows", kPresetRowsId);
    c.presetU             = MakeButton(p, L"Preset: U", kPresetUId);
    c.presetHorseshoe     = MakeButton(p, L"Preset: Horseshoe", kPresetHorseshoeId);
    c.toggleVisible       = MakeButton(p, L"Toggle Visible", kToggleVisibleId);

    // Layout mode — transform tools
    c.layoutTransformLabel = MakeLabel(p, L"Arrangement Tools");
    c.rotateCW    = MakeButton(p, L"Rot ↻",  kRotateCWId);
    c.rotateCCW   = MakeButton(p, L"Rot ↺",  kRotateCCWId);
    c.flipH       = MakeButton(p, L"Flip H",      kFlipHId);
    c.lockItem    = MakeButton(p, L"Lock",         kLockItemId);
    c.selectAllLayout = MakeButton(p, L"Select All", kSelectAllLayoutId);

    // Layout mode — alignment
    c.alignLabel    = MakeLabel(p, L"Align selected");
    c.alignLeft     = MakeButton(p, L"↤ L",  kAlignLeftId);
    c.alignRight    = MakeButton(p, L"R ↦",  kAlignRightId);
    c.alignTop      = MakeButton(p, L"↥ T",  kAlignTopId);
    c.alignBottom   = MakeButton(p, L"B ↧",  kAlignBottomId);
    c.alignCenterH  = MakeButton(p, L"↔ H",  kAlignCenterHId);
    c.alignCenterV  = MakeButton(p, L"↕ V",  kAlignCenterVId);
    c.distributeH   = MakeButton(p, L"Dist H",    kDistributeHId);
    c.distributeV   = MakeButton(p, L"Dist V",    kDistributeVId);

    // Layout mode — inspector
    c.layoutInspectorLabel = MakeLabel(p, L"Inspector");
    c.layoutNameLabel      = MakeLabel(p, L"Label");
    c.layoutLabelEdit      = MakeEdit(p, kLayoutLabelEditId);
    c.layoutXLabel         = MakeLabel(p, L"X");
    c.layoutYLabel         = MakeLabel(p, L"Y");
    c.layoutWidthLabel     = MakeLabel(p, L"W");
    c.layoutHeightLabel    = MakeLabel(p, L"H");
    c.layoutXEdit          = MakeEdit(p, kLayoutXEditId,      ES_NUMBER);
    c.layoutYEdit          = MakeEdit(p, kLayoutYEditId,      ES_NUMBER);
    c.layoutWidthEdit      = MakeEdit(p, kLayoutWidthEditId,  ES_NUMBER);
    c.layoutHeightEdit     = MakeEdit(p, kLayoutHeightEditId, ES_NUMBER);
    c.layoutCapacityLabel  = MakeLabel(p, L"Seats");
    c.layoutCapacityEdit   = MakeEdit(p, kLayoutCapacityEditId, ES_NUMBER);
    c.applyLayoutItem      = MakeButton(p, L"Apply",          kApplyLayoutItemId);
    c.duplicateLayoutItem  = MakeButton(p, L"Duplicate",      kDuplicateLayoutItemId);
    c.sendLayoutBack       = MakeButton(p, L"Send to Back",   kSendLayoutBackId);
    c.bringLayoutFront     = MakeButton(p, L"Bring to Front", kBringLayoutFrontId);

    // Spinner (updown) controls — positioned manually in LayoutFloatingTools
    auto makeSpin = [&](int id) -> HWND {
        return CreateWindowExW(0, UPDOWN_CLASS, nullptr,
            WS_CHILD | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
            0, 0, 0, 0, p,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
    };
    c.layoutXSpin = makeSpin(kLayoutXSpinId);
    c.layoutYSpin = makeSpin(kLayoutYSpinId);
    c.layoutWSpin = makeSpin(kLayoutWSpinId);
    c.layoutHSpin = makeSpin(kLayoutHSpinId);

    // Layout mode — room size
    c.roomSizeLabel   = MakeLabel(p, L"Room size (units, 0=default 1200\xD7""800)");
    c.roomWidthLabel  = MakeLabel(p, L"W");
    c.roomWidthEdit   = MakeEdit(p, kRoomWidthEditId,  ES_NUMBER);
    c.roomHeightLabel = MakeLabel(p, L"H");
    c.roomHeightEdit  = MakeEdit(p, kRoomHeightEditId, ES_NUMBER);
    c.applyRoomSize   = MakeButton(p, L"Apply Room Size", kApplyRoomSizeId);

    // Layout mode — room front edge (cycles Top→Right→Bottom→Left on click)
    c.frontEdgeLabel  = MakeLabel(p, L"Front of room (click to change)");
    c.frontEdgeButton = MakeButton(p, L"Front: Top", kFrontEdgeId);

    // --- Roster tab: two-column ListView ---
    c.rosterView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
        LVS_REPORT | LVS_SHOWSELALWAYS,   // no LVS_SINGLESEL — shift/ctrl-click works
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kRosterViewId)),
        GetModuleHandleW(nullptr), nullptr);
    if (c.rosterView) {
        ListView_SetExtendedListViewStyle(c.rosterView,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        LVCOLUMNW lvc{};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
        lvc.fmt  = LVCFMT_LEFT;   // explicit left-align for # and all columns
        lvc.iSubItem = 0; lvc.cx = Scale(28);
        lvc.pszText = const_cast<wchar_t*>(L"#");
        ListView_InsertColumn(c.rosterView, 0, &lvc);
        lvc.iSubItem = 1; lvc.cx = Scale(100);
        lvc.pszText = const_cast<wchar_t*>(L"First Name");
        ListView_InsertColumn(c.rosterView, 1, &lvc);
        lvc.iSubItem = 2; lvc.cx = Scale(100);
        lvc.pszText = const_cast<wchar_t*>(L"Last Name");
        ListView_InsertColumn(c.rosterView, 2, &lvc);
    }
    c.addStudentBtn    = MakeButton(p, L"+ Add",    kAddStudentId);
    c.removeStudentBtn = MakeButton(p, L"Remove",    kRemoveStudentId);
    c.showLastNamesBtn = MakeButton(p, L"Show Last Names", kShowLastNamesId, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    // Inline add/edit bar — always visible, no popup dialog needed
    c.inlineFirstEdit = MakeEdit(p, kInlineFirstEditId, ES_AUTOHSCROLL);
    SendMessageW(c.inlineFirstEdit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"First name…"));
    c.inlineLastEdit  = MakeEdit(p, kInlineLastEditId,  ES_AUTOHSCROLL);
    SendMessageW(c.inlineLastEdit,  EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Last name…"));
    c.saveStudentEdit = MakeButton(p, L"Save", kSaveStudentEditId);

    // --- Rules tab: structured sections ---
    c.keepApartHeader    = MakeLabel(p, L"Keep Apart Rules");
    c.keepApartDesc      = MakeLabel(p, L"Keep students away from each other.");
    c.keepApartList      = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kKeepApartListId)),
        GetModuleHandleW(nullptr), nullptr);
    if (c.keepApartList) {
        LVCOLUMNW lvc{};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        lvc.iSubItem = 0; lvc.cx = Scale(110);
        lvc.pszText = const_cast<wchar_t*>(L"Student A");
        ListView_InsertColumn(c.keepApartList, 0, &lvc);
        lvc.iSubItem = 1; lvc.cx = Scale(110);
        lvc.pszText = const_cast<wchar_t*>(L"Student B");
        ListView_InsertColumn(c.keepApartList, 1, &lvc);
        // Fixed columns — prevent user from dragging column dividers
        if (HWND hdr = ListView_GetHeader(c.keepApartList)) {
            SetWindowLongW(hdr, GWL_STYLE,
                GetWindowLongW(hdr, GWL_STYLE) | HDS_NOSIZING);
        }
    }
    c.addKeepApartBtn    = MakeButton(p, L"+ Add",  kAddKeepApartId);
    c.remKeepApartBtn    = MakeButton(p, L"Remove",      kRemKeepApartId);

    c.keepTogetherHeader = MakeLabel(p, L"Keep Together Rules");
    c.keepTogetherDesc   = MakeLabel(p, L"Keep groups of students near each other.");
    c.keepTogetherList   = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kKeepTogetherListId)),
        GetModuleHandleW(nullptr), nullptr);
    if (c.keepTogetherList) {
        LVCOLUMNW lvc{};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        lvc.iSubItem = 0; lvc.cx = Scale(110);
        lvc.pszText = const_cast<wchar_t*>(L"Student A");
        ListView_InsertColumn(c.keepTogetherList, 0, &lvc);
        lvc.iSubItem = 1; lvc.cx = Scale(110);
        lvc.pszText = const_cast<wchar_t*>(L"Student B");
        ListView_InsertColumn(c.keepTogetherList, 1, &lvc);
        if (HWND hdr = ListView_GetHeader(c.keepTogetherList)) {
            SetWindowLongW(hdr, GWL_STYLE,
                GetWindowLongW(hdr, GWL_STYLE) | HDS_NOSIZING);
        }
    }
    c.addKeepTogetherBtn = MakeButton(p, L"+ Add",  kAddKeepTogetherId);
    c.remKeepTogetherBtn = MakeButton(p, L"Remove",      kRemKeepTogetherId);

    c.deskTagHeader = MakeLabel(p, L"Desk Tag Rules");
    c.deskTagDesc   = MakeLabel(p,
        L"Restrict students on the seating chart to desks with a certain tag.");
    c.deskTagList    = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
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
        if (HWND hdr = ListView_GetHeader(c.deskTagList)) {
            SetWindowLongW(hdr, GWL_STYLE,
                GetWindowLongW(hdr, GWL_STYLE) | HDS_NOSIZING);
        }
    }
    c.addDeskTagRuleBtn = MakeButton(p, L"+ Tag Rule", kAddDeskTagRuleId);
    c.remDeskTagRuleBtn = MakeButton(p, L"Remove",     kRemDeskTagRuleId);

    // --- Groups tab (index 3) ---
    c.groupSizeLabel = MakeLabel(p, L"Groups of:");
    // Combobox for selecting base group size (valid options computed from roster)
    c.groupSizeCombo = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kGroupSizeComboId)),
        GetModuleHandleW(nullptr), nullptr);
    c.groupOrLabel    = MakeLabel(p, L"or");
    c.groupOrValLabel = MakeLabel(p, L"—");
    c.groupSizeEdit  = MakeEdit(p, kGroupSizeEditId, ES_NUMBER | ES_CENTER);
    SetWindowTextW(c.groupSizeEdit, L"3");
    c.groupSizeSpin  = CreateWindowExW(0, UPDOWN_CLASS, nullptr,
        WS_CHILD | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kGroupSizeSpinId)),
        GetModuleHandleW(nullptr), nullptr);
    c.groupConfigList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_VSCROLL |
        LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kGroupConfigListId)),
        GetModuleHandleW(nullptr), nullptr);
    c.shuffleGroupsBtn = MakeButton(p, L"Shuffle Groups", kShuffleGroupsId);
    c.groupsOutputList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_VSCROLL |
        LBS_NOINTEGRALHEIGHT,
        0, 0, 0, 0, p,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kGroupsOutputListId)),
        GetModuleHandleW(nullptr), nullptr);
    c.groupRulesLabel = MakeLabel(p, L"Group Rules");
    c.groupRulesEdit  = MakeEdit(p, kGroupRulesEditId, ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
    c.groupRulesApply = MakeButton(p, L"Apply Group Rules", kGroupRulesApplyId);
    c.groupResetBtn   = MakeButton(p, L"Reset Shuffle Memory", kGroupResetId);
    c.groupKeepApartToggle    = MakeButton(p, L"Show Keep Apart Rules",    kGroupKeepApartToggleId);
    c.groupKeepTogetherToggle = MakeButton(p, L"Show Keep Together Rules", kGroupKeepTogetherToggleId);
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
    SendMessageW(c.groupAvoidSameNumberCheck, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(c.groupAvoidSamePartnersCheck, BM_SETCHECK, BST_CHECKED, 0);

    // Tab control — Roster | Rules | Arrange | Groups
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
        TabCtrl_SetCurSel(c.tabControl, 2); // default: Arrange tab
    }

    // -----------------------------------------------------------------------
    // Tooltips — registered after all controls are created so every HWND is
    // valid.  One shared TOOLTIPS_CLASS window serves the entire sidebar.
    //
    // Rationale: ISTE Teacher Ready 1.4 (Interface Design) + 1.5 (Discoverability).
    // Teachers use this app under classroom time pressure and need just-in-time
    // explanations without having to leave the task to read documentation.
    // -----------------------------------------------------------------------
    HWND tip = CreateTooltipWnd(p);

    // Mode buttons
    AddTip(tip, c.layoutMode,
        L"Arrange mode\nAdd and position desks, tables and furniture.\n"
        L"Keyboard shortcuts: R = rotate  \xB7  F = flip  \xB7  L = lock  \xB7  "
        L"Arrow keys = nudge  \xB7  Ctrl+A = select all  \xB7  Ctrl+D = duplicate");
    AddTip(tip, c.seatMode,
        L"Assign mode\nDrag students from the roster list onto seats, or "
        L"double-click a seat to pick a name.\n"
        L"Keyboard: Delete = clear seat  \xB7  Esc = deselect");

    // Capture / export / print
    AddTip(tip, c.captureChart,
        L"Copy chart as image to clipboard\nShortcut: Ctrl+C");
    AddTip(tip, c.exportChart,
        L"Save chart as PNG image file\nShortcut: Ctrl+E");
    AddTip(tip, c.printChart,
        L"Print the seating chart\nShortcut: Ctrl+P");
    AddTip(tip, c.exportCsv,
        L"Export seating assignments as a CSV spreadsheet\n"
        L"Columns: Name, Desk, Seat #, Tags, Notes");
    AddTip(tip, c.seatingReport,
        L"Show a detailed seating summary\n"
        L"Includes: all assignments, unplaced students, constraint audit, affinity score");
    AddTip(tip, c.exportHtml,
        L"Export a printable HTML version of the chart for sharing or projecting");

    // Templates
    AddTip(tip, c.saveTemplateBtn,
        L"Save the current room layout as a reusable template\n"
        L"(Roster and rules are NOT saved — furniture layout only)");
    AddTip(tip, c.loadTemplateBtn,
        L"Load a previously saved room layout template\n"
        L"(Your current roster and rules are kept)");

    // Roster section
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
    AddTip(tip, c.rosterFilter,
        L"Filter the roster list by name or tag\n"
        L"Examples: type \"IEP\" to show IEP students  \xB7  type \"Alice\" to find a student\n"
        L"Clear to show all students");
    AddTip(tip, c.assignSelectedRoster,
        L"Assign the selected roster name to the focused seat on the chart\n"
        L"Tip: click a seat on the chart first, then select a name here");
    AddTip(tip, c.bulkTag,
        L"Apply or remove a tag for all selected students in the roster list\n"
        L"Select multiple names with Ctrl+click, then click here");
    AddTip(tip, c.showLastNamesBtn,
        L"Toggle whether names are shown with their last name everywhere in the app");
    AddTip(tip, c.applyRules,
        L"Parse the rules text and apply constraints for auto-assign\n\n"
        L"Formats (one per line):\n"
        L"  Alice | Bob          keep apart (hard constraint)\n"
        L"  Alice | Bob @3       keep apart, radius 3 units\n"
        L"  Alice + Bob          sit near (soft preference)\n"
        L"  Alice + Bob @5       sit near, weight 5 (stronger)\n"
        L"  Alice == Bob         must sit together (same item)\n"
        L"  Group: Alice Bob Carol   cluster (same pod/table)");
    AddTip(tip, c.groupRulesEdit,
        L"Enter group-cluster rules separately from desk rules.\n"
        L"Use: Group: Alice Bob Charlie");
    AddTip(tip, c.groupResetBtn,
        L"Clear remembered shuffle history so the next group shuffle can reuse the same pairings");

    // Layout tools
    AddTip(tip, c.addSmartboard,   L"Add a smartboard / whiteboard to the layout");
    AddTip(tip, c.addTrap,         L"Add a trapezoid desk (single seat, angled)");
    AddTip(tip, c.addDesk,         L"Add a rectangular single-student desk");
    AddTip(tip, c.addTable,        L"Add a 4-seat table");
    AddTip(tip, c.addBigTable,     L"Add a large table with configurable seat count");
    AddTip(tip, c.addBlock,        L"Add a label block (no seats — for room labels)");
    AddTip(tip, c.addTrapPair,     L"Add a trapezoid pair (2 seats facing each other)");
    AddTip(tip, c.addTrapPod,      L"Add a trapezoid pod (4 seats in a cluster)");
    AddTip(tip, c.deleteLayout,    L"Delete selected item(s)\nShortcut: Delete key");
    AddTip(tip, c.mergeSelected,   L"Merge selected block labels into one");
    AddTip(tip, c.presetRows,      L"Replace layout with a simple rows preset");
    AddTip(tip, c.presetU,         L"Replace layout with a U-shape preset");
    AddTip(tip, c.presetHorseshoe, L"Replace layout with a horseshoe preset");
    AddTip(tip, c.toggleVisible,   L"Toggle visibility of selected item(s)\n"
                                    L"Hidden items are not shown on print/export");

    // Transform
    AddTip(tip, c.rotateCW,        L"Rotate selected item(s) 90\xB0 clockwise\nShortcut: R");
    AddTip(tip, c.rotateCCW,       L"Rotate selected item(s) 90\xB0 counter-clockwise\nShortcut: Shift+R");
    AddTip(tip, c.flipH,           L"Flip selected item(s) horizontally\nShortcut: F");
    AddTip(tip, c.lockItem,        L"Lock / unlock selected item\n"
                                    L"Locked items cannot be moved or resized accidentally\n"
                                    L"Shortcut: L");
    AddTip(tip, c.selectAllLayout, L"Select all layout items\nShortcut: Ctrl+A");

    // Alignment
    AddTip(tip, c.alignLeft,    L"Align left edges of selected items");
    AddTip(tip, c.alignRight,   L"Align right edges of selected items");
    AddTip(tip, c.alignTop,     L"Align top edges of selected items");
    AddTip(tip, c.alignBottom,  L"Align bottom edges of selected items");
    AddTip(tip, c.alignCenterH, L"Center selected items horizontally");
    AddTip(tip, c.alignCenterV, L"Center selected items vertically");
    AddTip(tip, c.distributeH,  L"Space selected items evenly — horizontal");
    AddTip(tip, c.distributeV,  L"Space selected items evenly — vertical");

    // Inspector
    AddTip(tip, c.applyLayoutItem,
        L"Apply the X, Y, width, height and label values from the fields above\n"
        L"to the selected layout item");
    AddTip(tip, c.duplicateLayoutItem,
        L"Duplicate the selected item (also Ctrl+D)");
    AddTip(tip, c.sendLayoutBack,
        L"Move selected item one step towards the back (below other items)");
    AddTip(tip, c.bringLayoutFront,
        L"Move selected item one step towards the front (above other items)");

    // Room size / front edge
    AddTip(tip, c.applyRoomSize,
        L"Set the room canvas size in room-unit coordinates\n"
        L"Leave at 0 to use the default (1200 \xD7 800)");
    AddTip(tip, c.frontEdgeButton,
        L"Click to cycle which room edge is designated as the front\n"
        L"The front edge is used by the \"Front row\" tag rule in auto-assign");
}

// ---------------------------------------------------------------------------
// ApplyFontsToControls
// ---------------------------------------------------------------------------

void ApplyFontsToControls(const ControlHandles& c, const Renderer& r) {
    auto set = [](HWND h, HFONT f) { if (h && f) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE); };
    const HWND ui[] = {
        c.rosterEdit, c.rosterFilter, c.rosterList, c.restrictionEdit, c.bulkTag,
        c.importRoster, c.loadRoster, c.saveNow,
        c.applyRules, c.autoAssign, c.layoutMode, c.seatMode,
        c.addSmartboard, c.addTrap, c.addDesk, c.addTable, c.addBigTable, c.addBlock,
        c.addTrapPair, c.addTrapPod, c.deleteLayout, c.mergeSelected,
        c.captureChart, c.exportChart, c.printChart, c.exportCsv, c.seatingReport, c.exportHtml,
        c.summaryLabel, c.modeLabel, c.rosterLabel, c.rosterListLabel,
        c.restrictionLabel, c.layoutToolsLabel, c.statusLabel, c.footerMetaLabel,
        c.quickFillSeats, c.assignSelectedRoster, c.sendLayoutBack, c.bringLayoutFront,
        c.clearAllSeats, c.layoutInspectorLabel, c.layoutNameLabel, c.layoutLabelEdit,
        c.layoutXLabel, c.layoutYLabel, c.layoutWidthLabel, c.layoutHeightLabel,
        c.layoutXEdit, c.layoutYEdit, c.layoutWidthEdit, c.layoutHeightEdit,
        c.layoutCapacityLabel, c.layoutCapacityEdit,
        c.applyLayoutItem, c.duplicateLayoutItem,
        c.saveTemplateBtn, c.loadTemplateBtn,
        c.layoutTransformLabel, c.rotateCW, c.rotateCCW, c.flipH, c.lockItem,
        c.selectAllLayout,
        c.alignLabel, c.alignLeft, c.alignRight, c.alignTop, c.alignBottom,
        c.alignCenterH, c.alignCenterV, c.distributeH, c.distributeV, c.presetRows, c.presetU, c.presetHorseshoe, c.toggleVisible,
        c.roomSizeLabel, c.roomWidthLabel, c.roomWidthEdit,
        c.roomHeightLabel, c.roomHeightEdit, c.applyRoomSize,
        c.frontEdgeLabel, c.frontEdgeButton
    };
    for (HWND h : ui) set(h, r.UiFont());
    set(c.tabControl, r.UiFont());
    // Roster tab
    set(c.rosterView, r.UiFont());
    set(c.addStudentBtn, r.UiFont()); set(c.removeStudentBtn, r.UiFont());
    set(c.showLastNamesBtn, r.UiFont());
    set(c.inlineFirstEdit, r.UiFont()); set(c.inlineLastEdit, r.UiFont());
    set(c.saveStudentEdit, r.UiFont());
    // Rules tab
    set(c.keepApartList, r.UiFont()); set(c.keepTogetherList, r.UiFont());
    set(c.addKeepApartBtn, r.UiFont()); set(c.remKeepApartBtn, r.UiFont());
    set(c.addKeepTogetherBtn, r.UiFont()); set(c.remKeepTogetherBtn, r.UiFont());
    set(c.keepApartHeader, r.UiFont()); set(c.keepApartDesc, r.UiFont());
    set(c.keepTogetherHeader, r.UiFont()); set(c.keepTogetherDesc, r.UiFont());
    set(c.deskTagHeader, r.UiFont()); set(c.deskTagDesc, r.UiFont());
    set(c.deskTagList, r.UiFont());
    set(c.addDeskTagRuleBtn, r.UiFont()); set(c.remDeskTagRuleBtn, r.UiFont());
    // Groups tab
    set(c.groupSizeLabel, r.UiFont()); set(c.groupSizeEdit, r.UiFont());
    set(c.groupSizeCombo, r.UiFont());
    set(c.groupOrLabel, r.UiFont()); set(c.groupOrValLabel, r.UiFont());
    set(c.groupConfigList, r.UiFont()); set(c.groupsOutputList, r.UiFont());
    set(c.shuffleGroupsBtn, r.UiFont());
    set(c.groupRulesLabel, r.UiFont()); set(c.groupRulesEdit, r.UiFont());
    set(c.groupRulesApply, r.UiFont()); set(c.groupResetBtn, r.UiFont());
    set(c.groupKeepApartToggle, r.UiFont()); set(c.groupKeepTogetherToggle, r.UiFont());
    set(c.groupAvoidSameNumberCheck, r.UiFont()); set(c.groupAvoidSamePartnersCheck, r.UiFont());
    set(c.titleLabel, r.TitleFont());
    for (HWND h : {c.modeLabel, c.rosterLabel, c.rosterListLabel, c.restrictionLabel,
                   c.layoutToolsLabel, c.statusLabel, c.footerMetaLabel,
                   c.layoutInspectorLabel,
                   c.layoutTransformLabel, c.alignLabel, c.roomSizeLabel})
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
        const std::wstring last  = (s.showLastNames && sp != std::wstring::npos) ? name.substr(sp + 1) : L"";
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

    // Ghost row — always one empty row at the bottom showing the next number.
    // Clicking its First/Last Name cell starts inline add without any button press.
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
    auto fillList = [](HWND lv, const std::vector<Restriction>& rules) {
        if (!lv) return;
        ListView_DeleteAllItems(lv);
        for (int i = 0; i < static_cast<int>(rules.size()); ++i) {
            LVITEMW lvi{};
            lvi.mask = LVIF_TEXT; lvi.iItem = i; lvi.iSubItem = 0;
            lvi.pszText = const_cast<wchar_t*>(rules[static_cast<size_t>(i)].first.c_str());
            const int idx = ListView_InsertItem(lv, &lvi);
            if (idx >= 0)
                ListView_SetItemText(lv, idx, 1,
                    const_cast<wchar_t*>(rules[static_cast<size_t>(i)].second.c_str()));
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
    si(c.layoutXEdit,     item.bounds.left);
    si(c.layoutYEdit,     item.bounds.top);
    si(c.layoutWidthEdit, item.bounds.right  - item.bounds.left);
    si(c.layoutHeightEdit,item.bounds.bottom - item.bounds.top);
    // Capacity field: show current capacity (or default) for BigTable, blank others
    if (item.type == LayoutItemType::BigTable) {
        const int cap = item.capacity > 0 ? item.capacity : LayoutItemDefaultCapacity(item.type);
        si(c.layoutCapacityEdit, cap);
    } else if (c.layoutCapacityEdit) {
        SetWindowTextW(c.layoutCapacityEdit, L"");
    }
    // Lock button text
    if (c.lockItem) SetWindowTextW(c.lockItem, item.locked ? L"Unlock" : L"Lock");
}

void UpdateSidebarText(const AppState& s, const ControlHandles& c) {
    if (c.titleLabel) SetWindowTextW(c.titleLabel, L"Seating Chart");
    if (c.summaryLabel) {
        // Build a two-line summary so teachers can scan key stats at a glance
        // without the line being truncated (ISTE 1.4 Interface Design; Deep
        // Research Report: "at-a-glance comprehension is a UX concern").
        int layoutSeats = 0, layoutAssigned = 0;
        for (const auto& item : s.layoutItems) {
            layoutSeats += LayoutItemSeats(item);
            for (const auto& occ : item.occupants) if (!occ.empty()) ++layoutAssigned;
        }
        const bool isLayout = (s.chartMode == ChartMode::Layout);

        // Line 1: mode + seat fill status + unplaced count
        std::wstring line1 = isLayout ? L"Arrange" : L"Assign";
        line1 += L"  \xB7  " + std::to_wstring(static_cast<int>(s.layoutItems.size())) + L" items";
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

        // Line 2: roster size + rules + affinity (optional, only when relevant)
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
    // Sync room size edits only when they appear empty (avoid overwriting user input).
    if (c.roomWidthEdit  && GetWindowTextLengthW(c.roomWidthEdit)  == 0)
        SetWindowTextW(c.roomWidthEdit,  std::to_wstring(s.roomW).c_str());
    if (c.roomHeightEdit && GetWindowTextLengthW(c.roomHeightEdit) == 0)
        SetWindowTextW(c.roomHeightEdit, std::to_wstring(s.roomH).c_str());
    if (c.frontEdgeButton)
        SetWindowTextW(c.frontEdgeButton,
                       (L"Front: " + std::wstring(RoomEdgeName(s.frontEdge))).c_str());
}

void UpdateButtonState(const AppState& s, const ControlHandles& c, bool aaRunning) {
    const BOOL seats   = (s.chartMode == ChartMode::Seats);   // "Assign" tool
    const BOOL layout  = (s.chartMode == ChartMode::Layout);  // "Arrange" tool
    const BOOL hasRoster   = !s.roster.empty();
    const BOOL hasSeats    = TotalLayoutSeats(s.layoutItems) > 0;
    const BOOL hasFocusSeat= s.selectedLayoutSeat.has_value();
    const BOOL hasItem   = s.selectedLayoutItem.has_value();
    const BOOL hasAny    = !s.selectedLayoutItems.empty();
    const bool itemLocked = hasItem && s.layoutItems[static_cast<size_t>(*s.selectedLayoutItem)].locked;

    EnableWindow(c.rosterEdit,  TRUE);
    EnableWindow(c.importRoster,TRUE);
    EnableWindow(c.loadRoster,  TRUE);
    EnableWindow(c.saveNow,     TRUE);
    EnableWindow(c.showLastNamesBtn, TRUE);
    EnableWindow(c.inlineFirstEdit, TRUE);
    EnableWindow(c.inlineLastEdit,  TRUE);
    EnableWindow(c.saveStudentEdit, TRUE);
    EnableWindow(c.groupSizeCombo, TRUE);
    EnableWindow(c.groupRulesEdit, TRUE);
    EnableWindow(c.groupRulesApply, TRUE);
    EnableWindow(c.groupKeepApartToggle, TRUE);
    EnableWindow(c.groupKeepTogetherToggle, TRUE);
    EnableWindow(c.groupAvoidSameNumberCheck, TRUE);
    EnableWindow(c.groupAvoidSamePartnersCheck, TRUE);
    EnableWindow(c.groupResetBtn,   TRUE);
    EnableWindow(c.rosterList,  seats);
    EnableWindow(c.restrictionEdit, seats);
    EnableWindow(c.applyRules,  seats);
    EnableWindow(c.autoAssign,  seats && hasRoster && hasSeats && !aaRunning);
    EnableWindow(c.quickFillSeats,  seats && hasRoster && hasSeats);
    EnableWindow(c.clearAllSeats,   seats && hasSeats);
    const bool rosterSel = c.rosterList &&
        SendMessageW(c.rosterList, LB_GETCURSEL, 0, 0) != LB_ERR;
    EnableWindow(c.assignSelectedRoster, seats && hasFocusSeat && rosterSel);
    // bulk if any selected in roster (even if no focus seat)
    bool hasRosterSel = false;
    if (c.rosterList) {
        int count = SendMessageW(c.rosterList, LB_GETSELCOUNT, 0, 0);
        hasRosterSel = count > 0;
    }
    EnableWindow(c.bulkTag, seats && hasRosterSel);
    EnableWindow(c.seatMode,    TRUE);
    EnableWindow(c.layoutMode,  TRUE);
    EnableWindow(c.captureChart,TRUE);
    EnableWindow(c.exportChart, TRUE);
    EnableWindow(c.printChart,  TRUE);
    EnableWindow(c.exportCsv,   TRUE);
    EnableWindow(c.seatingReport, TRUE);
    EnableWindow(c.exportHtml, TRUE);

    // Add item buttons (always enabled in layout mode)
    EnableWindow(c.addSmartboard, layout);
    EnableWindow(c.addTrap,       layout);
    EnableWindow(c.addDesk,       layout);
    EnableWindow(c.addTable,      layout);
    EnableWindow(c.addBigTable,   layout);
    EnableWindow(c.addTrapPair,   layout);
    EnableWindow(c.addTrapPod,    layout);
    EnableWindow(c.addBlock,      layout);
    EnableWindow(c.deleteLayout,  layout && hasAny);
    EnableWindow(c.mergeSelected, FALSE);
    EnableWindow(c.presetRows,    layout);
    EnableWindow(c.presetU,       layout);
    EnableWindow(c.presetHorseshoe, layout);
    EnableWindow(c.toggleVisible, layout && hasAny);
    EnableWindow(c.selectAllLayout, layout && !s.layoutItems.empty());

    // Transform (rotate, flip, lock) — unlocked single item or multi-select
    const BOOL canTransform = layout && hasAny && !itemLocked;
    EnableWindow(c.rotateCW,  canTransform);
    EnableWindow(c.rotateCCW, canTransform);
    EnableWindow(c.flipH,     canTransform);
    EnableWindow(c.lockItem,  layout && hasItem);  // lock always available

    // Alignment — need >=2 selected items
    for (HWND h : {c.alignLeft, c.alignRight, c.alignTop, c.alignBottom,
                   c.alignCenterH, c.alignCenterV, c.distributeH, c.distributeV})
        EnableWindow(h, layout);

    // Inspector (single selected item)
    const BOOL inspector = layout && hasItem;
    for (HWND h : {c.layoutLabelEdit, c.layoutXEdit, c.layoutYEdit,
                   c.layoutWidthEdit, c.layoutHeightEdit, c.applyLayoutItem, c.duplicateLayoutItem,
                   c.layoutXSpin, c.layoutYSpin, c.layoutWSpin, c.layoutHSpin})
        EnableWindow(h, inspector && !itemLocked);
    EnableWindow(c.layoutCapacityEdit,
        inspector && !itemLocked &&
        s.layoutItems[static_cast<size_t>(*s.selectedLayoutItem)].type == LayoutItemType::BigTable);
    EnableWindow(c.sendLayoutBack,  inspector && *s.selectedLayoutItem > 0);
    EnableWindow(c.bringLayoutFront,inspector &&
        *s.selectedLayoutItem < static_cast<int>(s.layoutItems.size()) - 1);

    // Room size
    EnableWindow(c.roomWidthEdit,  layout);
    EnableWindow(c.roomHeightEdit, layout);
    EnableWindow(c.applyRoomSize,  layout);
    EnableWindow(c.frontEdgeButton, layout);

    EnableWindow(c.saveTemplateBtn, TRUE);
    EnableWindow(c.loadTemplateBtn, TRUE);

    SendMessageW(c.seatMode,   BM_SETCHECK, s.chartMode == ChartMode::Seats  ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(c.layoutMode, BM_SETCHECK, s.chartMode == ChartMode::Layout ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(c.showLastNamesBtn, BM_SETCHECK, s.showLastNames ? BST_CHECKED : BST_UNCHECKED, 0);
    UpdateSidebarText(s, c);
}
