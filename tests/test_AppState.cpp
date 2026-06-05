#include <doctest/doctest.h>
#include "AppState.h"
#include "Utils.h"

// Suppress the GWLP_USERDATA / Scale() references that need a real HWND.
// Tests only exercise pure data logic.
UINT g_dpi = 96;

namespace {
// Small helper: a minimal seating item used to drive the command-based undo
// system (AddLayoutItem pushes a real UndoCommand, unlike Transaction::Commit).
LayoutItem MakeDesk() {
    LayoutItem it;
    it.type   = LayoutItemType::RectangleDesk;
    it.bounds = RECT{ 0, 0, 100, 80 };
    return it;
}
} // namespace

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

TEST_CASE("AppState::Init resets to defaults") {
    AppState s;
    s.Init();
    CHECK(s.roster.empty());
    CHECK(s.restrictions.empty());
    CHECK(s.layoutItems.empty());
    CHECK_FALSE(s.selectedLayoutItem.has_value());
    CHECK(s.selectedLayoutItems.empty());
    CHECK_FALSE(s.selectedLayoutSeat.has_value());
    CHECK(s.chartMode == ChartMode::Layout);   // unified chart opens in Arrange
    CHECK(s.frontEdge == RoomEdge::Top);       // front defaults to the top edge
    CHECK_FALSE(s.dirty);
    CHECK_FALSE(s.CanUndo());
    CHECK_FALSE(s.CanRedo());
}

// ---------------------------------------------------------------------------
// Undo / redo
//
// The live undo system is command-based (UndoManager + UndoCommand). A bare
// Transaction::Commit() intentionally no longer creates an undo entry (it only
// marks dirty + advances the revision counter — see AppState.h), so these
// exercise undo through the command API (AddLayoutItem / Undo / Redo).
// ---------------------------------------------------------------------------

TEST_CASE("Transaction::Commit marks dirty but creates no undo entry") {
    AppState s; s.Init();
    CHECK_FALSE(s.CanUndo());
    {
        AppState::Transaction tx(s);
        tx->roster.push_back(L"Alice");
        tx.Commit();
    }
    CHECK(s.dirty);
    CHECK_FALSE(s.CanUndo());   // undo entries come from commands, not Transactions
    REQUIRE(s.roster.size() == 1u);
    CHECK(s.roster[0] == L"Alice");
}

TEST_CASE("Transaction without Commit leaves no undo record") {
    AppState s; s.Init();
    {
        AppState::Transaction tx(s);
        tx->roster.push_back(L"Alice");
        // No Commit.
    }
    // The data WAS mutated (Transaction doesn't roll back data, only the record).
    CHECK_FALSE(s.CanUndo());
}

TEST_CASE("A command is undoable") {
    AppState s; s.Init();
    CHECK_FALSE(s.CanUndo());
    s.AddLayoutItem(MakeDesk());
    CHECK(s.CanUndo());
    CHECK_FALSE(s.CanRedo());
    REQUIRE(s.layoutItems.size() == 1u);
}

TEST_CASE("Undo reverses the most recent command") {
    AppState s; s.Init();
    s.AddLayoutItem(MakeDesk());
    REQUIRE(s.layoutItems.size() == 1u);
    s.Undo();
    CHECK(s.layoutItems.empty());
    CHECK_FALSE(s.CanUndo());
    CHECK(s.CanRedo());
}

TEST_CASE("Redo reapplies after undo") {
    AppState s; s.Init();
    s.AddLayoutItem(MakeDesk());
    s.Undo();
    CHECK(s.layoutItems.empty());
    s.Redo();
    REQUIRE(s.layoutItems.size() == 1u);
    CHECK_FALSE(s.CanRedo());
}

TEST_CASE("A new command clears the redo stack") {
    AppState s; s.Init();
    s.AddLayoutItem(MakeDesk());
    s.Undo();
    CHECK(s.CanRedo());
    s.AddLayoutItem(MakeDesk());
    CHECK_FALSE(s.CanRedo()); // executing a new command cleared redo
}

TEST_CASE("Undo stack is capped at kMaxUndoDepth") {
    AppState s; s.Init();
    for (size_t i = 0; i < kMaxUndoDepth + 5; ++i)
        s.AddLayoutItem(MakeDesk());
    size_t count = 0;
    while (s.CanUndo()) { s.Undo(); ++count; }
    CHECK(count == kMaxUndoDepth);
}

// ---------------------------------------------------------------------------
// Revision counter
// ---------------------------------------------------------------------------

