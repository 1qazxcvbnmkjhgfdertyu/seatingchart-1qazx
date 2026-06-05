#pragma once
#include "AppState.h"
#include "Utils.h"
#include <windows.h>

class SeatingChartApp;

// ---------------------------------------------------------------------------
// SelectionManager
//
// Owns everything about *which* layout items are selected and *what* the user
// is pointing at. It manipulates the selection fields on AppState
// (selectedLayoutItem / selectedLayoutItems) and performs all layout hit
// testing against the current room→screen transform.
//
// Two kinds of methods live here:
//   * Pure state helpers (SetSingleSelection / ClearSelection /
//     RefreshSelectionFlags) — they only touch AppState and leave UI refresh
//     to the caller, exactly like the original SeatingChartApp helpers.
//   * Full operations (SelectAll / ToggleInSelection / FinalizeRubberBand) —
//     they additionally drive the standard UI refresh through the owning
//     SeatingChartApp, mirroring the behaviour of the functions they replace.
//
// Hit-testing methods are const and have no side effects.
// ---------------------------------------------------------------------------
class SelectionManager {
public:
    SelectionManager(SeatingChartApp& app, AppState& state,
                     const LayoutViewTransform& tx)
        : app_(app), state_(state), tx_(tx) {}

    SelectionManager(const SelectionManager&)            = delete;
    SelectionManager& operator=(const SelectionManager&) = delete;

    // --- Pure selection-state mutation (caller refreshes UI) ---
    void RefreshSelectionFlags();
    void SetSingleSelection(int idx);
    void ClearSelection();

    // --- Full selection operations (drive their own UI refresh) ---
    void SelectAll();
    void ToggleInSelection(int idx);                   // Shift+click add / remove
    // Compute the new selection from a screen-space rubber band. Returns the
    // number of items selected, or -1 if the band was too small to count as a
    // drag (treated as a click — selection left unchanged).
    int  FinalizeRubberBand(POINT start, POINT end);

    // --- Queries ---
    [[nodiscard]] bool HasSelection()       const { return !state_.selectedLayoutItems.empty(); }
    [[nodiscard]] bool HasSingleSelection() const { return state_.selectedLayoutItems.size() == 1; }
    [[nodiscard]] bool HasMultiSelection()  const { return state_.selectedLayoutItems.size() > 1; }
    [[nodiscard]] int  Primary()            const { return state_.selectedLayoutItem.value_or(-1); }
    [[nodiscard]] bool IsSelected(int idx)  const;

    // --- Hit testing (screen coords in; uses the room transform) ---
    [[nodiscard]] int          HitTestItem(POINT screenPt)   const;
    // The furniture seat slot under the point, if any (topmost item wins).
    [[nodiscard]] std::optional<LayoutSeatRef> HitTestSeatSlot(POINT screenPt) const;
    [[nodiscard]] ResizeHandle HitTestHandle(POINT screenPt) const;
    // Screen-space RECT of a resize handle for the given room-local bounds.
    // (Uses the rotation of the single selected item, if any.)
    [[nodiscard]] RECT         HandleRect(const RECT& roomBounds, ResizeHandle h) const;

    // PowerPoint-style rotation handle (single, unlocked selection only).
    // True when the screen point is over the rotation handle.
    [[nodiscard]] bool HitTestRotateHandle(POINT screenPt) const;
    // Whether a single unlocked item is selected (so handles/rotate are shown).
    [[nodiscard]] bool HasRotatableSelection() const;

private:
    SeatingChartApp&           app_;
    AppState&                  state_;
    const LayoutViewTransform& tx_;
};
