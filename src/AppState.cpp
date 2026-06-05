#include "AppState.h"
#include "Utils.h"
#include <algorithm>

void AppState::Init() {
    roster.clear();
    studentInfo.clear();
    restrictions.clear();
    affinities.clear();
    mustTogether.clear();
    groupAffinities.clear();
    layoutItems.clear();
    lastAffinitySatisfaction = 0.0;
    autoAssignSearchLimit = kDefaultAutoAssignSearchLimit;
    selectedLayoutItem   = std::nullopt;
    selectedLayoutItems.clear();
    selectedLayoutSeat   = std::nullopt;
    roomW = 0; roomH = 0;
    frontEdge = RoomEdge::Top;
    chartMode = ChartMode::Layout;   // unified chart opens in the Arrange tool
    dirty  = false;
    status = L"Ready";
    showLastNames = true;
    ClearUndoHistory();
}

// ---------------------------------------------------------------------------
// Student records (keyed by canonical name)
// ---------------------------------------------------------------------------

const StudentInfo* AppState::FindStudent(const std::wstring& name) const {
    const auto it = studentInfo.find(CanonicalName(name));
    return it == studentInfo.end() ? nullptr : &it->second;
}

StudentInfo& AppState::StudentRecord(const std::wstring& name) {
    return studentInfo[CanonicalName(name)];
}

uint32_t AppState::StudentColor(const std::wstring& name) const {
    const StudentInfo* s = FindStudent(name);
    return s ? s->color : 0u;
}

// ---------------------------------------------------------------------------
// Undo / redo
// ---------------------------------------------------------------------------

// Snapshot-based undo fully removed (command-based migration complete).
// All undo now uses UndoManager + lightweight commands in AppState.

// ---------------------------------------------------------------------------
// New lightweight command-based seat operations
// ---------------------------------------------------------------------------

void AppState::AssignStudentToSeat(LayoutSeatRef seat, const std::wstring& studentName) {
    const int itemIdx = seat.first;
    const int slotIdx = seat.second;

    if (itemIdx < 0 || itemIdx >= static_cast<int>(layoutItems.size())) return;
    auto& item = layoutItems[static_cast<size_t>(itemIdx)];

    if (slotIdx < 0 || slotIdx >= static_cast<int>(item.occupants.size())) return;

    auto cmd = std::make_unique<AssignStudentCommand>(item.occupants, slotIdx, studentName);
    undoManager.ExecuteCommand(std::move(cmd));

    dirty = true;
    ++revision_;
}

void AppState::AssignStudentToSeatExclusive(LayoutSeatRef seat, const std::wstring& studentName) {
    const auto targetName = TrimCopy(studentName);
    const auto targetCanon = CanonicalName(targetName);
    if (targetCanon.empty()) return;

    const int itemIdx = seat.first;
    const int slotIdx = seat.second;
    if (itemIdx < 0 || itemIdx >= static_cast<int>(layoutItems.size())) return;
    auto& item = layoutItems[static_cast<size_t>(itemIdx)];
    if (slotIdx < 0 || slotIdx >= static_cast<int>(item.occupants.size())) return;

    UndoGroup group(undoManager, L"Assign student");
    for (auto& li : layoutItems) {
        for (int s = 0; s < static_cast<int>(li.occupants.size()); ++s) {
            if (&li == &item && s == slotIdx) continue;
            if (CanonicalName(li.occupants[static_cast<size_t>(s)]) == targetCanon) {
                undoManager.ExecuteCommand(std::make_unique<ClearStudentCommand>(li.occupants, s));
            }
        }
    }
    undoManager.ExecuteCommand(std::make_unique<AssignStudentCommand>(item.occupants, slotIdx, targetName));

    dirty = true;
    ++revision_;
}

void AppState::ClearStudentFromSeat(LayoutSeatRef seat) {
    const int itemIdx = seat.first;
    const int slotIdx = seat.second;

    if (itemIdx < 0 || itemIdx >= static_cast<int>(layoutItems.size())) return;
    auto& item = layoutItems[static_cast<size_t>(itemIdx)];

    if (slotIdx < 0 || slotIdx >= static_cast<int>(item.occupants.size())) return;

    auto cmd = std::make_unique<ClearStudentCommand>(item.occupants, slotIdx);
    undoManager.ExecuteCommand(std::move(cmd));

    dirty = true;
    ++revision_;
}

// New lightweight command helpers for layout items
void AppState::AddLayoutItem(const LayoutItem& item) {
    auto cmd = std::make_unique<AddLayoutItemCommand>(layoutItems, item);
    undoManager.ExecuteCommand(std::move(cmd));
    dirty = true;
    ++revision_;
}

void AppState::DeleteLayoutItem(int index) {
    if (index < 0 || index >= static_cast<int>(layoutItems.size())) return;
    auto cmd = std::make_unique<DeleteLayoutItemCommand>(layoutItems, index);
    undoManager.ExecuteCommand(std::move(cmd));
    dirty = true;
    ++revision_;
}
