#include "Undo.h"
#include <algorithm>
#include <cassert>

void UndoManager::ExecuteCommand(std::unique_ptr<UndoCommand> command) {
    if (!command) return;

    command->Execute();

    undoStack_.push_back(std::move(command));
    redoStack_.clear();

    // Enforce undo depth limit
    if (undoStack_.size() > maxUndoDepth_) {
        undoStack_.erase(undoStack_.begin());
    }
}

void UndoManager::Undo() {
    if (undoStack_.empty()) return;

    auto& cmd = undoStack_.back();
    cmd->Undo();

    redoStack_.push_back(std::move(cmd));
    undoStack_.pop_back();
}

void UndoManager::Redo() {
    if (redoStack_.empty()) return;

    auto& cmd = redoStack_.back();
    cmd->Execute();

    undoStack_.push_back(std::move(cmd));
    redoStack_.pop_back();
}

bool UndoManager::CanUndo() const {
    return !undoStack_.empty();
}

bool UndoManager::CanRedo() const {
    return !redoStack_.empty();
}

void UndoManager::Clear() {
    undoStack_.clear();
    redoStack_.clear();
}

void UndoManager::SetMaxUndoDepth(size_t maxDepth) {
    maxUndoDepth_ = maxDepth;

    // Trim existing stack if necessary
    while (undoStack_.size() > maxUndoDepth_) {
        undoStack_.erase(undoStack_.begin());
    }
}

std::wstring UndoManager::GetUndoDescription() const {
    if (undoStack_.empty()) return L"";
    return undoStack_.back()->GetDescription();
}

std::wstring UndoManager::GetRedoDescription() const {
    if (redoStack_.empty()) return L"";
    return redoStack_.back()->GetDescription();
}

// ---------------------------------------------------------------------------
// UndoGroup implementation
// ---------------------------------------------------------------------------

UndoGroup::UndoGroup(UndoManager& manager, std::wstring description)
    : manager_(manager), description_(std::move(description)), startIndex_(manager_.undoStack_.size()) {
}

UndoGroup::~UndoGroup() {
    const size_t endIndex = manager_.undoStack_.size();

    if (endIndex <= startIndex_) {
        return; // No commands were executed inside this group
    }

    const size_t count = endIndex - startIndex_;

    if (count == 1) {
        // Only one command — no need to wrap it
        if (!description_.empty()) {
            // Optionally we could set description on the single command, but
            // most commands have their own meaningful description.
        }
        return;
    }

    // Multiple commands → bundle them into one CompositeCommand for cleaner undo history
    std::vector<std::unique_ptr<UndoCommand>> grouped;
    grouped.reserve(count);

    for (size_t i = startIndex_; i < endIndex; ++i) {
        // We need to take ownership of the commands that were just pushed
        grouped.push_back(std::move(manager_.undoStack_[i]));
    }

    // Remove the individual commands from the stack
    manager_.undoStack_.resize(startIndex_);

    auto composite = std::make_unique<CompositeCommand>(std::move(grouped), description_);
    manager_.undoStack_.push_back(std::move(composite));
}

// ---------------------------------------------------------------------------
// AssignStudentCommand
// ---------------------------------------------------------------------------

AssignStudentCommand::AssignStudentCommand(std::vector<std::wstring>& occupants,
                                           int slotIndex,
                                           std::wstring newStudentName)
    : occupants_(occupants),
      slotIndex_(slotIndex),
      newName_(std::move(newStudentName)) {
    if (slotIndex_ >= 0 && slotIndex_ < static_cast<int>(occupants_.size())) {
        oldName_ = occupants_[static_cast<size_t>(slotIndex_)];
    }
}

void AssignStudentCommand::Execute() {
    if (slotIndex_ >= 0 && slotIndex_ < static_cast<int>(occupants_.size())) {
        occupants_[static_cast<size_t>(slotIndex_)] = newName_;
    }
}

void AssignStudentCommand::Undo() {
    if (slotIndex_ >= 0 && slotIndex_ < static_cast<int>(occupants_.size())) {
        occupants_[static_cast<size_t>(slotIndex_)] = oldName_;
    }
}

std::wstring AssignStudentCommand::GetDescription() const {
    return L"Assign student";
}

// ---------------------------------------------------------------------------
// ClearStudentCommand
// ---------------------------------------------------------------------------

