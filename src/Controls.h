#pragma once
#include "AppState.h"
#include <windows.h>

// ---------------------------------------------------------------------------
// Control IDs
// ---------------------------------------------------------------------------
constexpr int kRosterEditId           = 1004;
constexpr int kImportRosterId         = 1005;
constexpr int kLoadRosterId           = 1006;
constexpr int kSaveNowId              = 1007;
constexpr int kRosterListId           = 1008;
constexpr int kRestrictionEditId      = 1009;
constexpr int kApplyRulesId           = 1010;
constexpr int kAutoAssignId           = 1011;
constexpr int kQuickFillSeatsId       = 1012;
constexpr int kAssignSelectedRosterId = 1013;
constexpr int kBulkTagId              = 1016; // bulk tag for selected in roster
constexpr int kRosterFilterId         = 1014; // for search/filter roster list
constexpr int kShowLastNamesId        = 1017;
constexpr int kSeatModeId             = 2001;
constexpr int kLayoutModeId           = 2002;
constexpr int kCaptureChartId         = 2003;
constexpr int kClearAllSeatsId        = 2004;
constexpr int kExportChartId          = 2005;
constexpr int kPrintChartId           = 2006;
constexpr int kExportCsvId            = 2007; // Quick win: export seating as CSV (Name, Desk, Seat, Tags, Notes)
constexpr int kSeatingReportId        = 2008; // detailed report
constexpr int kExportHtmlId           = 2009; // printable HTML export
constexpr int kAddSmartboardId        = 3001;
constexpr int kAddTrapezoidId         = 3002;
constexpr int kAddDeskId              = 3003;
constexpr int kAddTableId             = 3004;
constexpr int kDeleteLayoutItemId     = 3005;
constexpr int kAddTrapPodId           = 3006;
constexpr int kAddTrapPairId          = 3007;
constexpr int kAddBlockId             = 3008;
constexpr int kMergeSelectedId        = 3110;
constexpr int kLayoutLabelEditId      = 3101;
constexpr int kLayoutXEditId          = 3102;
constexpr int kLayoutYEditId          = 3103;
constexpr int kLayoutWidthEditId      = 3104;
constexpr int kLayoutHeightEditId     = 3105;
constexpr int kApplyLayoutItemId      = 3106;
constexpr int kDuplicateLayoutItemId  = 3107;
constexpr int kSendLayoutBackId       = 3108;
constexpr int kBringLayoutFrontId     = 3109;
constexpr int kSaveTemplateId         = 4001;
constexpr int kLoadTemplateId         = 4002;

// New layout controls (3200+)
constexpr int kAddBigTableId          = 3200;
constexpr int kRotateCWId             = 3201;
constexpr int kRotateCCWId            = 3202;
constexpr int kFlipHId                = 3203;
constexpr int kLockItemId             = 3204;
constexpr int kSelectAllLayoutId      = 3205;
constexpr int kAlignLeftId            = 3210;
constexpr int kAlignRightId           = 3211;
constexpr int kAlignTopId             = 3212;
constexpr int kAlignBottomId          = 3213;
constexpr int kAlignCenterHId         = 3214;
constexpr int kAlignCenterVId         = 3215;
constexpr int kDistributeHId          = 3216;
constexpr int kDistributeVId          = 3217;
constexpr int kLayoutCapacityEditId   = 3218;
constexpr int kPresetRowsId           = 3230; // basic built-in preset for sprint4
constexpr int kPresetUId              = 3231;
constexpr int kPresetHorseshoeId      = 3232;
constexpr int kToggleVisibleId        = 3233; // layer visibility
constexpr int kCopyLayoutItemId       = 3234;
constexpr int kCutLayoutItemId        = 3235;
constexpr int kPasteLayoutItemId      = 3236;
constexpr int kGroupLayoutItemsId     = 3237;
constexpr int kUngroupLayoutItemsId   = 3238;
constexpr int kRoomWidthEditId        = 3221;
constexpr int kRoomHeightEditId       = 3222;
constexpr int kApplyRoomSizeId        = 3223;
constexpr int kFrontEdgeId            = 3224;
constexpr int kShowAlignmentToolsId   = 5001;
constexpr int kShowObjectInspectorId  = 5002;
constexpr int kTabControlId           = 5100;

