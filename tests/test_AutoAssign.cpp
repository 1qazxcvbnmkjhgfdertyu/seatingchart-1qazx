#include <doctest/doctest.h>
#include "AppState.h"
#include "AutoAssign.h"
#include "Utils.h"
#include <atomic>
#include <unordered_set>

extern UINT g_dpi;

// ---------------------------------------------------------------------------
// Utility helpers
// ---------------------------------------------------------------------------

TEST_CASE("CanonicalName folds case and trims whitespace") {
    CHECK(CanonicalName(L"  Alice  ") == L"alice");
    CHECK(CanonicalName(L"BOB")       == L"bob");
    CHECK(CanonicalName(L"  ")        == L"");
    CHECK(CanonicalName(L"John  Doe") == L"john doe"); // internal whitespace collapsed
}

TEST_CASE("SplitRosterInput handles various delimiters") {
    const auto r = SplitRosterInput(L"Alice\nBob\rCharlie,Dave;Eve\tFrank");
    REQUIRE(r.size() == 6u);
    CHECK(r[0] == L"Alice");
    CHECK(r[5] == L"Frank");
}

TEST_CASE("SplitRosterInput trims each name") {
    const auto r = SplitRosterInput(L"  Alice  \n  Bob  ");
    REQUIRE(r.size() == 2u);
    CHECK(r[0] == L"Alice");
    CHECK(r[1] == L"Bob");
}

TEST_CASE("SplitRestrictionInput parses pipe separator") {
    const auto r = SplitRestrictionInput(L"Alice | Bob\nCharlie | Dave");
    REQUIRE(r.size() == 2u);
    // Pairs are sorted canonically so first < second.
    CHECK(CanonicalName(r[0].first)  == L"alice");
    CHECK(CanonicalName(r[0].second) == L"bob");
}

TEST_CASE("SplitRestrictionInput deduplicates") {
    const auto r = SplitRestrictionInput(L"Alice | Bob\nBob | Alice\nalice|bob");
    CHECK(r.size() == 1u);
}

TEST_CASE("SplitRestrictionInput parses an optional @radius") {
    const auto r = SplitRestrictionInput(L"Alice | Bob @150\nCharlie | Dave");
    REQUIRE(r.size() == 2u);
    for (const auto& rule : r) {
        const bool isAliceBob = CanonicalName(rule.first)  == L"alice" ||
                                CanonicalName(rule.second) == L"alice";
        CHECK(rule.radius == (isAliceBob ? 150 : 0));
    }
}

TEST_CASE("SplitAffinityInput parses sit-near pairs and ignores apart lines") {
    const auto a = SplitAffinityInput(L"Alice + Bob\nCarol & Dave\nEve | Frank");
    REQUIRE(a.size() == 2u);   // the "|" line is keep-apart, not an affinity
    CHECK(CanonicalName(a[0].first)  == L"alice");
    CHECK(CanonicalName(a[0].second) == L"bob");
}

TEST_CASE("NormalizeRestriction sorts pair alphabetically") {
    const auto r = NormalizeRestriction({L"Zoe", L"Alice"});
    CHECK(CanonicalName(r.first) == L"alice");
    CHECK(CanonicalName(r.second) == L"zoe");
}

TEST_CASE("FindDuplicateCanonicalName detects case-insensitive dupes") {
    std::vector<std::wstring> roster = {L"Alice", L"ALICE"};
    std::wstring dup;
    CHECK(FindDuplicateCanonicalName(roster, &dup));
    CHECK(CanonicalName(dup) == L"alice");
}

TEST_CASE("FindDuplicateCanonicalName passes clean roster") {
    std::vector<std::wstring> roster = {L"Alice", L"Bob", L"Charlie"};
    CHECK_FALSE(FindDuplicateCanonicalName(roster, nullptr));
}

// ---------------------------------------------------------------------------
// AppState + assignment integration
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// BeginAutoAssign precondition checks
// ---------------------------------------------------------------------------

TEST_CASE("BeginAutoAssign returns false for empty roster") {
    AppState s; s.Init();
    std::atomic<bool> cancel{false};
    HANDLE th = nullptr;
    CHECK_FALSE(BeginAutoAssign(nullptr, s, cancel, th));
    CHECK(th == nullptr);
}

