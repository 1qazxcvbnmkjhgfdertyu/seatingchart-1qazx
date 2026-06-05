#include "LayoutEditor.h"
#include "SeatingChartApp.h"
#include "Controls.h"
#include "Templates.h"
#include "Utils.h"
#include "Undo.h"
#include <algorithm>
#include <climits>

namespace {

bool RectsOverlap(const RECT& a, const RECT& b) {
    RECT r{};
    return IntersectRect(&r, &a, &b) != FALSE;
}

RECT FindVisiblePlacement(RECT base, const std::vector<LayoutItem>& items,
                          int roomW, int roomH) {
    base = NormalizeRect(base);
    const int w = static_cast<int>(base.right - base.left);
    const int h = static_cast<int>(base.bottom - base.top);
    const int gap = 20;
    const int startL = static_cast<int>(base.left);
    const int startT = static_cast<int>(base.top);

    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            RECT candidate{
                startL + col * (w + gap),
                startT + row * (h + gap),
                startL + col * (w + gap) + w,
                startT + row * (h + gap) + h
            };
            if (candidate.right > roomW) {
                const int shifted = roomW - w;
                candidate.left = std::max(0, shifted);
                candidate.right = candidate.left + w;
            }
            if (candidate.bottom > roomH) {
                const int shifted = roomH - h;
                candidate.top = std::max(0, shifted);
                candidate.bottom = candidate.top + h;
            }
            bool overlaps = false;
            for (const auto& item : items) {
                if (RectsOverlap(candidate, item.bounds)) { overlaps = true; break; }
            }
            if (!overlaps) return candidate;
        }
    }

    const int offset = static_cast<int>(items.size() % 12) * gap;
    return {
        std::clamp(startL + offset, 0, std::max(0, roomW - w)),
        std::clamp(startT + offset, 0, std::max(0, roomH - h)),
        std::clamp(startL + offset, 0, std::max(0, roomW - w)) + w,
        std::clamp(startT + offset, 0, std::max(0, roomH - h)) + h
    };
}

} // namespace

// ---------------------------------------------------------------------------
// Room-local clamping
// ---------------------------------------------------------------------------

RECT LayoutEditor::ClampToRoom(RECT rc) const {
    return ClampToRoomBounds(rc,
                             EffectiveRoomW(state_.roomW),
                             EffectiveRoomH(state_.roomH),
                             Scale(20), Scale(20));
}

// ---------------------------------------------------------------------------
// Snap helpers (room-local coordinates)
// ---------------------------------------------------------------------------

void LayoutEditor::SnapToNeighbors(size_t idx) {
    if (idx >= state_.layoutItems.size()) return;
    const int rW = EffectiveRoomW(state_.roomW);
    const int rH = EffectiveRoomH(state_.roomH);
    constexpr int kSnap = 12; // snap radius in room units

    RECT rc = SnapRectToGrid(state_.layoutItems[idx].bounds, kRoomSnapGrid);
    const LONG w = rc.right - rc.left, h = rc.bottom - rc.top;

    // Snap to other items
    for (size_t i = 0; i < state_.layoutItems.size(); ++i) {
        if (i == idx) continue;
        const RECT& o = state_.layoutItems[i].bounds;
        if (std::abs(rc.left  - o.right)  <= kSnap) { rc.left  = o.right;           rc.right  = rc.left + w; }
        if (std::abs(rc.right - o.left)   <= kSnap) { rc.right = o.left;            rc.left   = rc.right - w; }
        if (std::abs(rc.top   - o.bottom) <= kSnap) { rc.top   = o.bottom;          rc.bottom = rc.top + h; }
        if (std::abs(rc.bottom- o.top)    <= kSnap) { rc.bottom= o.top;             rc.top    = rc.bottom - h; }
    }

    // Snap to room walls
    auto snapEdge = [&](LONG& edge, LONG wall) {
        if (std::abs(edge - wall) <= kSnap) edge = wall;
    };
    snapEdge(rc.left,   0);  if (rc.left   == 0)  rc.right  = w;
    snapEdge(rc.top,    0);  if (rc.top    == 0)  rc.bottom = h;
    snapEdge(rc.right,  rW); if (rc.right  == rW) rc.left   = rW - w;
    snapEdge(rc.bottom, rH); if (rc.bottom == rH) rc.top    = rH - h;

    state_.layoutItems[idx].bounds = ClampToRoom(NormalizeRect(rc));
}

// ---------------------------------------------------------------------------
// Item creation / removal
// ---------------------------------------------------------------------------

void LayoutEditor::AddItem(LayoutItemType type) {
    if (static_cast<int>(state_.layoutItems.size()) >= kMaxLayoutItemCount) return;
    AppState::Transaction tx(state_);

    // Default placement in room-local coords (room is 1200×800 by default).
    const int cx = EffectiveRoomW(state_.roomW) / 2;
    const int cy = EffectiveRoomH(state_.roomH) / 2;
    RECT rc{80, 20, 320, 80};
    switch (type) {
    case LayoutItemType::Smartboard:    rc = { 80,  20, 360,  80}; break;
    case LayoutItemType::TrapezoidDesk: rc = { 80, 120, 180, 190}; break;
    case LayoutItemType::RectangleDesk: rc = { 80, 200, 200, 260}; break;
    case LayoutItemType::Table4:        rc = { 80, 300, 240, 400}; break;
    case LayoutItemType::BigTable:      rc = { 80, 300, 320, 440}; break;
    // Pods are single objects, centred in the room.
    case LayoutItemType::TrapPair:      rc = { cx-150, cy-90,  cx+150, cy+90  }; break;
    case LayoutItemType::TrapPod:       rc = { cx-190, cy-115, cx+190, cy+115 }; break;
    }
    LayoutItem item;
    item.type     = type;
    rc = FindVisiblePlacement(rc, tx->layoutItems,
                              EffectiveRoomW(state_.roomW),
                              EffectiveRoomH(state_.roomH));
    item.bounds   = ClampToRoom(SnapRectToGrid(NormalizeRect(rc), kRoomSnapGrid));
    item.label    = std::wstring(LayoutTypeName(type));
    item.capacity = LayoutItemDefaultCapacity(type);
    EnsureSeatSlots(item);
    tx->layoutItems.push_back(item);
    const int newIdx = static_cast<int>(tx->layoutItems.size()) - 1;
    tx->selectedLayoutItem  = newIdx;
    tx->selectedLayoutItems = { newIdx };
    tx.Commit();
    app_.RefreshSelectionFlags();
    app_.SyncLayoutInspector();
    app_.RefreshButtons();
    app_.SetStatus(L"Added " + std::wstring(LayoutTypeName(type)));
    app_.InvalidateChart();
    app_.ScheduleSave();
}

