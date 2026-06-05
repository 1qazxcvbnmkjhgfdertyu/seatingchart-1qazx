#include "SelectionManager.h"
#include "SeatingChartApp.h"
#include "Controls.h"
#include "Utils.h"
#include <algorithm>

// ---------------------------------------------------------------------------
// Pure selection-state mutation
// ---------------------------------------------------------------------------

void SelectionManager::RefreshSelectionFlags() {
    for (size_t i = 0; i < state_.layoutItems.size(); ++i)
        state_.layoutItems[i].selected = IsSelected(static_cast<int>(i));
}

void SelectionManager::SetSingleSelection(int idx) {
    state_.selectedLayoutItem = (idx >= 0) ? std::optional<int>(idx) : std::nullopt;
    state_.selectedLayoutItems.clear();
    if (idx >= 0) {
        const int gid = idx < static_cast<int>(state_.layoutItems.size())
            ? state_.layoutItems[static_cast<size_t>(idx)].groupId : 0;
        if (gid > 0) {
            for (int i = 0; i < static_cast<int>(state_.layoutItems.size()); ++i)
                if (state_.layoutItems[static_cast<size_t>(i)].groupId == gid)
                    state_.selectedLayoutItems.push_back(i);
        } else {
            state_.selectedLayoutItems.push_back(idx);
        }
    }
    state_.selectedLayoutSeat = std::nullopt;
    RefreshSelectionFlags();
}

void SelectionManager::ClearSelection() {
    state_.selectedLayoutItem = std::nullopt;
    state_.selectedLayoutItems.clear();
    state_.selectedLayoutSeat = std::nullopt;
    RefreshSelectionFlags();
}

// ---------------------------------------------------------------------------
// Full selection operations (drive their own UI refresh)
// ---------------------------------------------------------------------------

void SelectionManager::SelectAll() {
    if (state_.layoutItems.empty()) return;
    state_.selectedLayoutItems.clear();
    for (int i = 0; i < static_cast<int>(state_.layoutItems.size()); ++i)
        state_.selectedLayoutItems.push_back(i);
    state_.selectedLayoutItem = state_.selectedLayoutItems[0];
    RefreshSelectionFlags();
    app_.SyncLayoutInspector();
    app_.RefreshButtons();
    app_.InvalidateChart();
    app_.SetStatus(L"Selected all " + std::to_wstring(state_.selectedLayoutItems.size()) + L" items");
}

void SelectionManager::ToggleInSelection(int idx) {
    if (idx < 0) return;
    std::vector<int> targets;
    const int gid = idx < static_cast<int>(state_.layoutItems.size())
        ? state_.layoutItems[static_cast<size_t>(idx)].groupId : 0;
    if (gid > 0) {
        for (int i = 0; i < static_cast<int>(state_.layoutItems.size()); ++i)
            if (state_.layoutItems[static_cast<size_t>(i)].groupId == gid)
                targets.push_back(i);
    } else {
        targets.push_back(idx);
    }
    auto& sel = state_.selectedLayoutItems;
    const bool adding = std::any_of(targets.begin(), targets.end(), [&](int t) {
        return std::find(sel.begin(), sel.end(), t) == sel.end();
    });
    if (adding) {
        for (int t : targets)
            if (std::find(sel.begin(), sel.end(), t) == sel.end()) sel.push_back(t);
        std::sort(sel.begin(), sel.end());
        state_.selectedLayoutItem = idx;
    } else {
        for (int t : targets)
            sel.erase(std::remove(sel.begin(), sel.end(), t), sel.end());
        state_.selectedLayoutItem = sel.empty()
            ? std::nullopt : std::optional<int>(sel.back());
    }
    RefreshSelectionFlags();
    app_.SyncLayoutInspector();
    app_.RefreshButtons();
    app_.InvalidateChart();
}