TEST_CASE("BeginAutoAssign returns false when there are no furniture seats") {
    AppState s; s.Init();              // no layout items → no seats
    for (int i = 0; i < 5; ++i)
        s.roster.push_back(L"Student" + std::to_wstring(i));
    std::atomic<bool> cancel{false};
    HANDLE th = nullptr;
    CHECK_FALSE(BeginAutoAssign(nullptr, s, cancel, th));
    CHECK(th == nullptr);
}

TEST_CASE("EnumerateLayoutSeats / TotalLayoutSeats count furniture seats") {
    AppState s; s.Init();
    LayoutItem desk;  desk.type  = LayoutItemType::RectangleDesk;
    desk.capacity  = LayoutItemDefaultCapacity(LayoutItemType::RectangleDesk);
    EnsureSeatSlots(desk);
    LayoutItem table; table.type = LayoutItemType::Table4;
    table.capacity = LayoutItemDefaultCapacity(LayoutItemType::Table4);
    EnsureSeatSlots(table);
    s.layoutItems = { desk, table };
    CHECK(TotalLayoutSeats(s.layoutItems) == 5);   // 1 desk + 4 table
    const auto refs = EnumerateLayoutSeats(s.layoutItems);
    REQUIRE(refs.size() == 5u);
    CHECK(refs[0] == LayoutSeatRef{0, 0});
    CHECK(refs[1] == LayoutSeatRef{1, 0});
    CHECK(refs[4] == LayoutSeatRef{1, 3});
}

TEST_CASE("BeginAutoAssign returns false when roster exceeds furniture seats") {
    AppState s; s.Init();
    LayoutItem desk; desk.type = LayoutItemType::RectangleDesk; desk.capacity = 1;
    EnsureSeatSlots(desk);
    s.layoutItems = { desk };          // 1 seat
    s.roster = { L"Alice", L"Bob" };   // 2 students
    std::atomic<bool> cancel{false};
    HANDLE th = nullptr;
    CHECK_FALSE(BeginAutoAssign(nullptr, s, cancel, th));
    CHECK(th == nullptr);
}

// ---------------------------------------------------------------------------
// State revision — stale auto-assign guard
// ---------------------------------------------------------------------------

TEST_CASE("State revision diverges when chart changes between start and done") {
    AppState s; s.Init();
    s.roster = {L"Alice", L"Bob"};
    { AppState::Transaction tx(s); tx->roster = s.roster; tx.Commit(); }
    const uint64_t startRev = s.Revision();

    // Simulate a furniture-seat change after auto-assign was launched.
    {
        AppState::Transaction tx(s);
        LayoutItem desk; desk.type = LayoutItemType::RectangleDesk; desk.capacity = 1;
        EnsureSeatSlots(desk);
        desk.occupants[0] = L"Charlie";
        tx->layoutItems.push_back(desk);
        tx.Commit();
    }

    CHECK(s.Revision() != startRev);   // revision diverged → stale result must be discarded
}

TEST_CASE("Quick fill places roster names into empty furniture seats in order") {
    AppState s; s.Init();
    s.roster = {L"Alice", L"Bob", L"Charlie"};
    // One Table4 (4 seats); seat 0 is already occupied by Alice.
    LayoutItem table; table.type = LayoutItemType::Table4;
    table.capacity = LayoutItemDefaultCapacity(LayoutItemType::Table4);
    EnsureSeatSlots(table);
    table.occupants[0] = L"Alice";
    s.layoutItems.push_back(table);

    // Replicate QuickFillSeats logic (the real one lives in SeatingChartApp
    // which has UI deps — test the logic independently) over furniture seats.
    std::unordered_set<std::wstring> assigned;
    for (const auto& item : s.layoutItems)
        for (const auto& occ : item.occupants) {
            auto cn = CanonicalName(occ); if (!cn.empty()) assigned.insert(cn);
        }
    size_t ri = 0; int filled = 0;
    const auto seatRefs = EnumerateLayoutSeats(s.layoutItems);
    for (const auto& ref : seatRefs) {
        auto& occ = s.layoutItems[static_cast<size_t>(ref.first)]
                        .occupants[static_cast<size_t>(ref.second)];
        if (!occ.empty()) continue;
        while (ri < s.roster.size()) {
            const auto& cand = s.roster[ri++];
            const auto cn = CanonicalName(cand);
            if (cn.empty() || assigned.count(cn)) continue;
            occ = cand; assigned.insert(cn); ++filled; break;
        }
        if (ri >= s.roster.size()) break;
    }
    const auto& occ = s.layoutItems[0].occupants;
    CHECK(filled == 2);
    CHECK(occ[0] == L"Alice");   // unchanged
    CHECK(occ[1] == L"Bob");
    CHECK(occ[2] == L"Charlie");
}