// ---------------------------------------------------------------------------
// Block authoring — square, grid-snapped blocks that can be merged into a
// labelled region. Blocks reuse the existing LayoutItem model (a one-seat
// RectangleDesk for a student-desk block; merging yields a Smartboard region).
// ---------------------------------------------------------------------------

void LayoutEditor::AddBlock() {
    if (static_cast<int>(state_.layoutItems.size()) >= kMaxLayoutItemCount) return;
    AppState::Transaction tx(state_);
    const int cell = kBlockGridCell;
    // One square cell near the room centre, snapped to the block grid.
    const int cx = (EffectiveRoomW(state_.roomW) / 2 / cell) * cell;
    const int cy = (EffectiveRoomH(state_.roomH) / 2 / cell) * cell;
    LayoutItem item;
    item.type     = LayoutItemType::RectangleDesk;   // one-seat student-desk block
    RECT rc = FindVisiblePlacement(RECT{ cx, cy, cx + cell, cy + cell }, tx->layoutItems,
                                   EffectiveRoomW(state_.roomW),
                                   EffectiveRoomH(state_.roomH));
    item.bounds   = ClampToRoom(SnapRectToGrid(rc, cell));
    item.label    = std::wstring(LayoutTypeName(LayoutItemType::RectangleDesk));
    item.capacity = LayoutItemDefaultCapacity(LayoutItemType::RectangleDesk);
    EnsureSeatSlots(item);
    tx->layoutItems.push_back(item);
    const int newIdx = static_cast<int>(tx->layoutItems.size()) - 1;
    tx->selectedLayoutItem  = newIdx;
    tx->selectedLayoutItems = { newIdx };
    tx.Commit();
    app_.RefreshSelectionFlags();
    app_.SyncLayoutInspector();
    app_.RefreshButtons();
    app_.SetStatus(L"Added block");
    app_.InvalidateChart();
    app_.ScheduleSave();
}

void LayoutEditor::MergeSelected() {
    if (state_.selectedLayoutItems.size() < 2) return;
    AppState::Transaction tx(state_);
    // Union of the selected blocks (computed before erasing them).
    const RECT merged = ClampToRoom(MergedBlockBounds(tx->layoutItems, tx->selectedLayoutItems));
    const size_t count = tx->selectedLayoutItems.size();

    std::vector<int> sorted = tx->selectedLayoutItems;
    std::sort(sorted.rbegin(), sorted.rend());          // descending → indices stay valid
    for (int idx : sorted)
        if (idx >= 0 && idx < static_cast<int>(tx->layoutItems.size()))
            tx->layoutItems.erase(tx->layoutItems.begin() + idx);

    LayoutItem m;
    m.type     = LayoutItemType::Smartboard;            // a labelled, non-seat region
    m.bounds   = merged;
    m.label    = std::wstring(LayoutTypeName(LayoutItemType::Smartboard));
    m.capacity = LayoutItemDefaultCapacity(LayoutItemType::Smartboard);
    EnsureSeatSlots(m);
    tx->layoutItems.push_back(m);
    const int newIdx = static_cast<int>(tx->layoutItems.size()) - 1;
    tx->selectedLayoutItem  = newIdx;
    tx->selectedLayoutItems = { newIdx };
    tx->selectedLayoutSeat  = std::nullopt;
    tx.Commit();
    app_.RefreshSelectionFlags();
    app_.SyncLayoutInspector();
    app_.RefreshButtons();
    app_.SetStatus(L"Merged " + std::to_wstring(count) + L" blocks into a label");
    app_.InvalidateChart();
    app_.ScheduleSave();
}

// ---------------------------------------------------------------------------
// Trapezoid collaborative pods — each is a SINGLE fixed object (one LayoutItem),
// not a group of individually-selectable desks.
// ---------------------------------------------------------------------------

void LayoutEditor::AddTrapPair() { AddItem(LayoutItemType::TrapPair); }
void LayoutEditor::AddTrapPod()  { AddItem(LayoutItemType::TrapPod);  }

void LayoutEditor::DeleteSelected() {
    if (state_.selectedLayoutItems.empty()) return;
    AppState::Transaction tx(state_);
    std::vector<int> sorted = state_.selectedLayoutItems;
    std::sort(sorted.rbegin(), sorted.rend());
    for (int idx : sorted) {
        if (idx < 0 || idx >= static_cast<int>(tx->layoutItems.size())) continue;
        tx->layoutItems.erase(tx->layoutItems.begin() + idx);
    }
    tx->selectedLayoutItem  = std::nullopt;
    tx->selectedLayoutItems.clear();
    tx->selectedLayoutSeat  = std::nullopt;
    tx.Commit();
    app_.RefreshSelectionFlags();
    app_.SyncLayoutInspector();
    app_.RefreshButtons();
    app_.SetStatus(L"Deleted " + std::to_wstring(sorted.size()) + L" item(s)");
    app_.InvalidateChart();
    app_.ScheduleSave();
}