TEST_CASE("Revision increments on Transaction::Commit") {
    AppState s; s.Init();
    const uint64_t r0 = s.Revision();
    {
        AppState::Transaction tx(s);
        tx->roster.push_back(L"Alice");
        tx.Commit();
    }
    CHECK(s.Revision() == r0 + 1);
}

TEST_CASE("Revision not incremented without Commit") {
    AppState s; s.Init();
    const uint64_t r0 = s.Revision();
    {
        AppState::Transaction tx(s);
        tx->roster.push_back(L"Alice");
        // deliberately not committing
    }
    CHECK(s.Revision() == r0);
}

TEST_CASE("Revision increments on Undo") {
    AppState s; s.Init();
    s.AddLayoutItem(MakeDesk());
    const uint64_t r1 = s.Revision();
    s.Undo();
    CHECK(s.Revision() == r1 + 1);
}

TEST_CASE("Revision increments on Redo") {
    AppState s; s.Init();
    s.AddLayoutItem(MakeDesk());
    s.Undo();
    const uint64_t r2 = s.Revision();
    s.Redo();
    CHECK(s.Revision() == r2 + 1);
}

TEST_CASE("Multiple commits each increment revision") {
    AppState s; s.Init();
    const uint64_t r0 = s.Revision();
    for (int i = 0; i < 3; ++i) {
        AppState::Transaction tx(s);
        tx->roster.push_back(std::to_wstring(i));
        tx.Commit();
    }
    CHECK(s.Revision() == r0 + 3);
}

// ---------------------------------------------------------------------------
// Front edge (room orientation) helpers
// ---------------------------------------------------------------------------

TEST_CASE("RoomEdge name round-trips") {
    for (RoomEdge e : { RoomEdge::Top, RoomEdge::Bottom, RoomEdge::Left, RoomEdge::Right })
        CHECK(RoomEdgeFromName(RoomEdgeName(e)) == e);
    CHECK(RoomEdgeFromName(L"nonsense") == RoomEdge::Top); // safe default
}

TEST_CASE("SeatFrontDistance ranks seats by distance from the front edge") {
    const int W = 1000, H = 800;
    const POINT nearTop{ 500, 50 };
    const POINT nearBottom{ 500, 750 };

    // Front = Top: a seat near the top is more front (smaller distance).
    CHECK(SeatFrontDistance(nearTop, RoomEdge::Top, W, H) <
          SeatFrontDistance(nearBottom, RoomEdge::Top, W, H));
    CHECK(SeatFrontDistance(nearTop, RoomEdge::Top, W, H) == 50);

    // Front = Bottom flips the ranking.
    CHECK(SeatFrontDistance(nearBottom, RoomEdge::Bottom, W, H) <
          SeatFrontDistance(nearTop, RoomEdge::Bottom, W, H));
    CHECK(SeatFrontDistance(nearBottom, RoomEdge::Bottom, W, H) == H - 750);

    // Left / Right rank along the x axis.
    const POINT nearLeft{ 30, 400 };
    CHECK(SeatFrontDistance(nearLeft, RoomEdge::Left,  W, H) == 30);
    CHECK(SeatFrontDistance(nearLeft, RoomEdge::Right, W, H) == W - 30);
}

TEST_CASE("SeatFrontDistance substitutes default room size when 0") {
    const POINT p{ 100, 200 };
    CHECK(SeatFrontDistance(p, RoomEdge::Bottom, 0, 0) == kDefaultRoomH - 200);
    CHECK(SeatFrontDistance(p, RoomEdge::Right,  0, 0) == kDefaultRoomW - 100);
}

// ---------------------------------------------------------------------------
// Block authoring — merged bounds geometry
// ---------------------------------------------------------------------------

TEST_CASE("MergedBlockBounds unions block bounds and snaps to the grid") {
    std::vector<LayoutItem> items(3);
    items[0].bounds = {100, 100, 200, 200};
    items[1].bounds = {200, 100, 300, 200};
    items[2].bounds = {100, 200, 200, 300};
    const RECT m = MergedBlockBounds(items, {0, 1, 2}, 100);
    CHECK(m.left   == 100);
    CHECK(m.top    == 100);
    CHECK(m.right  == 300);   // bounding box of the three cells
    CHECK(m.bottom == 300);
}

TEST_CASE("MergedBlockBounds ignores out-of-range indices") {
    std::vector<LayoutItem> items(1);
    items[0].bounds = {0, 0, 100, 100};
    const RECT m = MergedBlockBounds(items, {0, 5, -1}, 100);
    CHECK(m.right  == 100);
    CHECK(m.bottom == 100);
}