// ---------------------------------------------------------------------------
// SolveAutoAssign — pure constraint solver (radius-based keep-apart)
// ---------------------------------------------------------------------------

TEST_CASE("SolveAutoAssign places all roster students when unconstrained") {
    AutoAssignInput in;
    in.seatCount      = 3;
    in.roster         = { L"Alice", L"Bob", L"Charlie" };
    in.fixedOccupants = { L"", L"", L"" };
    in.seatItem       = { 0, 1, 2 };
    in.seatCentres    = { {0,0}, {100,0}, {200,0} };
    const auto sol = SolveAutoAssign(in);
    REQUIRE(sol.success);
    std::unordered_set<int> seats(sol.assignments.begin(), sol.assignments.end());
    CHECK(seats.size() == 3u);      // distinct seats
    CHECK(seats.count(-1) == 0u);   // nobody unplaced
}

TEST_CASE("SolveAutoAssign radius 0 forbids a restricted pair on the same item") {
    AutoAssignInput in;
    in.seatCount      = 2;
    in.roster         = { L"Alice", L"Bob" };
    in.fixedOccupants = { L"", L"" };
    in.seatItem       = { 0, 0 };                   // both seats on one furniture item
    in.seatCentres    = { {0,0}, {40,0} };
    in.restrictions   = { { L"Alice", L"Bob", 0 } }; // radius 0 = same-item keep-apart
    const auto sol = SolveAutoAssign(in);
    CHECK_FALSE(sol.success);   // can't separate them onto different items
}

TEST_CASE("SolveAutoAssign radius 0 allows a restricted pair on different items") {
    AutoAssignInput in;
    in.seatCount      = 2;
    in.roster         = { L"Alice", L"Bob" };
    in.fixedOccupants = { L"", L"" };
    in.seatItem       = { 0, 1 };                   // separate items
    in.seatCentres    = { {0,0}, {40,0} };           // close, but radius 0 ignores distance
    in.restrictions   = { { L"Alice", L"Bob", 0 } };
    const auto sol = SolveAutoAssign(in);
    CHECK(sol.success);
}

TEST_CASE("SolveAutoAssign radius keeps a restricted pair beyond the radius") {
    AutoAssignInput in;
    in.seatCount      = 3;
    in.roster         = { L"Alice", L"Bob" };
    in.fixedOccupants = { L"", L"", L"" };
    in.seatItem       = { 0, 1, 2 };
    in.seatCentres    = { {0,0}, {100,0}, {300,0} };  // 0-1 = 100, 0-2 = 300, 1-2 = 200
    in.restrictions   = { { L"Alice", L"Bob", 150 } };
    const auto sol = SolveAutoAssign(in);
    REQUIRE(sol.success);
    const int sa = sol.assignments[0], sb = sol.assignments[1];
    REQUIRE(sa >= 0); REQUIRE(sb >= 0);
    const int dx  = in.seatCentres[static_cast<size_t>(sa)].x -
                    in.seatCentres[static_cast<size_t>(sb)].x;
    const int adx = dx < 0 ? -dx : dx;
    CHECK(adx > 150);   // placed beyond the keep-apart radius
}

TEST_CASE("SolveAutoAssign radius is infeasible when every seat is too close") {
    AutoAssignInput in;
    in.seatCount      = 2;
    in.roster         = { L"Alice", L"Bob" };
    in.fixedOccupants = { L"", L"" };
    in.seatItem       = { 0, 1 };                    // different items → radius 0 wouldn't fire
    in.seatCentres    = { {0,0}, {100,0} };           // only 100 apart
    in.restrictions   = { { L"Alice", L"Bob", 150 } };
    const auto sol = SolveAutoAssign(in);
    CHECK_FALSE(sol.success);   // failure is purely distance-driven
}