void LayoutEditor::DuplicateSelected() {
    if (state_.selectedLayoutItems.empty()) return;
    AppState::Transaction tx(state_);
    std::vector<int> newSelection;
    for (int idx : state_.selectedLayoutItems) {
        if (idx < 0 || idx >= static_cast<int>(tx->layoutItems.size())) continue;
        if (static_cast<int>(tx->layoutItems.size()) >= kMaxLayoutItemCount) break;
        LayoutItem copy = tx->layoutItems[static_cast<size_t>(idx)];
        copy.bounds   = ClampToRoom(OffsetRectCopy(copy.bounds, 20, 20));
        copy.selected = false;
        copy.locked   = false;
        copy.groupId  = 0;
        // Duplicating furniture should not duplicate the people sitting in it.
        for (auto& occ : copy.occupants) occ.clear();
        tx->layoutItems.push_back(copy);
        newSelection.push_back(static_cast<int>(tx->layoutItems.size()) - 1);
    }
    if (newSelection.empty()) return;
    tx->selectedLayoutItem  = newSelection.front();
    tx->selectedLayoutItems = std::move(newSelection);
    tx->selectedLayoutSeat  = std::nullopt;
    tx.Commit();
    app_.RefreshSelectionFlags();
    app_.SyncLayoutInspector();
    app_.RefreshButtons();
    app_.InvalidateChart();
    app_.SetStatus(L"Duplicated " + std::to_wstring(tx->selectedLayoutItems.size()) + L" item(s)");
    app_.ScheduleSave();
}

void LayoutEditor::CopySelected() {
    clipboardItems_.clear();
    for (int idx : state_.selectedLayoutItems) {
        if (idx < 0 || idx >= static_cast<int>(state_.layoutItems.size())) continue;
        LayoutItem copy = state_.layoutItems[static_cast<size_t>(idx)];
        copy.selected = false;
        for (auto& occ : copy.occupants) occ.clear();
        clipboardItems_.push_back(std::move(copy));
    }
    if (!clipboardItems_.empty())
        app_.SetStatus(L"Copied " + std::to_wstring(clipboardItems_.size()) + L" item(s)");
}

void LayoutEditor::CutSelected() {
    if (state_.selectedLayoutItems.empty()) return;
    CopySelected();
    DeleteSelected();
    if (!clipboardItems_.empty())
        app_.SetStatus(L"Cut " + std::to_wstring(clipboardItems_.size()) + L" item(s)");
}

void LayoutEditor::PasteClipboard() {
    if (clipboardItems_.empty()) return;
    AppState::Transaction tx(state_);
    std::vector<int> newSelection;
    const int roomW = EffectiveRoomW(state_.roomW);
    const int roomH = EffectiveRoomH(state_.roomH);

    for (const auto& src : clipboardItems_) {
        if (static_cast<int>(tx->layoutItems.size()) >= kMaxLayoutItemCount) break;
        LayoutItem item = src;
        item.selected = false;
        item.locked = false;
        item.groupId = 0;
        item.bounds = FindVisiblePlacement(OffsetRectCopy(item.bounds, 20, 20),
                                           tx->layoutItems, roomW, roomH);
        item.bounds = ClampToRoom(SnapRectToGrid(item.bounds, kRoomSnapGrid));
        tx->layoutItems.push_back(item);
        newSelection.push_back(static_cast<int>(tx->layoutItems.size()) - 1);
    }

    if (newSelection.empty()) return;
    tx->selectedLayoutItem = newSelection.front();
    tx->selectedLayoutItems = std::move(newSelection);
    tx->selectedLayoutSeat = std::nullopt;
    tx.Commit();
    app_.RefreshSelectionFlags();
    app_.SyncLayoutInspector();
    app_.RefreshButtons();
    app_.InvalidateChart();
    app_.SetStatus(L"Pasted " + std::to_wstring(tx->selectedLayoutItems.size()) + L" item(s)");
    app_.ScheduleSave();
}

void LayoutEditor::GroupSelected() {
    if (state_.selectedLayoutItems.size() < 2) return;
    int maxGroup = 0;
    for (const auto& item : state_.layoutItems) maxGroup = std::max(maxGroup, item.groupId);
    AppState::Transaction tx(state_);
    const int groupId = maxGroup + 1;
    int count = 0;
    for (int idx : state_.selectedLayoutItems) {
        if (idx < 0 || idx >= static_cast<int>(tx->layoutItems.size())) continue;
        tx->layoutItems[static_cast<size_t>(idx)].groupId = groupId;
        ++count;
    }
    tx.Commit();
    app_.RefreshSelectionFlags();
    app_.SyncLayoutInspector();
    app_.RefreshButtons();
    app_.InvalidateChart();
    app_.SetStatus(L"Grouped " + std::to_wstring(count) + L" item(s)");
    app_.ScheduleSave();
}

