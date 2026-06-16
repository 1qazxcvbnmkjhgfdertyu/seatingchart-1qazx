#pragma once
#include "Types.h"
#include "Undo.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Application limits
constexpr int kMaxRosterCount      = 1000;
constexpr int kMaxRestrictionCount = 2000;
constexpr int kMaxLayoutItemCount  = 500;
constexpr size_t kDefaultAutoAssignSearchLimit = 500000;
constexpr size_t kMaxUndoDepth          = 50;

// Timers
constexpr UINT_PTR kAutoSaveTimerId = 4001;
constexpr UINT     kAutoSaveDelayMs = 750;

// Alignment modes for multi-select alignment tool
enum class AlignMode { Left, Right, Top, Bottom, CenterH, CenterV, DistributeH, DistributeV };

struct Restriction {
    std::wstring first;
    std::wstring second;
    // Keep-apart radius in room units. 0 = legacy "same furniture item"; >0 =
    // the two students may not sit within this distance (centre-to-centre).
    int radius = 0;
    // For affinities: strength 1-10 (higher = stronger preference to sit near). Default 1.
    // Ignored for hard restrictions.
    int weight = 1;
};

// Per-student record (attributes/tags, free-text notes, colour/group). Stored in
// AppState::studentInfo keyed by CanonicalName(name) so it survives roster edits
// and is referenced by occupant name on seats. Kept lightweight.
struct StudentInfo {
    std::vector<std::wstring> tags;    // e.g. "Front row", "IEP", "Behavior"
    std::wstring              notes;   // free text (substitute-teacher friendly)
    uint32_t                  color = 0; // 0 = none; else 0x00RRGGBB group colour
    std::vector<std::wstring> forbiddenDesks; // placeholder for per-student off-limits (labels/types); future solver/UI
};

// Core types (LayoutItem, LayoutItemType, LayoutSeatRef, etc.) are now in Types.h
// This reduces include bloat and helps with the lightweight goal.

struct ThemeColors {
    COLORREF window{}, panel{}, text{}, mutedText{},
             seatEmpty{}, seatOccupied{}, seatSelected{},
             border{}, accent{}, furniture{}, furnitureSelected{}, paper{};
};

struct AppLayout {
    RECT client{}, chart{}, panel{}, info{};
};

// UndoSnapshot removed - migration to command-based undo (UndoManager + concrete commands) is complete.
// All mutations now go through lightweight commands for fine-grained undo.

// ---------------------------------------------------------------------------
// Application model — owns all persistent data.
// ---------------------------------------------------------------------------
class AppState {
public:
    // --- Data ---
    UINT saveDpi = 96;              // DPI at last save; used to scale layout items on load
    std::vector<std::wstring> roster;
    // Per-student attributes/notes/colour, keyed by CanonicalName(name).
    std::unordered_map<std::wstring, StudentInfo> studentInfo;
    std::vector<Restriction>  restrictions;          // hard keep-apart rules (seating)
    std::vector<Restriction>  affinities;            // soft "sit near" preferences
    std::vector<Restriction>  mustTogether;          // hard "must sit together" (same item or close seats)
    std::vector<std::vector<std::wstring>> groupAffinities; // groups of students that should sit clustered (e.g. same table/pod)
    // Group generator rules.  By default the Groups tab reuses the seating
    // keep-apart / keep-together rules above; unchecking "Same as seating" in a
    // section switches it to its own independent list below.
    std::vector<Restriction>  groupRestrictions;     // group-only keep-apart (used when !groupUseSeatingApart)
    std::vector<Restriction>  groupMustTogether;     // group-only keep-together (used when !groupUseSeatingTogether)
    bool groupUseSeatingApart    = true;             // groups share the seating keep-apart rules
    bool groupUseSeatingTogether = true;             // groups share the seating keep-together rules
    std::vector<LayoutItem>   layoutItems;
    size_t                    autoAssignSearchLimit = kDefaultAutoAssignSearchLimit; // configurable steps for solver
    double                    lastAffinitySatisfaction = 0.0; // for post-solve badge / summary
    std::optional<int>        selectedLayoutItem;   // inspector focus (primary selected)
    std::vector<int>          selectedLayoutItems;  // full multi-select set (sorted)
    std::optional<LayoutSeatRef> selectedLayoutSeat; // focused furniture seat (item, slot)
    int                       roomW = 0, roomH = 0; // 0 = auto (fill chart bounds)
    RoomEdge     frontEdge = RoomEdge::Top;          // which room edge is "the front"
    ChartMode    chartMode = ChartMode::Seats;
    bool         dirty     = false;
    std::wstring className = L"Class 1";
    std::wstring status    = L"Ready";
    bool         showLastNames = true;               // when true, display full names everywhere

