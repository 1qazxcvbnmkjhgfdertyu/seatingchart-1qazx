#pragma once
#include "AppState.h"
#include <atomic>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

// Posted when the background search completes.
// lParam = AutoAssignResult* (heap; recipient must delete).
constexpr UINT WM_APP_AUTOASSIGN_DONE     = WM_APP + 1;

// Posted every 10 000 backtracking steps so the UI can show progress.
// wParam = steps completed so far (truncated to WPARAM width).
constexpr UINT WM_APP_AUTOASSIGN_PROGRESS = WM_APP + 2;

struct AutoAssignResult {
    bool success        = false;
    bool searchLimitHit = false;
    std::wstring              errorMessage;
    std::vector<int>          assignments; // roster index → seat index (-1 = not placed)
    std::vector<std::wstring> roster;      // parallel copy of input roster
    double                    affinitySatisfaction = 0.0; // 0.0-1.0 fraction of soft affinities "met" (close or same item)
    size_t                    stepsUsed = 0; // backtrack steps taken
    double                    elapsedMs = 0.0; // time for solve
};

// ---------------------------------------------------------------------------
// Pure solver core (no UI / threading) — assembled from the chart by
// BeginAutoAssign, but exposed directly so it can be unit-tested.
// ---------------------------------------------------------------------------

struct AutoAssignInput {
    int                       seatCount = 0;
    std::vector<POINT>        seatCentres;     // room-coord seat centres, parallel to seats
    std::vector<int>          seatItem;        // furniture item index owning each seat
    std::vector<std::wstring> roster;
    std::vector<Restriction>  restrictions;    // carry per-rule keep-apart radius
    std::vector<Restriction>  mustTogether;    // hard must-sit-together (same item preferred or close)
    std::vector<std::wstring> fixedOccupants;  // parallel to seats ("" = empty)
    RoomEdge                  frontEdge = RoomEdge::Top;  // which edge is "the front"
    int                       roomW = 0, roomH = 0;       // for the front-band threshold
    std::vector<char>         rosterFrontRequired;        // parallel to roster (1 = must sit front)
    std::vector<Restriction>  affinities;                 // soft "sit near" preferences
    // Tag-derived auto rules (populated by caller from studentInfo; e.g. "Behavior" students keep apart)
    std::vector<std::wstring> behaviorStudents;           // names (canonical later) that must keep apart from each other
    size_t                    searchLimit = 500000;       // configurable backtrack limit
    std::vector<std::vector<std::wstring>> groupAffinities; // student groups to keep clustered
    std::vector<std::vector<std::wstring>> rosterForbidden; // parallel to roster: forbidden desk labels per student (for whitelisting)
    std::vector<std::wstring> seatLabels;                   // parallel to seatCentres: label or type of the furniture item for each seat
    std::vector<int> previousAssignments; // optional warm start: for roster students, preferred seat or -1
};

struct AutoAssignSolution {
    bool             success        = false;
    bool             searchLimitHit = false;
    std::wstring     errorMessage;
    std::vector<int> assignments;            // roster index → seat index (-1 = unplaced)
    double           affinitySatisfaction = 0.0; // 0.0-1.0 
    size_t           stepsUsed = 0;
    double           elapsedMs = 0.0; // time for solve
};

// Backtracking constraint solve. `cancel` and `progressWnd` may be null (tests
// pass null). A restriction with radius==0 means "not on the same furniture
// item"; radius>0 means "seat centres must be more than `radius` units apart".
AutoAssignSolution SolveAutoAssign(const AutoAssignInput& in,
                                   const std::atomic<bool>* cancel = nullptr,
                                   HWND progressWnd = nullptr);

// Validates preconditions synchronously, then launches a worker thread.
// Returns false (caller should show errorMessage) if preconditions fail.
// On success the thread posts WM_APP_AUTOASSIGN_DONE to notifyWnd;
// the caller must not destroy cancel/threadHandle until that message arrives.
[[nodiscard]] bool BeginAutoAssign(HWND notifyWnd, const AppState& state,
                                    std::atomic<bool>& cancel,
                                    HANDLE& threadHandle);

// Pure pre-solve validation. Returns list of human-readable problems (empty = OK).
// Can be called before launching the thread to show a warning dialog.
[[nodiscard]] std::vector<std::wstring> ValidateAutoAssignPreconditions(const AppState& state);