TEST_CASE("SolveAutoAssign keeps fixed occupants in place") {
    AutoAssignInput in;
    in.seatCount      = 3;
    in.roster         = { L"Alice", L"Bob" };
    in.fixedOccupants = { L"Charlie", L"", L"" };     // seat 0 pre-filled
    in.seatItem       = { 0, 1, 2 };
    in.seatCentres    = { {0,0}, {100,0}, {200,0} };
    const auto sol = SolveAutoAssign(in);
    REQUIRE(sol.success);
    CHECK(sol.assignments[0] != 0);                  // Alice not on the fixed seat
    CHECK(sol.assignments[1] != 0);                  // Bob not on the fixed seat
    CHECK(sol.assignments[0] != sol.assignments[1]);
}

TEST_CASE("SolveAutoAssign forces a front-row student into a front-band seat") {
    AutoAssignInput in;
    in.seatCount      = 2;
    in.roster         = { L"Alice", L"Bob" };
    in.fixedOccupants = { L"", L"" };
    in.seatItem       = { 0, 1 };
    in.frontEdge      = RoomEdge::Top;
    in.roomW = 1000; in.roomH = 800;                 // front band ≈ y <= 272
    in.seatCentres    = { {500, 700}, {500, 50} };   // seat 0 back, seat 1 front
    in.rosterFrontRequired = { 1, 0 };               // Alice must sit front
    const auto sol = SolveAutoAssign(in);
    REQUIRE(sol.success);
    CHECK(sol.assignments[0] == 1);                  // Alice → the front seat
    CHECK(sol.assignments[1] == 0);                  // Bob → the back seat
}

TEST_CASE("SolveAutoAssign front-row is infeasible when no seat is in front") {
    AutoAssignInput in;
    in.seatCount      = 2;
    in.roster         = { L"Alice", L"Bob" };
    in.fixedOccupants = { L"", L"" };
    in.seatItem       = { 0, 1 };
    in.frontEdge      = RoomEdge::Top;
    in.roomW = 1000; in.roomH = 800;
    in.seatCentres    = { {500, 700}, {500, 600} };  // both seats well behind the band
    in.rosterFrontRequired = { 1, 0 };
    const auto sol = SolveAutoAssign(in);
    CHECK_FALSE(sol.success);
}

TEST_CASE("SolveAutoAssign refines sit-near affinities to reduce pair distance") {
    AutoAssignInput in;
    in.seatCount      = 3;
    in.roster         = { L"Alice", L"Bob", L"Carol" };
    in.fixedOccupants = { L"", L"", L"" };
    in.seatItem       = { 0, 1, 2 };
    // Natural index placement would put Alice@0 and Bob@1 (1000 apart); the
    // refinement should swap so the affinity pair ends up adjacent.
    in.seatCentres    = { {0,0}, {1000,0}, {50,0} };
    in.affinities     = { { L"Alice", L"Bob" } };
    const auto sol = SolveAutoAssign(in);
    REQUIRE(sol.success);
    const int sa = sol.assignments[0], sb = sol.assignments[1];
    REQUIRE(sa >= 0); REQUIRE(sb >= 0);
    const int dx  = in.seatCentres[static_cast<size_t>(sa)].x -
                    in.seatCentres[static_cast<size_t>(sb)].x;
    const int adx = dx < 0 ? -dx : dx;
    CHECK(adx <= 100);   // affinity pulled the pair together (was 1000 unrefined)
}

TEST_CASE("SolveAutoAssign affinity refinement never breaks a keep-apart rule") {
    AutoAssignInput in;
    in.seatCount      = 3;
    in.roster         = { L"Alice", L"Bob", L"Carol" };
    in.fixedOccupants = { L"", L"", L"" };
    in.seatItem       = { 0, 1, 2 };
    in.seatCentres    = { {0,0}, {50,0}, {1000,0} };
    in.affinities     = { { L"Alice", L"Bob" } };        // want them near…
    in.restrictions   = { { L"Alice", L"Bob", 200 } };   // …but hard keep-apart ≥200 wins
    const auto sol = SolveAutoAssign(in);
    REQUIRE(sol.success);
    const int sa = sol.assignments[0], sb = sol.assignments[1];
    const int dx  = in.seatCentres[static_cast<size_t>(sa)].x -
                    in.seatCentres[static_cast<size_t>(sb)].x;
    const int adx = dx < 0 ? -dx : dx;
    CHECK(adx > 200);   // hard constraint still satisfied after soft refinement
}