void LayoutEditor::UngroupSelected() {
    if (state_.selectedLayoutItems.empty()) return;
    AppState::Transaction tx(state_);
    int count = 0;
    for (int idx : state_.selectedLayoutItems) {
        if (idx < 0 || idx >= static_cast<int>(tx->layoutItems.size())) continue;
        if (tx->layoutItems[static_cast<size_t>(idx)].groupId != 0) {
            tx->layoutItems[static_cast<size_t>(idx)].groupId = 0;
            ++count;
        }
    }
    tx.Commit();
    app_.RefreshSelectionFlags();
    app_.SyncLayoutInspector();
    app_.RefreshButtons();
    app_.InvalidateChart();
    app_.SetStatus(count > 0 ? L"Ungrouped selected item(s)" : L"Selection is not grouped");
    app_.ScheduleSave();
}

// ---------------------------------------------------------------------------
// Z-order
// ---------------------------------------------------------------------------

void LayoutEditor::SendToBack() {
    if (!state_.selectedLayoutItem || *state_.selectedLayoutItem <= 0) return;
    AppState::Transaction tx(state_);
    const int idx = *tx->selectedLayoutItem;
    LayoutItem item = tx->layoutItems[static_cast<size_t>(idx)];
    tx->layoutItems.erase(tx->layoutItems.begin() + idx);
    tx->layoutItems.insert(tx->layoutItems.begin(), item);
    tx->selectedLayoutItem  = 0;
    tx->selectedLayoutItems = { 0 };
    tx.Commit();
    app_.RefreshSelectionFlags();
    app_.SyncLayoutInspector();
    app_.RefreshButtons();
    app_.InvalidateChart();
    app_.SetStatus(L"Sent layout item to back");
    app_.ScheduleSave();
}

void LayoutEditor::BringToFront() {
    if (!state_.selectedLayoutItem ||
        *state_.selectedLayoutItem >= static_cast<int>(state_.layoutItems.size()) - 1) return;
    AppState::Transaction tx(state_);
    const int idx = *tx->selectedLayoutItem;
    LayoutItem item = tx->layoutItems[static_cast<size_t>(idx)];
    tx->layoutItems.erase(tx->layoutItems.begin() + idx);
    tx->layoutItems.push_back(item);
    const int newIdx = static_cast<int>(tx->layoutItems.size()) - 1;
    tx->selectedLayoutItem  = newIdx;
    tx->selectedLayoutItems = { newIdx };
    tx.Commit();
    app_.RefreshSelectionFlags();
    app_.SyncLayoutInspector();
    app_.RefreshButtons();
    app_.InvalidateChart();
    app_.SetStatus(L"Brought layout item to front");
    app_.ScheduleSave();
}

// ---------------------------------------------------------------------------
// Transforms
// ---------------------------------------------------------------------------

void LayoutEditor::Rotate(int deltaDeg) {
    if (state_.selectedLayoutItems.empty()) return;
    AppState::Transaction tx(state_);
    for (int idx : state_.selectedLayoutItems) {
        if (idx < 0 || idx >= static_cast<int>(tx->layoutItems.size())) continue;
        LayoutItem& item = tx->layoutItems[static_cast<size_t>(idx)];
        if (item.locked) continue;
        // Rotation is a pure display transform about the item centre; bounds
        // stay as the unrotated footprint (no width/height swap).
        item.rotation = ((item.rotation + deltaDeg) % 360 + 360) % 360;
    }
    tx.Commit();
    app_.RefreshSelectionFlags();
    app_.SyncLayoutInspector();
    app_.RefreshButtons();
    app_.InvalidateChart();
    app_.SetStatus(L"Rotated " + std::to_wstring(state_.selectedLayoutItems.size()) + L" item(s)");
    app_.ScheduleSave();
}

void LayoutEditor::Flip() {
    if (state_.selectedLayoutItems.empty()) return;
    AppState::Transaction tx(state_);
    for (int idx : state_.selectedLayoutItems) {
        if (idx < 0 || idx >= static_cast<int>(tx->layoutItems.size())) continue;
        LayoutItem& item = tx->layoutItems[static_cast<size_t>(idx)];
        if (item.locked) continue;
        item.flipped = !item.flipped;
    }
    tx.Commit();
    app_.InvalidateChart();
    app_.SetStatus(L"Flipped selected item(s)");
    app_.ScheduleSave();
}

void LayoutEditor::ToggleLock() {
    if (state_.selectedLayoutItems.empty()) return;
    bool shouldLock = false;
    for (int idx : state_.selectedLayoutItems) {
        if (idx < 0 || idx >= static_cast<int>(state_.layoutItems.size())) continue;
        if (!state_.layoutItems[static_cast<size_t>(idx)].locked) {
            shouldLock = true;
            break;
        }
    }
    AppState::Transaction tx(state_);
    int changed = 0;
    for (int idx : state_.selectedLayoutItems) {
        if (idx < 0 || idx >= static_cast<int>(tx->layoutItems.size())) continue;
        tx->layoutItems[static_cast<size_t>(idx)].locked = shouldLock;
        ++changed;
    }
    tx.Commit();
    app_.SyncLayoutInspector();
    app_.RefreshButtons();
    app_.InvalidateChart();
    app_.SetStatus((shouldLock ? L"Locked " : L"Unlocked ") + std::to_wstring(changed) + L" item(s)");
    app_.ScheduleSave();
}

