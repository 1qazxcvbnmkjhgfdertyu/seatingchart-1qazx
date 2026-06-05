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
    bool resizingSidebar_ = false;

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

    // --- Front-edge drag ---
    bool     draggingFront_          = false;
    RoomEdge frontDragOriginalEdge_  = RoomEdge::Top;

    // --- Multi-class management ---
    struct ClassInfo { std::wstring name; std::wstring file; };
    std::vector<ClassInfo> classList_;
    int  activeClassIdx_ = 0;
    HWND classListBox_   = nullptr;
    HWND addClassBtn_    = nullptr;
    static constexpr int kClassStripW = 44; // logical units

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
    LRESULT OnSetCursor(LPARAM lParam);
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
    [[nodiscard]] std::wstring BuildGroupsSummaryText(const std::wstring& exactText) const;
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
    void BulkTagSelectedRoster(); // bulk apply tag to selected in roster list
    bool BeginRosterDragFromList(POINT listClientPt);
    void UpdateRosterDrag(POINT screenPt);
    void EndRosterDrag(POINT screenPt, bool commit);

    // --- Layout inspector (reads the sidebar edit controls) ---
    void ApplyLayoutInspector();
    void OnInspectorSpin(int spinId, int delta);
    void SetLayoutSelectionHint();   // status hint reflecting current selection
    // Assign / clear the occupant of a furniture seat via a roster popup menu.
    void AssignLayoutSeatViaMenu(LayoutSeatRef seat, POINT screenAnchor);
    // Right-click menu for a seated student: colour/group + attribute tags.
    void StudentContextMenu(LayoutSeatRef seat, POINT screenAnchor);

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