// Roster tab — two-column student table
constexpr int kRosterViewId           = 6001;
constexpr int kAddStudentId           = 6002;
constexpr int kRemoveStudentId        = 6003;

// Rules tab — structured sections
constexpr int kKeepApartListId        = 6010;
constexpr int kAddKeepApartId         = 6011;
constexpr int kRemKeepApartId         = 6012;
constexpr int kKeepTogetherListId     = 6013;
constexpr int kAddKeepTogetherId      = 6014;
constexpr int kRemKeepTogetherId      = 6015;
constexpr int kDeskTagListId          = 6016;
constexpr int kAddDeskTagRuleId       = 6017;
constexpr int kRemDeskTagRuleId       = 6018;

// Groups tab (index 3)
constexpr int kGroupSizeEditId        = 6020;
constexpr int kGroupSizeSpinId        = 6021;
constexpr int kGroupConfigListId      = 6022;
constexpr int kShuffleGroupsId        = 6023;
constexpr int kGroupsOutputListId     = 6024;
constexpr int kGroupRulesApplyId      = 6025;
constexpr int kGroupResetId           = 6026;
constexpr int kGroupRulesEditId       = 6027;
constexpr int kGroupSizeComboId       = 6028; // "Groups of:" dropdown

// Inline roster edit bar (Roster tab)
constexpr int kInlineFirstEditId      = 6035;
constexpr int kInlineLastEditId       = 6036;
constexpr int kSaveStudentEditId      = 6037;

// Class strip (main window, not sidebar)
constexpr int kClassStripId           = 6100;
constexpr int kAddClassBtnId          = 6101;

// Spinner (updown) controls for the Object Inspector
constexpr int kLayoutXSpinId          = 3240;
constexpr int kLayoutYSpinId          = 3241;
constexpr int kLayoutWSpinId          = 3242;
constexpr int kLayoutHSpinId          = 3243;

// ---------------------------------------------------------------------------
// All UI control handles (plain data struct)
// ---------------------------------------------------------------------------
struct ControlHandles {
    HWND sidebar    = nullptr;
    HWND tabControl = nullptr; // Roster | Rules | Arrange tab strip

    // Header / status (always visible, not scrolled)
    HWND titleLabel = nullptr, summaryLabel = nullptr, statusLabel = nullptr;

    // Mode strip
    HWND modeLabel = nullptr, seatMode = nullptr, layoutMode = nullptr;
    HWND captureChart = nullptr, exportChart = nullptr, printChart = nullptr, exportCsv = nullptr, seatingReport = nullptr, exportHtml = nullptr;

    // Assign mode (roster / restrictions / auto-fill)
    HWND rosterLabel = nullptr, rosterEdit = nullptr;
    HWND importRoster = nullptr, loadRoster = nullptr, saveNow = nullptr;
    HWND autoAssign = nullptr, quickFillSeats = nullptr, clearAllSeats = nullptr;
    HWND rosterFilter = nullptr, rosterListLabel = nullptr, rosterList = nullptr, assignSelectedRoster = nullptr, bulkTag = nullptr;
    HWND restrictionLabel = nullptr, restrictionEdit = nullptr, applyRules = nullptr;
    HWND saveTemplateBtn = nullptr, loadTemplateBtn = nullptr;

    // Layout mode — add items
    HWND layoutToolsLabel = nullptr;
    HWND addSmartboard = nullptr, addTrap = nullptr, addDesk = nullptr, addTable = nullptr;
    HWND addBigTable = nullptr, addBlock = nullptr;
    HWND addTrapPod = nullptr, addTrapPair = nullptr;
    HWND deleteLayout = nullptr, mergeSelected = nullptr;

    // Layout mode — transform tools
    HWND layoutTransformLabel = nullptr;
    HWND rotateCW = nullptr, rotateCCW = nullptr, flipH = nullptr, lockItem = nullptr;
    HWND selectAllLayout = nullptr;

    // Layout mode — alignment
    HWND alignLabel = nullptr;
    HWND alignLeft = nullptr, alignRight = nullptr, alignTop = nullptr, alignBottom = nullptr;
    HWND alignCenterH = nullptr, alignCenterV = nullptr;
    HWND distributeH = nullptr, distributeV = nullptr;
    HWND presetRows = nullptr; // built-in preset button
    HWND presetU = nullptr;
    HWND presetHorseshoe = nullptr;
    HWND toggleVisible = nullptr; // visibility toggle for selected