void LayoutEditor::Align(AlignMode mode) {
    if (state_.selectedLayoutItems.size() < 2) return;
    AppState::Transaction tx(state_);
    const auto& items = tx->layoutItems;

    // Compute bounding union of selected items in room coords.
    int minL = INT_MAX, minT = INT_MAX, maxR = INT_MIN, maxB = INT_MIN;
    for (int idx : state_.selectedLayoutItems) {
        if (idx < 0 || idx >= static_cast<int>(items.size())) continue;
        const RECT& b = items[static_cast<size_t>(idx)].bounds;
        minL = std::min(minL, static_cast<int>(b.left));
        minT = std::min(minT, static_cast<int>(b.top));
        maxR = std::max(maxR, static_cast<int>(b.right));
        maxB = std::max(maxB, static_cast<int>(b.bottom));
    }
    const int centerX = (minL + maxR) / 2;
    const int centerY = (minT + maxB) / 2;

    if (mode == AlignMode::DistributeH || mode == AlignMode::DistributeV) {
        std::vector<int> sorted = state_.selectedLayoutItems;
        if (mode == AlignMode::DistributeH) {
            std::sort(sorted.begin(), sorted.end(), [&](int a, int b2){
                return items[static_cast<size_t>(a)].bounds.left <
                       items[static_cast<size_t>(b2)].bounds.left; });
            int totalW = 0;
            for (int idx : sorted) {
                const auto& b = items[static_cast<size_t>(idx)].bounds;
                totalW += static_cast<int>(b.right - b.left);
            }
            const int gap = sorted.size() > 1
                ? ((maxR - minL) - totalW) / static_cast<int>(sorted.size() - 1) : 0;
            int x = minL;
            for (int idx : sorted) {
                LayoutItem& item = tx->layoutItems[static_cast<size_t>(idx)];
                const int w = static_cast<int>(item.bounds.right - item.bounds.left);
                item.bounds.left  = x; item.bounds.right  = x + w; x += w + gap;
            }
        } else {
            std::sort(sorted.begin(), sorted.end(), [&](int a, int b2){
                return items[static_cast<size_t>(a)].bounds.top <
                       items[static_cast<size_t>(b2)].bounds.top; });
            int totalH = 0;
            for (int idx : sorted) {
                const auto& b = items[static_cast<size_t>(idx)].bounds;
                totalH += static_cast<int>(b.bottom - b.top);
            }
            const int gap = sorted.size() > 1
                ? ((maxB - minT) - totalH) / static_cast<int>(sorted.size() - 1) : 0;
            int y = minT;
            for (int idx : sorted) {
                LayoutItem& item = tx->layoutItems[static_cast<size_t>(idx)];
                const int h = static_cast<int>(item.bounds.bottom - item.bounds.top);
                item.bounds.top = y; item.bounds.bottom = y + h; y += h + gap;
            }
        }
    } else {
        for (int idx : state_.selectedLayoutItems) {
            if (idx < 0 || idx >= static_cast<int>(tx->layoutItems.size())) continue;
            LayoutItem& item = tx->layoutItems[static_cast<size_t>(idx)];
            if (item.locked) continue;
            const int w = static_cast<int>(item.bounds.right  - item.bounds.left);
            const int h = static_cast<int>(item.bounds.bottom - item.bounds.top);
            switch (mode) {
            case AlignMode::Left:    item.bounds.left  = minL; item.bounds.right  = minL + w; break;
            case AlignMode::Right:   item.bounds.right = maxR; item.bounds.left   = maxR - w; break;
            case AlignMode::Top:     item.bounds.top   = minT; item.bounds.bottom = minT + h; break;
            case AlignMode::Bottom:  item.bounds.bottom= maxB; item.bounds.top    = maxB - h; break;
            case AlignMode::CenterH: item.bounds.left  = centerX - w/2; item.bounds.right  = centerX + w/2; break;
            case AlignMode::CenterV: item.bounds.top   = centerY - h/2; item.bounds.bottom = centerY + h/2; break;
            default: break;
            }
        }
    }
    tx.Commit();
    app_.RefreshSelectionFlags();
    app_.SyncLayoutInspector();
    app_.InvalidateChart();
    app_.SetStatus(L"Aligned selected items");
    app_.ScheduleSave();
}

void LayoutEditor::NudgeSelected(int dx, int dy) {
    if (state_.selectedLayoutItems.empty() || (dx == 0 && dy == 0)) return;
    AppState::Transaction tx(state_);
    for (int idx : state_.selectedLayoutItems) {
        if (idx < 0 || idx >= static_cast<int>(tx->layoutItems.size())) continue;
        if (tx->layoutItems[static_cast<size_t>(idx)].locked) continue;
        auto& b = tx->layoutItems[static_cast<size_t>(idx)].bounds;
        b = ClampToRoom(OffsetRectCopy(b, dx, dy));
    }
    tx.Commit();
    app_.SyncLayoutInspector();
    app_.InvalidateChart();
    app_.ScheduleSave();
}

// ---------------------------------------------------------------------------
// Room size
// ---------------------------------------------------------------------------

void LayoutEditor::ApplyRoomSize() {
    auto gi = [](HWND h) -> int {
        const auto t = GetWindowTextStr(h); if (t.empty()) return 0;
        wchar_t* e = nullptr;
        const long v = wcstol(t.c_str(), &e, 10);
        return (e == t.c_str() || v < 0) ? 0 : static_cast<int>(v);
    };
    const int w = gi(app_.Controls().roomWidthEdit);
    const int h = gi(app_.Controls().roomHeightEdit);
    AppState::Transaction tx(state_);
    tx->roomW = w;
    tx->roomH = h;
    tx.Commit();
    // Recompute transform immediately
    app_.RecomputeLayoutTransform();
    app_.InvalidateChart();
    app_.SetStatus(w > 0 && h > 0
        ? L"Room set to " + std::to_wstring(w) + L"\xD7" + std::to_wstring(h) + L" units"
        : L"Room size set to default (" + std::to_wstring(kDefaultRoomW)
          + L"\xD7" + std::to_wstring(kDefaultRoomH) + L" units)");
    app_.ScheduleSave();
}