ClearStudentCommand::ClearStudentCommand(std::vector<std::wstring>& occupants, int slotIndex)
    : occupants_(occupants), slotIndex_(slotIndex) {
    if (slotIndex_ >= 0 && slotIndex_ < static_cast<int>(occupants_.size())) {
        previousName_ = occupants_[static_cast<size_t>(slotIndex_)];
    }
}

void ClearStudentCommand::Execute() {
    if (slotIndex_ >= 0 && slotIndex_ < static_cast<int>(occupants_.size())) {
        occupants_[static_cast<size_t>(slotIndex_)].clear();
    }
}

void ClearStudentCommand::Undo() {
    if (slotIndex_ >= 0 && slotIndex_ < static_cast<int>(occupants_.size())) {
        occupants_[static_cast<size_t>(slotIndex_)] = previousName_;
    }
}

std::wstring ClearStudentCommand::GetDescription() const {
    return L"Clear seat";
}

// ---------------------------------------------------------------------------
// MoveLayoutItemCommand
// ---------------------------------------------------------------------------

MoveLayoutItemCommand::MoveLayoutItemCommand(RECT& targetBounds, RECT oldB, RECT newB)
    : targetBounds_(targetBounds), oldBounds_(oldB), newBounds_(newB) {
}

void MoveLayoutItemCommand::Execute() {
    targetBounds_ = newBounds_;
}

void MoveLayoutItemCommand::Undo() {
    targetBounds_ = oldBounds_;
}

std::wstring MoveLayoutItemCommand::GetDescription() const {
    return L"Move item";
}

// ---------------------------------------------------------------------------
// ResizeLayoutItemCommand
// ---------------------------------------------------------------------------

ResizeLayoutItemCommand::ResizeLayoutItemCommand(RECT& targetBounds, RECT oldB, RECT newB)
    : targetBounds_(targetBounds), oldBounds_(oldB), newBounds_(newB) {
}

void ResizeLayoutItemCommand::Execute() {
    targetBounds_ = newBounds_;
}

void ResizeLayoutItemCommand::Undo() {
    targetBounds_ = oldBounds_;
}

std::wstring ResizeLayoutItemCommand::GetDescription() const {
    return L"Resize item";
}

// ---------------------------------------------------------------------------
// CompositeCommand
// ---------------------------------------------------------------------------

CompositeCommand::CompositeCommand(std::vector<std::unique_ptr<UndoCommand>> commands, std::wstring description)
    : commands_(std::move(commands)), description_(std::move(description)) {
}

void CompositeCommand::Execute() {
    for (auto& cmd : commands_) {
        cmd->Execute();
    }
}

void CompositeCommand::Undo() {
    // Undo in reverse order
    for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) {
        (*it)->Undo();
    }
}

std::wstring CompositeCommand::GetDescription() const {
    if (!description_.empty()) return description_;
    return L"Multiple changes";
}

// ---------------------------------------------------------------------------
// AddLayoutItemCommand
// ---------------------------------------------------------------------------

AddLayoutItemCommand::AddLayoutItemCommand(std::vector<LayoutItem>& items, LayoutItem newItem)
    : items_(items), item_(std::move(newItem)) {
}

void AddLayoutItemCommand::Execute() {
    items_.push_back(item_);
    insertedIndex_ = static_cast<int>(items_.size()) - 1;
}

void AddLayoutItemCommand::Undo() {
    if (insertedIndex_ >= 0 && insertedIndex_ < static_cast<int>(items_.size())) {
        items_.erase(items_.begin() + insertedIndex_);
    }
}

std::wstring AddLayoutItemCommand::GetDescription() const {
    return L"Add item";
}

// ---------------------------------------------------------------------------
// DeleteLayoutItemCommand
// ---------------------------------------------------------------------------

DeleteLayoutItemCommand::DeleteLayoutItemCommand(std::vector<LayoutItem>& items, int index)
    : items_(items), deletedIndex_(index) {
    if (index >= 0 && index < static_cast<int>(items_.size())) {
        deletedItem_ = items_[index];
    }
}

void DeleteLayoutItemCommand::Execute() {
    if (deletedIndex_ >= 0 && deletedIndex_ < static_cast<int>(items_.size())) {
        items_.erase(items_.begin() + deletedIndex_);
    }
}

void DeleteLayoutItemCommand::Undo() {
    if (deletedIndex_ >= 0) {
        items_.insert(items_.begin() + deletedIndex_, deletedItem_);
    }
}

std::wstring DeleteLayoutItemCommand::GetDescription() const {
    return L"Delete item";
}