int SelectionManager::FinalizeRubberBand(POINT start, POINT end) {
    const RECT screenSel{
        std::min(start.x, end.x),
        std::min(start.y, end.y),
        std::max(start.x, end.x),
        std::max(start.y, end.y)
    };

    // Too small to be a deliberate drag — treat as a click, leave selection.
    if (screenSel.right - screenSel.left < 4 && screenSel.bottom - screenSel.top < 4)
        return -1;

    // Convert the rubber band from screen to room coords for item intersection.
    const RECT roomSel = ScreenToRoomRect(screenSel, tx_);

    state_.selectedLayoutItems.clear();
    for (int i = 0; i < static_cast<int>(state_.layoutItems.size()); ++i) {
        RECT isect{};
        if (IntersectRect(&isect, &state_.layoutItems[static_cast<size_t>(i)].bounds, &roomSel))
            state_.selectedLayoutItems.push_back(i);
    }
    state_.selectedLayoutItem = state_.selectedLayoutItems.empty()
        ? std::nullopt : std::optional<int>(state_.selectedLayoutItems[0]);
    RefreshSelectionFlags();
    app_.SyncLayoutInspector();
    app_.RefreshButtons();
    if (!state_.selectedLayoutItems.empty())
        app_.SetStatus(L"Selected " + std::to_wstring(state_.selectedLayoutItems.size()) + L" item(s)");
    return static_cast<int>(state_.selectedLayoutItems.size());
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

bool SelectionManager::IsSelected(int idx) const {
    return std::find(state_.selectedLayoutItems.begin(),
                     state_.selectedLayoutItems.end(), idx)
           != state_.selectedLayoutItems.end();
}

// ---------------------------------------------------------------------------
// Hit testing — convert screen point to room before testing item bounds
// ---------------------------------------------------------------------------

int SelectionManager::HitTestItem(POINT screenPt) const {
    const POINT roomPt = ScreenToRoomPoint(screenPt, tx_);
    for (int i = static_cast<int>(state_.layoutItems.size()) - 1; i >= 0; --i) {
        const LayoutItem& item = state_.layoutItems[static_cast<size_t>(i)];
        // Convert the screen point into the item's unrotated local frame by
        // rotating the room point by -rotation about the item's room centre,
        // then test against the axis-aligned bounds.
        POINT testPt = roomPt;
        if (item.rotation != 0) {
            const POINT c{ (item.bounds.left + item.bounds.right) / 2,
                           (item.bounds.top  + item.bounds.bottom) / 2 };
            testPt = RotatePointAround(roomPt, c, -static_cast<double>(item.rotation));
        }
        if (PtInRectEx(item.bounds, testPt)) return i;
    }
    return -1;
}

std::optional<LayoutSeatRef> SelectionManager::HitTestSeatSlot(POINT screenPt) const {
    const POINT roomPt = ScreenToRoomPoint(screenPt, tx_);
    for (int i = static_cast<int>(state_.layoutItems.size()) - 1; i >= 0; --i) {
        const LayoutItem& item = state_.layoutItems[static_cast<size_t>(i)];
        const auto slots = LayoutSeatSlots(item);
        if (slots.empty()) continue;
        POINT testPt = roomPt;
        if (item.rotation != 0) {
            const POINT c{ (item.bounds.left + item.bounds.right) / 2,
                           (item.bounds.top  + item.bounds.bottom) / 2 };
            testPt = RotatePointAround(roomPt, c, -static_cast<double>(item.rotation));
        }
        for (int s = 0; s < static_cast<int>(slots.size()); ++s)
            if (PtInRectEx(slots[static_cast<size_t>(s)], testPt))
                return LayoutSeatRef{ i, s };
    }
    return std::nullopt;
}

bool SelectionManager::HasRotatableSelection() const {
    if (!state_.selectedLayoutItem || state_.selectedLayoutItems.size() != 1)
        return false;
    const size_t idx = static_cast<size_t>(*state_.selectedLayoutItem);
    if (idx >= state_.layoutItems.size()) return false;
    return !state_.layoutItems[idx].locked;
}

// Resize handles are in screen space (so they remain grabbable at any zoom).
// Handle centres follow the item's rotated corners.
RECT SelectionManager::HandleRect(const RECT& roomBounds, ResizeHandle h) const {
    const int corner = ResizeHandleCornerIndex(h);
    if (corner < 0) return {};
    int rot = 0;
    if (state_.selectedLayoutItem &&
        *state_.selectedLayoutItem < static_cast<int>(state_.layoutItems.size()))
        rot = state_.layoutItems[static_cast<size_t>(*state_.selectedLayoutItem)].rotation;
    const auto g = ComputeLayoutHandleGeometry(roomBounds, rot, tx_);
    const POINT c = g.corners[corner];
    // Generous grab zone so the corner is easy to hit (otherwise the cursor
    // falls through to the item body and shows the move cursor instead).
    const int sz = std::max(18, Scale(18));
    return {c.x-sz/2, c.y-sz/2, c.x+sz/2, c.y+sz/2};
}

ResizeHandle SelectionManager::HitTestHandle(POINT screenPt) const {
    if (!HasRotatableSelection()) return ResizeHandle::None;
    const size_t idx = static_cast<size_t>(*state_.selectedLayoutItem);
    const RECT& roomBounds = state_.layoutItems[idx].bounds;
    for (auto h : {ResizeHandle::TopLeft, ResizeHandle::TopRight,
                   ResizeHandle::BottomLeft, ResizeHandle::BottomRight})
        if (PtInRectEx(HandleRect(roomBounds, h), screenPt)) return h;
    return ResizeHandle::None;
}

bool SelectionManager::HitTestRotateHandle(POINT screenPt) const {
    if (!HasRotatableSelection()) return false;
    const size_t idx = static_cast<size_t>(*state_.selectedLayoutItem);
    const auto g = ComputeLayoutHandleGeometry(state_.layoutItems[idx].bounds,
                                               state_.layoutItems[idx].rotation, tx_);
    const int sz = std::max(20, Scale(20));
    const RECT box{ g.rotate.x - sz/2, g.rotate.y - sz/2,
                    g.rotate.x + sz/2, g.rotate.y + sz/2 };
    return PtInRectEx(box, screenPt);
}