void LayoutEditor::CycleFrontEdge() {
    AppState::Transaction tx(state_);
    switch (tx->frontEdge) {
    case RoomEdge::Top:    tx->frontEdge = RoomEdge::Right;  break;
    case RoomEdge::Right:  tx->frontEdge = RoomEdge::Bottom; break;
    case RoomEdge::Bottom: tx->frontEdge = RoomEdge::Left;   break;
    case RoomEdge::Left:   tx->frontEdge = RoomEdge::Top;    break;
    }
    tx.Commit();
    app_.InvalidateChart();
    // SetStatus refreshes the sidebar (including the "Front: …" button label).
    app_.SetStatus(L"Front of room: " + std::wstring(RoomEdgeName(state_.frontEdge)));
    app_.ScheduleSave();
}

// ---------------------------------------------------------------------------
// Template integration (layout)
// ---------------------------------------------------------------------------

bool LayoutEditor::SaveTemplate() {
    return !PromptSaveTemplate(app_.Hwnd(), state_).empty();
}

bool LayoutEditor::ApplyTemplate() {
    AppState::Transaction tx(state_);
    if (!PromptLoadTemplate(app_.Hwnd(), &state_)) return false;
    tx.Commit();
    app_.RecomputeLayoutTransform();
    app_.FullUIRefresh();
    app_.SetStatus(L"Template applied");
    app_.ScheduleSave();
    return true;
}

void LayoutEditor::ToggleVisibility() {
    if (state_.selectedLayoutItems.empty() && state_.selectedLayoutItem) {
        state_.selectedLayoutItems = { *state_.selectedLayoutItem };
    }
    if (state_.selectedLayoutItems.empty()) return;
    bool allVisible = true;
    for (int idx : state_.selectedLayoutItems) {
        if (idx >= 0 && idx < static_cast<int>(state_.layoutItems.size())) {
            if (!state_.layoutItems[idx].visible) allVisible = false;
        }
    }
    bool newVis = !allVisible;
    for (int idx : state_.selectedLayoutItems) {
        if (idx >= 0 && idx < static_cast<int>(state_.layoutItems.size())) {
            state_.layoutItems[idx].visible = newVis;
        }
    }
    app_.FullUIRefresh();
    app_.SetStatus(L"Visibility toggled for selection");
}

// ---------------------------------------------------------------------------
// Interactive drag / resize (all coords in room-local space)
// ---------------------------------------------------------------------------

void LayoutEditor::BeginDrag(POINT screenPt) {
    if (state_.selectedLayoutItems.empty()) return;
    if (!state_.selectedLayoutItem) return;
    const size_t pri = static_cast<size_t>(*state_.selectedLayoutItem);
    if (state_.layoutItems[pri].locked) return;

    dragging_ = true;
    const RECT& rb = state_.layoutItems[pri].bounds;
    dragOriginalBounds_ = rb;
    const POINT roomPt  = ScreenToRoomPoint(screenPt, tx_);
    dragOffset_ = { roomPt.x - static_cast<int>(rb.left),
                    roomPt.y - static_cast<int>(rb.top) };

    groupDragOriginalBounds_.clear();
    for (int idx : state_.selectedLayoutItems)
        groupDragOriginalBounds_.push_back(state_.layoutItems[static_cast<size_t>(idx)].bounds);

    pendingDragTx_ = std::make_unique<AppState::Transaction>(state_);
    SetCapture(app_.Hwnd());
}

void LayoutEditor::BeginResize(POINT screenPt, ResizeHandle h) {
    if (!state_.selectedLayoutItem || h == ResizeHandle::None) return;
    const size_t idx = static_cast<size_t>(*state_.selectedLayoutItem);
    if (state_.layoutItems[idx].locked) return;
    resizing_          = true;
    activeHandle_      = h;
    resizeStartPt_     = ScreenToRoomPoint(screenPt, tx_);
    resizeStartBounds_ = state_.layoutItems[idx].bounds;
    resizeStartRot_    = state_.layoutItems[idx].rotation;
    resizePivot_       = { (resizeStartBounds_.left + resizeStartBounds_.right) / 2,
                           (resizeStartBounds_.top  + resizeStartBounds_.bottom) / 2 };
    dragOriginalBounds_= resizeStartBounds_;
    groupDragOriginalBounds_ = { resizeStartBounds_ };
    pendingDragTx_ = std::make_unique<AppState::Transaction>(state_);
    SetCapture(app_.Hwnd());
}

void LayoutEditor::UpdateDrag(POINT screenPt) {
    if (!dragging_ || !state_.selectedLayoutItem) return;
    const POINT roomPt  = ScreenToRoomPoint(screenPt, tx_);
    const int rW = EffectiveRoomW(state_.roomW);
    const int rH = EffectiveRoomH(state_.roomH);

    const size_t priIdx = static_cast<size_t>(*state_.selectedLayoutItem);
    auto& priItem = state_.layoutItems[priIdx];
    const int w  = static_cast<int>(priItem.bounds.right  - priItem.bounds.left);
    const int h  = static_cast<int>(priItem.bounds.bottom - priItem.bounds.top);
    const int newL = std::clamp(static_cast<int>(roomPt.x) - static_cast<int>(dragOffset_.x), 0, rW - w);
    const int newT = std::clamp(static_cast<int>(roomPt.y) - static_cast<int>(dragOffset_.y), 0, rH - h);
    const int dl   = newL - static_cast<int>(dragOriginalBounds_.left);
    const int dt   = newT - static_cast<int>(dragOriginalBounds_.top);
    priItem.bounds = { newL, newT, newL+w, newT+h };

    for (size_t k = 0; k < state_.selectedLayoutItems.size(); ++k) {
        const int idx = state_.selectedLayoutItems[k];
        if (idx == static_cast<int>(priIdx)) continue;
        if (idx < 0 || idx >= static_cast<int>(state_.layoutItems.size())) continue;
        if (k >= groupDragOriginalBounds_.size()) continue;
        const RECT& orig = groupDragOriginalBounds_[k];
        const int iw = static_cast<int>(orig.right - orig.left);
        const int ih = static_cast<int>(orig.bottom - orig.top);
        const int il = std::clamp(static_cast<int>(orig.left) + dl, 0, rW - iw);
        const int it = std::clamp(static_cast<int>(orig.top)  + dt, 0, rH - ih);
        state_.layoutItems[static_cast<size_t>(idx)].bounds = { il, it, il+iw, it+ih };
    }

    app_.SyncLayoutInspector();
    app_.InvalidateChart();
}