    // New lightweight undo system (Phase 1 foundation work)
    UndoManager undoManager;

    // --- Lifecycle ---
    void Init();

    // --- Student records (attributes / notes / colour) ---
    // Lookup by display name (canonicalised internally). Returns nullptr if none.
    const StudentInfo* FindStudent(const std::wstring& name) const;
    // Get (creating if needed) the mutable record for a name.
    StudentInfo&       StudentRecord(const std::wstring& name);
    // Occupant group colour (0 = none).
    uint32_t           StudentColor(const std::wstring& name) const;

    // --- State revision counter (increments on every committed mutation) ---
    uint64_t Revision() const { return revision_; }

    // --- Undo / redo (command based, fully migrated) ---
    bool CanUndo() const { return undoManager.CanUndo(); }
    bool CanRedo() const { return undoManager.CanRedo(); }
    void Undo() { if (undoManager.CanUndo()) { undoManager.Undo(); dirty = true; ++revision_; } }
    void Redo() { if (undoManager.CanRedo()) { undoManager.Redo(); dirty = true; ++revision_; } }
    void ClearUndoHistory() { undoManager.Clear(); }

    UndoManager& GetUndoManager() { return undoManager; }
    const UndoManager& GetUndoManager() const { return undoManager; }

    // --- New command-based seat operations (Phase 1 foundation) ---
    // These use the lightweight UndoCommand system instead of full state snapshots.
    void AssignStudentToSeat(LayoutSeatRef seat, const std::wstring& studentName);
    void AssignStudentToSeatExclusive(LayoutSeatRef seat, const std::wstring& studentName);
    void ClearStudentFromSeat(LayoutSeatRef seat);

    // New command-based layout item operations
    void AddLayoutItem(const LayoutItem& item);
    void DeleteLayoutItem(int index);

    // -----------------------------------------------------------------
    // Transaction — RAII undo guard.
    //
    //   AppState::Transaction tx(state_);
    //   tx->layoutItems[0].occupants[0] = L"Alice";
    //   tx.Commit();          // ← pushes snapshot, marks dirty
    //   // If Commit() is never called, the snapshot is discarded.
    // -----------------------------------------------------------------
    class Transaction {
    public:
        explicit Transaction(AppState& s)
            : state_(s) {}

        ~Transaction() = default;

        // During the transition to the new command-based undo system,
        // Commit() no longer creates undo entries (that is now done via UndoManager + Commands).
        // It still marks the state as dirty and advances the revision counter.
        void Commit() {
            if (committed_) return;
            state_.dirty = true;
            ++state_.revision_;
            committed_ = true;
        }

        AppState*       operator->()       { return &state_; }
        const AppState* operator->() const { return &state_; }
        AppState&       operator*()        { return state_; }

        Transaction(const Transaction&)            = delete;
        Transaction& operator=(const Transaction&) = delete;

    private:
        AppState&    state_;
        bool         committed_ = false;
    };

private:
    // Command-based undo is complete; no more snapshot-based undo.
    uint64_t revision_ = 0;
};
