#pragma once
#include "AppState.h"
#include "AutoAssign.h"
#include "Controls.h"
#include "LayoutEditor.h"
#include "Renderer.h"
#include "SelectionManager.h"
#include "SidebarManager.h"
#include "Utils.h"
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>

// ---------------------------------------------------------------------------
// SeatingChartApp
//
// Thin controller. Owns the model (AppState), controls, renderer, sidebar, and
// the two layout sub-controllers (LayoutEditor + SelectionManager). WndProc is
// a static trampoline that stores the app pointer in GWLP_USERDATA and forwards
// every message here; per-message handlers delegate layout work to the
// sub-controllers and expose a small set of host services (below) back to them.
// ---------------------------------------------------------------------------
class SeatingChartApp {
    friend LRESULT CALLBACK RosterListSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
public:
    SeatingChartApp()
        : selection_(*this, state_, layoutTx_),
          editor_(*this, state_, layoutTx_) {}
    ~SeatingChartApp() = default;

    SeatingChartApp(const SeatingChartApp&)            = delete;
    SeatingChartApp& operator=(const SeatingChartApp&) = delete;

    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleSidebarMessage(HWND sidebar, UINT msg, WPARAM wParam, LPARAM lParam);
    // Called from subclass procs (must be public)
    void CommitInlineCellEdit(bool advance = false);
    void CancelInlineCellEdit();
    void AdvanceToNextStudent();   // Enter key: commit + open next row's first-name cell
    bool IsAddingNewStudent() const { return cellEdit_.isNew; }
    // Called from subclass procs (must be public)
    enum class RuleAdvance { None, NextCell, NextRow };
    void CommitRuleCellEdit(RuleAdvance advance = RuleAdvance::None);
    void CancelRuleCellEdit();
    // Commit both floating inline editors (rule cell + roster cell) if open.
    // Called before anything that moves sidebar children (resize, scroll,
    // splitter drag, tab switch) — the editors float at fixed coordinates and
    // would otherwise be orphaned at stale positions.
    void CommitOpenInlineEditors() {
        if (ruleCellEdit_.edit) CommitRuleCellEdit(RuleAdvance::None);
        if (cellEdit_.wnd)      CommitInlineCellEdit(false);
    }
    void ShowRuleCellContextMenu(bool isApart, int row, int col);
    void UpdateRuleSuggestions();               // rebuild + show/hide popup list
    void AcceptRuleSuggestion(int idx);         // copy match[idx] into the edit
    void MoveRuleSuggestionHighlight(int delta);// scroll list selection up/down
    HWND RuleSuggestionList() const { return ruleCellEdit_.suggestion; }