void LayoutEditor::UpdateResize(POINT screenPt, bool keepAspect) {
    if (!resizing_ || !state_.selectedLayoutItem) return;
    auto& item = state_.layoutItems[static_cast<size_t>(*state_.selectedLayoutItem)];
    POINT roomPt   = ScreenToRoomPoint(screenPt, tx_);
    POINT startPt  = resizeStartPt_;
    // For rotated items, transform both the start and current mouse positions
    // into the item's unrotated local frame (rotate by -rotation about the
    // start centre) before computing the edge deltas. This keeps the bounds
    // axis-aligned and uncorrupted regardless of rotation.
    if (resizeStartRot_ != 0) {
        const double inv = -static_cast<double>(resizeStartRot_);
        roomPt  = RotatePointAround(roomPt,  resizePivot_, inv);
        startPt = RotatePointAround(startPt, resizePivot_, inv);
    }
    const int dx = roomPt.x - static_cast<int>(startPt.x);
    const int dy = roomPt.y - static_cast<int>(startPt.y);
    const int minSz = Scale(20); // minimum item size in room units

    RECT rc = resizeStartBounds_;
    switch (activeHandle_) {
    case ResizeHandle::TopLeft:     rc.left  += dx; rc.top    += dy; break;
    case ResizeHandle::TopRight:    rc.right += dx; rc.top    += dy; break;
    case ResizeHandle::BottomLeft:  rc.left  += dx; rc.bottom += dy; break;
    case ResizeHandle::BottomRight: rc.right += dx; rc.bottom += dy; break;
    case ResizeHandle::None: return;
    }
    if (keepAspect) {
        const int startW = std::max(1L, resizeStartBounds_.right - resizeStartBounds_.left);
        const int startH = std::max(1L, resizeStartBounds_.bottom - resizeStartBounds_.top);
        int w = std::max(minSz, static_cast<int>(std::abs(rc.right - rc.left)));
        int h = std::max(minSz, static_cast<int>(std::abs(rc.bottom - rc.top)));
        const double wChange = std::abs(static_cast<double>(w) / startW - 1.0);
        const double hChange = std::abs(static_cast<double>(h) / startH - 1.0);
        if (wChange >= hChange) h = std::max(minSz, static_cast<int>(std::lround(w * static_cast<double>(startH) / startW)));
        else                    w = std::max(minSz, static_cast<int>(std::lround(h * static_cast<double>(startW) / startH)));

        switch (activeHandle_) {
        case ResizeHandle::TopLeft:
            rc.left = resizeStartBounds_.right - w;
            rc.top = resizeStartBounds_.bottom - h;
            rc.right = resizeStartBounds_.right;
            rc.bottom = resizeStartBounds_.bottom;
            break;
        case ResizeHandle::TopRight:
            rc.left = resizeStartBounds_.left;
            rc.top = resizeStartBounds_.bottom - h;
            rc.right = resizeStartBounds_.left + w;
            rc.bottom = resizeStartBounds_.bottom;
            break;
        case ResizeHandle::BottomLeft:
            rc.left = resizeStartBounds_.right - w;
            rc.top = resizeStartBounds_.top;
            rc.right = resizeStartBounds_.right;
            rc.bottom = resizeStartBounds_.top + h;
            break;
        case ResizeHandle::BottomRight:
            rc.left = resizeStartBounds_.left;
            rc.top = resizeStartBounds_.top;
            rc.right = resizeStartBounds_.left + w;
            rc.bottom = resizeStartBounds_.top + h;
            break;
        case ResizeHandle::None:
            break;
        }
    }
    // Enforce minimum size
    if (rc.right - rc.left < minSz) {
        if (activeHandle_==ResizeHandle::TopLeft||activeHandle_==ResizeHandle::BottomLeft) rc.left  = rc.right - minSz;
        else                                                                                rc.right = rc.left  + minSz;
    }
    if (rc.bottom - rc.top < minSz) {
        if (activeHandle_==ResizeHandle::TopLeft||activeHandle_==ResizeHandle::TopRight)   rc.top    = rc.bottom - minSz;
        else                                                                                rc.bottom = rc.top    + minSz;
    }
    item.bounds = ClampToRoom(rc);
    app_.SyncLayoutInspector();
    app_.InvalidateChart();
}

