#pragma once

#include <windows.h>

#include "Types.h"

#include <memory>
#include <vector>
#include <string>
#include <functional>

// ---------------------------------------------------------------------------
// UndoCommand
//
// Base class for all undoable operations. Each command knows how to apply
// itself (Execute) and how to reverse itself (Undo).
// Commands should be lightweight — they store only the minimal data needed
// to perform the undo/redo.
// ---------------------------------------------------------------------------
class UndoCommand {
public:
    virtual ~UndoCommand() = default;

    // Apply the change (or re-apply it during redo)
    virtual void Execute() = 0;

    // Reverse the change
    virtual void Undo() = 0;

    // Human-readable description for debugging / future UI
    virtual std::wstring GetDescription() const = 0;
};

// ---------------------------------------------------------------------------
// UndoManager
//
// Owns the undo and redo stacks. This is the central piece for the new
// lightweight undo system.
// ---------------------------------------------------------------------------
class UndoManager {
public:
    UndoManager() = default;
    ~UndoManager() = default;

    // Disable copying (we own unique commands), but allow moving so that
    // owners (e.g. AppState) stay movable — the command stacks transfer cleanly.
    UndoManager(const UndoManager&) = delete;
    UndoManager& operator=(const UndoManager&) = delete;
    UndoManager(UndoManager&&) = default;
    UndoManager& operator=(UndoManager&&) = default;

    // Execute a command and push it onto the undo stack.
    // Clears the redo stack (standard behavior).
    void ExecuteCommand(std::unique_ptr<UndoCommand> command);

    // Undo the most recent command (if any)
    void Undo();

    // Redo the most recently undone command (if any)
    void Redo();

    bool CanUndo() const;
    bool CanRedo() const;

    void Clear();

    // Optional: Limit the number of undo steps (similar to old kMaxUndoDepth)
    void SetMaxUndoDepth(size_t maxDepth);

    // For debugging / status bar
    std::wstring GetUndoDescription() const;
    std::wstring GetRedoDescription() const;

private:
    std::vector<std::unique_ptr<UndoCommand>> undoStack_;
    std::vector<std::unique_ptr<UndoCommand>> redoStack_;
    size_t maxUndoDepth_ = 50;

    friend class UndoGroup;   // Allows UndoGroup to inspect stack size for grouping
};

// ---------------------------------------------------------------------------
// Concrete Commands (Phase 1)
// These are the first real lightweight undoable operations.
// We start with the most common actions: assigning and clearing students.
// ---------------------------------------------------------------------------

// Assigns (or re-assigns) a student to a specific seat slot on a furniture item.
// Stores only the old name and new name — very cheap compared to full state snapshot.
class AssignStudentCommand : public UndoCommand {
public:
    AssignStudentCommand(std::vector<std::wstring>& occupants, int slotIndex, std::wstring newStudentName);

    void Execute() override;
    void Undo() override;
    std::wstring GetDescription() const override;

private:
    std::vector<std::wstring>& occupants_;
    int slotIndex_;
    std::wstring oldName_;
    std::wstring newName_;
};

// Clears a student from a seat slot.
class ClearStudentCommand : public UndoCommand {
public:
    ClearStudentCommand(std::vector<std::wstring>& occupants, int slotIndex);

    void Execute() override;
    void Undo() override;
    std::wstring GetDescription() const override;

private:
    std::vector<std::wstring>& occupants_;
    int slotIndex_;
    std::wstring previousName_;
};

// ---------------------------------------------------------------------------
// Layout Item Commands (high frequency operations)
// ---------------------------------------------------------------------------

class MoveLayoutItemCommand : public UndoCommand {
public:
    // Stores old and new explicitly (better for interactive drag use cases)
    MoveLayoutItemCommand(RECT& targetBounds, RECT oldB, RECT newB);

    void Execute() override;
    void Undo() override;
    std::wstring GetDescription() const override;

private:
    RECT& targetBounds_;
    RECT oldBounds_;
    RECT newBounds_;
};

class ResizeLayoutItemCommand : public UndoCommand {
public:
    ResizeLayoutItemCommand(RECT& targetBounds, RECT oldB, RECT newB);

    void Execute() override;
    void Undo() override;
    std::wstring GetDescription() const override;

private:
    RECT& targetBounds_;
    RECT oldBounds_;
    RECT newBounds_;
};

// ---------------------------------------------------------------------------
// CompositeCommand - allows bundling multiple commands into one undo step
// ---------------------------------------------------------------------------
class CompositeCommand : public UndoCommand {
public:
    explicit CompositeCommand(std::vector<std::unique_ptr<UndoCommand>> commands, std::wstring description = L"");

    void Execute() override;
    void Undo() override;
    std::wstring GetDescription() const override;

private:
    std::vector<std::unique_ptr<UndoCommand>> commands_;
    std::wstring description_;
};

// ---------------------------------------------------------------------------
// Add / Delete Layout Item Commands
// ---------------------------------------------------------------------------

class AddLayoutItemCommand : public UndoCommand {
public:
    AddLayoutItemCommand(std::vector<LayoutItem>& items, LayoutItem newItem);

    void Execute() override;
    void Undo() override;
    std::wstring GetDescription() const override;

private:
    std::vector<LayoutItem>& items_;
    LayoutItem item_;
    int insertedIndex_ = -1;
};

class DeleteLayoutItemCommand : public UndoCommand {
public:
    DeleteLayoutItemCommand(std::vector<LayoutItem>& items, int index);

    void Execute() override;
    void Undo() override;
    std::wstring GetDescription() const override;

private:
    std::vector<LayoutItem>& items_;
    LayoutItem deletedItem_;
    int deletedIndex_ = -1;
};

// ---------------------------------------------------------------------------
// UndoGroup (RAII helper for grouping multiple commands into one undo step)
//
// Usage:
//
//   {
//       UndoGroup group(undoManager, L"Move multiple items");
//       // Create and execute several commands here
//       undoManager.ExecuteCommand(std::make_unique<MoveCommand>(...));
//       undoManager.ExecuteCommand(std::make_unique<MoveCommand>(...));
//   }  // When group goes out of scope, the grouped commands become one undo entry
//
// This provides a reasonably convenient API while still using commands internally.
// ---------------------------------------------------------------------------
class UndoGroup {
public:
    explicit UndoGroup(UndoManager& manager, std::wstring description = L"");
    ~UndoGroup();

    // Non-copyable, non-movable for safety
    UndoGroup(const UndoGroup&) = delete;
    UndoGroup& operator=(const UndoGroup&) = delete;

private:
    UndoManager& manager_;
    std::wstring description_;
    size_t startIndex_;   // Index in undo stack when group started
};