    static SeatingChartApp* FromHwnd(HWND hwnd) {
        return reinterpret_cast<SeatingChartApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    // -----------------------------------------------------------------------
    // Host services — used by LayoutEditor / SelectionManager.
    // These give the sub-controllers controlled access to the shared model,
    // controls, window handle and the standard UI-refresh primitives, so the
    // sub-controllers can own complete operations while the app stays thin.
    // -----------------------------------------------------------------------
    AppState&                  State()             { return state_; }
    ControlHandles&            Controls()          { return controls_; }
    const AppLayout&           Layout()      const { return layout_; }
    const LayoutViewTransform& LayoutTransform() const { return layoutTx_; }
    HWND                       Hwnd()        const { return hwnd_; }
    bool                       AutoAssignRunning() const { return aaRunning_; }

    void SetStatus(const std::wstring& text);
    void InvalidateChart();                 // invalidate full chart area
    void SetSnapGuides(std::vector<int> vert, std::vector<int> horiz);
    void ClearSnapGuides();
    void RecomputeLayoutTransform();        // rebuild layoutTx_ from current room size
    void RefreshSelectionFlags() { selection_.RefreshSelectionFlags(); }
    void SyncLayoutInspector();             // SyncLayoutInspectorWithSelection(state, controls)
    void RefreshButtons();                  // UpdateButtonState(state, controls, aaRunning_)
    void ScheduleSave();                    // ScheduleAutoSave(&state_, hwnd_)

    // --- Thin-controller convenience helpers ---
    [[nodiscard]] bool IsLayoutMode()       const { return state_.chartMode == ChartMode::Layout; }
    [[nodiscard]] bool HasSelection()       const { return selection_.HasSelection(); }
    [[nodiscard]] bool HasSingleSelection() const { return selection_.HasSingleSelection(); }
    [[nodiscard]] bool HasMultiSelection()  const { return selection_.HasMultiSelection(); }
    void InvalidateSelection();             // invalidate just the selection region
    void FullUIRefresh();
    void NotifyStateChanged();              // sync inspector + buttons + invalidate + autosave

private:
    // --- Owned subsystems ---
    AppState       state_;
    ControlHandles controls_;
    AppLayout      layout_;
    Renderer       renderer_;
    SidebarManager sidebar_;

    // --- Window handles ---
    HWND hwnd_ = nullptr;
    HWND alignmentToolsWindow_ = nullptr;
    HWND objectInspectorWindow_ = nullptr;
    UINT dpi_  = 96;
    int  sidebarWidth_ = 0;
    bool resizingSidebar_      = false;
    bool sidebarRecalculating_ = false; // re-entrance guard for HandleSidebarMessage(WM_SIZE)
    bool inSizeMove_           = false; // true while user is dragging a resize/move handle

    // --- Auto-assign ---
    bool              aaRunning_       = false;
    uint64_t          aaStartRevision_ = 0;
    std::atomic<bool> aaCancel_        { false };
    HANDLE            aaThread_        = nullptr;
    size_t            aaProgressSteps_ = 0;
    size_t            aaProgressLimit_ = kDefaultAutoAssignSearchLimit;

    // --- Layout viewport transform (room-local → screen) ---
    // Recomputed in RecalculateLayout(); referenced by the sub-controllers.
    LayoutViewTransform layoutTx_{};

    // --- Layout sub-controllers (declared after the members they reference) ---
    SelectionManager selection_;
    LayoutEditor     editor_;

    // --- Rubber-band selection (screen coords; tracked here for painting) ---
    bool  rubberBandSelecting_ = false;
    POINT rubberBandStart_     {};
    POINT rubberBandEnd_       {};

    // --- Hover ---
    int  hoverItem_     = -1;
    bool trackingMouse_ = false;

    // --- Layout drag priming (threshold before BeginDrag fires) ---
    bool  layoutDragPrimed_   = false;
    POINT layoutDragStartPt_  {};

    // --- Front-edge drag ---
    bool     draggingFront_          = false;
    RoomEdge frontDragOriginalEdge_  = RoomEdge::Top;

    // --- Multi-class management ---
    struct ClassInfo { std::wstring name; std::wstring file; };
    std::vector<ClassInfo> classList_;
    int  activeClassIdx_ = 0;
    HWND classListBox_   = nullptr;
    HWND addClassBtn_    = nullptr;
    static constexpr int kClassStripW = 58; // logical units

    // --- Groups (transient, not saved) ---
    std::vector<std::vector<std::wstring>> generatedGroups_;
    int   groupSizePref_ = 2;
    float groupsZoom_    = 3.0f;   // Ctrl+Wheel zoom for the groups canvas (0.5 – 5.0)
    std::unordered_map<std::wstring, int> groupPairHistory_;
    std::unordered_map<std::wstring, std::unordered_set<int>> groupNumberHistory_;
    std::unordered_map<std::wstring, std::vector<std::unordered_set<std::wstring>>> groupPartnerSetHistory_;
    std::unordered_set<std::wstring> groupExactHistory_;
    std::wstring groupPermutationCacheKey_;
    std::wstring groupPermutationCacheValue_ = L"0";
    int footerProgressCurrentPct_ = 0;
    int footerProgressTargetPct_ = 0;
    struct GroupRuleAudit {
        bool avoidSameNumber = false;
        bool avoidSamePartners = false;
        bool avoidSameFullGroup = false;
        bool numberedSlotExhausted = false;
        bool roundsKnown = false;
        int groupSlotCount = 0;
        int exhaustedUsedSlots = 0;
        int totalRoundCapacity = 0;
        int usedRounds = 0;
        int remainingRounds = 0;
        int progressPercent = 0;
        std::wstring exhaustedStudent;
        std::wstring blockingReason;
        // Human-readable description of WHAT sets the rounds cap when it is
        // tighter than the plain numbered-slot bound — e.g. a keep-together
        // pair that can only ever sit as its own (bannable) pair composition
        // or inside the single larger slot.  Shown in the footer/summary so
        // the predicted rounds match what shuffling will actually allow.
        std::wstring capSource;
    };

    // Diagnostics filled by the exact-cover group solver (used for the
    // "Unable to Shuffle Groups" explanation; ignored by the round simulator).
    struct GroupSolveDiagnostics {
        bool attempted = false;
        bool skipped = false;
        bool impossible = false;
        bool budgetHit = false;
        uint64_t candidateNodes = 0;
        uint64_t searchNodes = 0;
        size_t candidateCount = 0;
        std::wstring detail;
    };

    // --- Inline cell editing (floating overlay EDIT on rosterView) ---
    struct CellEdit {
        HWND  wnd        = nullptr; // the floating EDIT control (child of sidebar)
        int   item       = -1;      // roster row index (or == roster.size() when adding)
        int   subItem    = -1;      // 1 = First Name col, 2 = Last Name col
        bool  isNew      = false;   // true while adding a brand-new row
        std::wstring pendingFirst;  // first name already typed (isNew + subItem==2 only)
    };
    CellEdit cellEdit_;
    void BeginInlineCellEdit(int item, int subItem);   // create floating edit
    void BeginAddStudentRow();   // append temp row + open first-name editor

    // --- Inline rule cell editing (floating EDIT + popup suggestion LISTBOX) ---
    struct RuleCellEdit {
        HWND edit           = nullptr; // floating WS_CHILD EDIT (child of sidebar)
        HWND suggestion     = nullptr; // WS_POPUP LISTBOX with roster matches
        int  row            = -1;      // index into restrictions or mustTogether
        int  col            = -1;      // 0 = Student A, 1 = Student B
        bool isApart        = true;    // true = restrictions, false = mustTogether
        bool suppressChange = false;   // blocks EN_CHANGE re-entrance while we update
        std::vector<std::wstring> matches; // current suggestion list contents
    };
    RuleCellEdit ruleCellEdit_;
    void BeginRuleCellEdit(int row, int col, bool isApart);

    // --- Roster drag assignment ---
    bool rosterDragPrimed_ = false;
    bool rosterDragging_ = false;
    POINT rosterDragStartScreen_ {};
    std::wstring rosterDragName_;
    DragPreviewState dragPreview_;

    // --- Per-message handlers ---
    LRESULT OnCreate(HWND hwnd);
    LRESULT OnSize();
    LRESULT OnKeyDown(WPARAM vk, bool ctrl, bool shift);
    LRESULT OnCommand(int id, int notif);
    LRESULT OnLButtonDown(POINT pt, bool shift);
    LRESULT OnLButtonDblClk(POINT pt);
    LRESULT OnMouseMove(POINT pt, WPARAM buttons);
    LRESULT OnMouseLeave();
    LRESULT OnContextMenu(HWND source, POINT screenPt);   // right-click layout context menu
    LRESULT OnSetCursor(WPARAM wParam, LPARAM lParam);
    LRESULT OnPaint();
    LRESULT OnDestroy();
    LRESULT OnAutoAssignDone(AutoAssignResult* result);
    LRESULT OnAutoAssignProgress(size_t steps);
    LRESULT OnTimer(UINT_PTR id);
    LRESULT OnThemeChange();
    LRESULT OnDpiChanged(UINT newDpi, const RECT* suggested);

    // --- Seat operations ---
    void ClearAllSeats();
    void BuildMenuBar();
    void RefreshAutoAssignFooter();
    void SetFooterProgressTarget(int pct, bool animate);
    void StepFooterProgressAnimation();
    void ShowAlignmentToolsWindow();
    void ShowObjectInspectorWindow();
    void LayoutFloatingTools();

    // --- Front-edge helpers ---
    [[nodiscard]] RECT     FrontIndicatorRect() const;
    [[nodiscard]] bool     HitTestFrontIndicator(POINT clientPt) const;
    [[nodiscard]] RoomEdge NearestRoomEdge(POINT clientPt) const;
    void                   CommitFrontEdge(RoomEdge edge);
    void PromptAndApplyRoomSize();

    // --- Class management ---
    void InitClassList();
    void SaveClassList();
    void SwitchToClass(int idx);
    void NewClass();
    void RenameClass(int idx, const std::wstring& name);
    void SyncClassListBox();
    [[nodiscard]] std::wstring GetClassFilePath(int idx) const;

    // --- Roster / rules dialogs ---
    bool PromptStudentName(const std::wstring& title,
                           std::wstring& first, std::wstring& last);
    bool PromptRulePair(const std::wstring& title,
                        std::wstring& nameA, std::wstring& nameB);
    bool PromptRulePairDropdown(const std::wstring& title,
                                std::wstring& nameA, std::wstring& nameB);
    bool PromptSingleText(const std::wstring& title,
                          const std::wstring& label,
                          std::wstring& value);
    void ToggleShowLastNames();

    // --- Groups ---
    void RefreshGroupConfigList();
    void RefreshGroupCombo();
    void RefreshGroupRuleToggleLabels();
    [[nodiscard]] bool GroupAvoidSameNumberEnabled() const;
    [[nodiscard]] bool GroupAvoidSamePartnersEnabled() const;
    [[nodiscard]] bool GroupAvoidSameFullGroupEnabled() const;
    [[nodiscard]] std::vector<int> CurrentGroupPattern() const;
    [[nodiscard]] std::wstring CanonicalGroupKey(const std::vector<std::wstring>& group) const;
    [[nodiscard]] std::wstring BuildGroupsSummaryText();
    [[nodiscard]] GroupRuleAudit BuildGroupRuleAudit(const std::vector<int>& pattern) const;
    // One valid grouping for `pattern` under the active rules + current history,
    // or empty if none / solver inapplicable. The SAME exact-cover engine
    // ShuffleGroups uses, so the round simulator can never disagree with it.
    [[nodiscard]] std::vector<std::vector<std::wstring>> SolveGroupingExactCover(
        const std::vector<int>& pattern, GroupSolveDiagnostics* diag = nullptr) const;
    // How many MORE shuffles can actually succeed from the current history,
    // found by simulating real shuffles with SolveGroupingExactCover. Mirrors
    // pressing the button, so the displayed countdown matches reality. Returns
    // -1 when the solver does not apply (roster > 60 / >63 keep-together groups).
    [[nodiscard]] int ComputeRemainingGroupRounds(const std::vector<int>& pattern);
    // Fresh (never-grouped) student pairings remaining + total possible (C(N,2)).
    // Size-aware automatically: groupPairHistory_ records EVERY within-group pair,
    // so a group of k consumes C(k,2) pairings — not just one.
    [[nodiscard]] long long FreshPairings(long long& outTotal) const;
    // --- Rule scoping (seating vs group) ---
    // The keep-apart/keep-together rule set the ACTIVE rule UI reads & writes:
    // group-only list on the Groups tab when its "Same as seating" box is
    // unchecked, otherwise the shared seating rule list.
    [[nodiscard]] std::vector<Restriction>& ActiveRuleVec(bool isApart);
    // The rules the GROUP GENERATOR uses (independent of which tab is showing).
    [[nodiscard]] const std::vector<Restriction>& GroupApartRules() const;
    [[nodiscard]] const std::vector<Restriction>& GroupTogetherRules() const;
    // Repopulate the rule ListViews from whatever the active tab should show.
    void SyncActiveRulesLists();
    [[nodiscard]] std::wstring BuildGroupPermutationCacheKey() const;
    [[nodiscard]] std::wstring ExactGroupPermutationText();
    [[nodiscard]] bool CandidateGroupsMeetConstraints(const std::vector<std::vector<std::wstring>>& groups) const;
    void RecordGroupShuffleHistory(const std::vector<std::vector<std::wstring>>& groups);
    void ShuffleGroups();
    void SyncGroupsOutput();
    void ResetGroupShuffleMemory();
    bool HitSidebarSplitter(POINT pt) const;
    void ResizeSidebarLive(POINT pt);
    void QuickFillSeats();
    void ExportSeatingCsv(); // Quick win CSV export of current seating + metadata
    void ShowSeatingReport(); // detailed printable summary report
    void ExportHtml(); // printable HTML export
    void RefreshFilteredRosterList(); // filter roster LB by name or tag from filter edit
    void ApplyRowsPreset(); // basic built-in preset for demo
    void ApplyUPreset();
    void ApplyHorseshoePreset();

    // --- Roster / restrictions ---
    void StartAutoAssign();
    void ApplyRoster(std::vector<std::wstring> roster);
    void ImportRosterFromEdit();
    void ApplyRestrictions(std::vector<Restriction> restrictions,
                           std::vector<Restriction> affinities = {},
                           std::vector<Restriction> mustTogether = {},
                           std::vector<std::vector<std::wstring>> groups = {});
    void ApplyGroupRules(std::vector<std::vector<std::wstring>> groups);
    bool LoadRosterFromFile(const std::wstring& path);
    bool PromptAndLoadRosterFile();
    void AssignRosterSelectionToSeat();
    void DeleteSelectedRosterStudents(); // right-click "Delete" on roster
    void BulkTagSelectedRoster(); // bulk apply tag to selected roster-table rows
    bool BeginRosterDragFromList(POINT listClientPt);
    bool BeginRosterDragFromView(POINT listClientPt);
    void UpdateRosterDrag(POINT screenPt);
    void EndRosterDrag(POINT screenPt, bool commit);

    // --- Layout inspector (reads the sidebar edit controls) ---
    void ApplyLayoutInspector();
    void OnInspectorSpin(int spinId, int delta);
    void SetLayoutSelectionHint();   // status hint reflecting current selection
    // Assign / clear the occupant of a furniture seat via a roster popup menu.
    void AssignLayoutSeatViaMenu(LayoutSeatRef seat, POINT screenAnchor);
    // Right-click menu for a seated student: colour/group + attribute tags + notes.
    void StudentContextMenu(LayoutSeatRef seat, POINT screenAnchor);
    // Modal dialog for editing per-student free-text notes.
    void ShowStudentNotesDialog(const std::wstring& name);

    // --- Mode ---
    void SetChartMode(ChartMode mode);
    void PerformUndo();
    void PerformRedo();

    // --- Hover (layout hit testing lives in SelectionManager) ---
    void UpdateHover(POINT screenPt);
    void EndRubberBand(POINT endPt);     // finalize a rubber-band drag

    void ApplyThemeToListViews(); // sync bg/text/selection colours after theme change

    // --- UI refresh ---
    void RecalculateLayout();
    void SyncAllEditsFromState();
};
