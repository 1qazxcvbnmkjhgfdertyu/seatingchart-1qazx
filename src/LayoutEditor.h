#pragma once
#include "AppState.h"
#include "Utils.h"
#include <memory>
#include <vector>
#include <windows.h>

class SeatingChartApp;

// ---------------------------------------------------------------------------
// LayoutEditor
//
// Owns all mutation of layout items in Layout mode: creation, deletion,
// duplication, z-ordering, transforms (rotate / flip / lock / align / nudge),
// interactive drag & resize, snapping/clamping to the room, the room-size
// control, and template load/save integration.
//
// All geometry is in room-local logical units; only the room→screen transform
// (held by the owning SeatingChartApp and passed in by reference) converts to
// screen space for drag/resize hit math. Every operation that commits a change
// drives the standard UI refresh through the owning SeatingChartApp, so the
// app itself stays a thin controller.
// ---------------------------------------------------------------------------
class LayoutEditor {
public:
    LayoutEditor(SeatingChartApp& app, AppState& state,
                 const LayoutViewTransform& tx)
        : app_(app), state_(state), tx_(tx) {}

    LayoutEditor(const LayoutEditor&)            = delete;
    LayoutEditor& operator=(const LayoutEditor&) = delete;

    // --- Item creation / removal ---
    void AddItem(LayoutItemType type);
    void AddBlock();      // a one-cell square desk block (grid-snapped)
    void AddTrapPair();   // 2 trapezoids joined into a hexagon pod
    void AddTrapPod();    // 4 trapezoids forming a wide collaborative pod (single composite item)
    void DeleteSelected();
    void DuplicateSelected();
    void CopySelected();
    void CutSelected();
    void PasteClipboard();
    void GroupSelected();
    void UngroupSelected();
    void MergeSelected(); // combine ≥2 selected blocks into one labelled region

    // --- Z-order ---
    void SendToBack();
    void BringToFront();

    // --- Transforms ---
    void Rotate(int deltaDeg);
    void Flip();
    void ToggleLock();
    void Align(AlignMode mode);
    void NudgeSelected(int dx, int dy);   // arrow-key nudge (room units)

    // --- Room size / front edge ---
    void ApplyRoomSize();
    void CycleFrontEdge();   // rotate which room edge is "the front"

    // --- Template integration (layout) ---
    bool SaveTemplate();   // returns true if a template was written
    bool ApplyTemplate();  // returns true if a template was loaded & applied
    void ToggleVisibility(); // toggle visible for selected items (layer)

    // --- Interactive drag / resize (all coords in room-local space) ---
    void BeginDrag(POINT screenPt);
    void BeginResize(POINT screenPt, ResizeHandle h);
    void UpdateDrag(POINT screenPt);
    void UpdateResize(POINT screenPt, bool keepAspect = false);
    void EndDrag();
    void CancelEdit(bool doRelease = true);
    void ClearDragState();                // abandon any in-progress edit (no restore)

    // --- Interactive rotation (PowerPoint-style; screen coords in) ---
    void BeginRotate(POINT screenPt);
    void UpdateRotate(POINT screenPt, bool snap);  // snap = Shift held (15° steps)
    void EndRotate();

    [[nodiscard]] bool IsDragging() const { return dragging_; }
    [[nodiscard]] bool IsResizing() const { return resizing_; }
    [[nodiscard]] bool IsRotating() const { return rotating_; }
    [[nodiscard]] bool IsEditing()  const { return dragging_ || resizing_ || rotating_; }

    // --- Geometry helpers (room-local) ---
    [[nodiscard]] RECT ClampToRoom(RECT rc) const;

private:
    void SnapToNeighbors(size_t idx);

    SeatingChartApp&           app_;
    AppState&                  state_;
    const LayoutViewTransform& tx_;

    // --- Drag / resize state (all coords room-local) ---
    bool         dragging_     = false;
    bool         resizing_     = false;
    bool         rotating_     = false;
    POINT        dragOffset_   {};       // room-local offset of mouse within primary item
    ResizeHandle activeHandle_ = ResizeHandle::None;
    RECT         resizeStartBounds_{};   // room-local bounds at resize start
    POINT        resizeStartPt_{};       // room-local mouse position at resize start
    POINT        resizePivot_{};         // room-local item centre at resize start
    int          resizeStartRot_ = 0;    // item rotation at resize start
    RECT         dragOriginalBounds_{};  // room-local bounds of primary item before drag

    // --- Rotation drag state ---
    int          rotateOriginalRot_ = 0;   // item rotation at rotate start
    double       rotateLastAngle_   = 0.0; // last mouse angle (deg) around centre
    double       rotateAccumDelta_  = 0.0; // accumulated normalized angle delta

    // Wrap an angle delta into (-180, 180] so accumulation crosses the
    // atan2 ±180° seam smoothly without jumps.
    static double NormalizeAngleDelta(double d) {
        while (d > 180.0)   d -= 360.0;
        while (d <= -180.0) d += 360.0;
        return d;
    }

    // Original room-local bounds of ALL selected items (for group drag).
    std::vector<RECT> groupDragOriginalBounds_;

    // Pending undo transaction held open for the duration of a drag/resize.
    std::unique_ptr<AppState::Transaction> pendingDragTx_;
    std::vector<LayoutItem> clipboardItems_;
};