    // Layout mode — inspector
    HWND layoutInspectorLabel = nullptr, layoutNameLabel = nullptr, layoutLabelEdit = nullptr;
    HWND layoutXLabel = nullptr, layoutYLabel = nullptr;
    HWND layoutWidthLabel = nullptr, layoutHeightLabel = nullptr;
    HWND layoutXEdit = nullptr, layoutYEdit = nullptr;
    HWND layoutWidthEdit = nullptr, layoutHeightEdit = nullptr;
    HWND layoutCapacityLabel = nullptr, layoutCapacityEdit = nullptr;
    HWND applyLayoutItem = nullptr, duplicateLayoutItem = nullptr;
    HWND sendLayoutBack = nullptr, bringLayoutFront = nullptr;
    HWND layoutXSpin = nullptr, layoutYSpin = nullptr;
    HWND layoutWSpin = nullptr, layoutHSpin = nullptr;

    // Roster tab — two-column ListView
    HWND rosterView       = nullptr;
    HWND addStudentBtn    = nullptr;
    HWND removeStudentBtn = nullptr;
    HWND showLastNamesBtn = nullptr;

    // Inline roster edit bar
    HWND inlineFirstEdit  = nullptr;
    HWND inlineLastEdit   = nullptr;
    HWND saveStudentEdit  = nullptr;

    // Rules tab — structured sections
    HWND keepApartHeader  = nullptr, keepApartDesc     = nullptr;
    HWND keepApartList    = nullptr;
    HWND addKeepApartBtn  = nullptr, remKeepApartBtn   = nullptr;
    HWND keepTogetherHeader = nullptr, keepTogetherDesc = nullptr;
    HWND keepTogetherList = nullptr;
    HWND addKeepTogetherBtn = nullptr, remKeepTogetherBtn = nullptr;
    HWND deskTagHeader    = nullptr, deskTagDesc        = nullptr;
    HWND deskTagList      = nullptr;
    HWND addDeskTagRuleBtn = nullptr, remDeskTagRuleBtn = nullptr;

    // Groups tab (index 3)
    HWND groupSizeLabel   = nullptr;
    HWND groupSizeEdit    = nullptr, groupSizeSpin    = nullptr; // legacy, kept hidden
    HWND groupSizeCombo   = nullptr;   // new "Groups of:" dropdown
    HWND groupOrLabel     = nullptr;   // "or" static text
    HWND groupOrValLabel  = nullptr;   // e.g. "3" (computed overflow size)
    HWND groupConfigList  = nullptr;   // legacy list kept hidden
    HWND shuffleGroupsBtn = nullptr;
    HWND groupsOutputList = nullptr;
    HWND groupRulesLabel  = nullptr;
    HWND groupRulesEdit   = nullptr;
    HWND groupRulesApply  = nullptr;
    HWND groupResetBtn    = nullptr;

    // Layout mode — room size
    HWND roomSizeLabel = nullptr;
    HWND roomWidthLabel = nullptr, roomWidthEdit = nullptr;
    HWND roomHeightLabel = nullptr, roomHeightEdit = nullptr;
    HWND applyRoomSize = nullptr;

    // Layout mode — room front edge
    HWND frontEdgeLabel = nullptr, frontEdgeButton = nullptr;
};

// ---------------------------------------------------------------------------
// Forward-declared Renderer (full definition not needed here)
// ---------------------------------------------------------------------------
class Renderer;

void CreateAllUIControls(HWND parent, ControlHandles& c);
void ApplyFontsToControls(const ControlHandles& c, const Renderer& r);

// Sync helpers (called by SeatingChartApp after state changes)
void SyncRosterEditFromRoster(const AppState& state, const ControlHandles& c);
void SyncRestrictionEditFromRules(const AppState& state, const ControlHandles& c);
void SyncGroupRulesEditFromState(const AppState& state, const ControlHandles& c);
void RefreshRosterList(const AppState& state, const ControlHandles& c);
void SyncLayoutInspectorWithSelection(const AppState& state, const ControlHandles& c);
void SyncRosterView(const AppState& state, const ControlHandles& c);
void SyncRulesLists(const AppState& state, const ControlHandles& c);
void UpdateSidebarText(const AppState& state, const ControlHandles& c);
void UpdateButtonState(const AppState& state, const ControlHandles& c,
                       bool autoAssignRunning);