void LayoutEditor::EndDrag() {
    if (!dragging_ && !resizing_) return;

    const bool wasResizing = resizing_;
    dragging_ = resizing_ = false;
    activeHandle_ = ResizeHandle::None;
    ReleaseCapture();

    if (state_.selectedLayoutItem) {
        const size_t idx = static_cast<size_t>(*state_.selectedLayoutItem);
        auto& item = state_.layoutItems[idx];

        if (!wasResizing) {
            SnapToNeighbors(idx);
        } else {
            item.bounds = ClampToRoom(SnapRectToGrid(item.bounds, kRoomSnapGrid));
        }

        // === Lightweight undo optimization ===
        // Use specific Move/Resize commands instead of heavy full-state snapshot.
        if (!wasResizing) {
            // Move
            const RECT& da = dragOriginalBounds_;
            const RECT& db = item.bounds;
            if (da.left != db.left || da.top != db.top || da.right != db.right || da.bottom != db.bottom) {
                state_.GetUndoManager().ExecuteCommand(
                    std::make_unique<MoveLayoutItemCommand>(item.bounds, dragOriginalBounds_, item.bounds)
                );
            }
        } else {
            // Resize
            const RECT& a = resizeStartBounds_;
            const RECT& b = item.bounds;
            if (a.left != b.left || a.top != b.top || a.right != b.right || a.bottom != b.bottom) {
                state_.GetUndoManager().ExecuteCommand(
                    std::make_unique<ResizeLayoutItemCommand>(item.bounds, resizeStartBounds_, item.bounds)
                );
            }
        }

        // Legacy Transaction still committed for now (side effects / future cleanup)
        if (pendingDragTx_) {
            pendingDragTx_->Commit();
            pendingDragTx_.reset();
        }

        app_.RefreshSelectionFlags();
        app_.SyncLayoutInspector();
        app_.ScheduleSave();
    } else {
        pendingDragTx_.reset();
    }

    app_.InvalidateChart();
}

// ---------------------------------------------------------------------------
// Interactive rotation (PowerPoint-style)
// ---------------------------------------------------------------------------

void LayoutEditor::BeginRotate(POINT screenPt) {
    if (!state_.selectedLayoutItem || state_.selectedLayoutItems.size() != 1) return;
    const size_t idx = static_cast<size_t>(*state_.selectedLayoutItem);
    if (state_.layoutItems[idx].locked) return;

    rotating_ = true;
    rotateOriginalRot_ = state_.layoutItems[idx].rotation;
    rotateAccumDelta_  = 0.0;

    // Mouse angle about the item's screen centre at the start of the drag.
    const RECT  sr = RoomToScreenRect(state_.layoutItems[idx].bounds, tx_);
    const POINT c{ (sr.left + sr.right) / 2, (sr.top + sr.bottom) / 2 };
    rotateLastAngle_ = std::atan2(static_cast<double>(screenPt.y - c.y),
                                  static_cast<double>(screenPt.x - c.x)) * 180.0 / kPi;

    pendingDragTx_ = std::make_unique<AppState::Transaction>(state_);
    SetCapture(app_.Hwnd());
}

void LayoutEditor::UpdateRotate(POINT screenPt, bool snap) {
    if (!rotating_ || !state_.selectedLayoutItem) return;
    const size_t idx = static_cast<size_t>(*state_.selectedLayoutItem);
    LayoutItem& item = state_.layoutItems[idx];

    const RECT  sr = RoomToScreenRect(item.bounds, tx_);
    const POINT c{ (sr.left + sr.right) / 2, (sr.top + sr.bottom) / 2 };
    const double angle = std::atan2(static_cast<double>(screenPt.y - c.y),
                                    static_cast<double>(screenPt.x - c.x)) * 180.0 / kPi;
    // Accumulate normalized deltas so we never jump at the atan2 ±180° seam.
    rotateAccumDelta_ += NormalizeAngleDelta(angle - rotateLastAngle_);
    rotateLastAngle_   = angle;
    double newRot = static_cast<double>(rotateOriginalRot_) + rotateAccumDelta_;
    if (snap) newRot = std::round(newRot / 15.0) * 15.0;  // 15° increments

    int r = static_cast<int>(std::lround(newRot)) % 360;
    if (r < 0) r += 360;
    item.rotation = r;

    app_.SetStatus(L"Rotation " + std::to_wstring(r) + L"\xB0");
    app_.InvalidateChart();
}

void LayoutEditor::EndRotate() {
    if (!rotating_) return;
    rotating_ = false;
    ReleaseCapture();
    if (state_.selectedLayoutItem && pendingDragTx_) {
        pendingDragTx_->Commit();
        pendingDragTx_.reset();
        app_.RefreshButtons();
        app_.ScheduleSave();
    } else {
        pendingDragTx_.reset();
    }
    app_.InvalidateChart();
}

void LayoutEditor::CancelEdit(bool doRelease) {
    if (!dragging_ && !resizing_ && !rotating_) return;
    const bool wasRotating = rotating_;
    dragging_ = resizing_ = rotating_ = false;
    activeHandle_ = ResizeHandle::None;
    if (doRelease) ReleaseCapture();
    if (wasRotating) {
        if (state_.selectedLayoutItem &&
            *state_.selectedLayoutItem < static_cast<int>(state_.layoutItems.size()))
            state_.layoutItems[static_cast<size_t>(*state_.selectedLayoutItem)].rotation =
                rotateOriginalRot_;
    } else {
        for (size_t k = 0; k < state_.selectedLayoutItems.size(); ++k) {
            const int idx = state_.selectedLayoutItems[k];
            if (idx < 0 || idx >= static_cast<int>(state_.layoutItems.size())) continue;
            if (k < groupDragOriginalBounds_.size())
                state_.layoutItems[static_cast<size_t>(idx)].bounds = groupDragOriginalBounds_[k];
        }
    }
    app_.RefreshSelectionFlags();
    app_.SyncLayoutInspector();
    pendingDragTx_.reset();
    app_.InvalidateChart();
}

void LayoutEditor::ClearDragState() {
    dragging_ = resizing_ = rotating_ = false;
    activeHandle_ = ResizeHandle::None;
    pendingDragTx_.reset();
}
