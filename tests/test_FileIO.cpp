#include <doctest/doctest.h>
#include "AppState.h"
#include "FileIO.h"
#include "Utils.h"

// g_dpi defined once in test_AppState.cpp; extern reference here.
extern UINT g_dpi;

// ---------------------------------------------------------------------------
// JSON round-trip
// ---------------------------------------------------------------------------

static AppState MakeTestState() {
    AppState s; s.Init();
    s.roster = { L"Alice", L"Bob", L"Charlie" };
    s.restrictions.push_back({ L"Alice", L"Bob", 120 });   // with keep-apart radius
    s.affinities.push_back({ L"Alice", L"Charlie" });      // sit-near soft preference
    s.mustTogether.push_back({ L"Bob", L"Charlie" });      // must sit together
    s.chartMode = ChartMode::Seats;
    s.frontEdge = RoomEdge::Bottom;   // non-default, to prove it round-trips
    LayoutItem li;
    li.type   = LayoutItemType::Smartboard;
    li.bounds = {10, 10, 100, 50};
    li.label  = L"Front Board";
    s.layoutItems.push_back(li);
    return s;
}

TEST_CASE("JSON round-trip preserves all fields") {
    const AppState original = MakeTestState();
    const std::string json = BuildStateJson(original);

    AppState loaded; loaded.Init();
    REQUIRE(LoadStateFromJson(json, &loaded));

    CHECK(loaded.chartMode == original.chartMode);
    CHECK(loaded.frontEdge == RoomEdge::Bottom);   // front edge persisted
    CHECK(loaded.roster == original.roster);
    REQUIRE(loaded.restrictions.size() == 1u);
    // Restrictions are normalized (alphabetically sorted pair).
    CHECK(CanonicalName(loaded.restrictions[0].first)  != L"");
    CHECK(CanonicalName(loaded.restrictions[0].second) != L"");
    CHECK(loaded.restrictions[0].radius == 120);   // keep-apart radius persisted
    REQUIRE(loaded.affinities.size() == 1u);       // sit-near affinity persisted
    CHECK(CanonicalName(loaded.affinities[0].first)  == L"alice");
    CHECK(CanonicalName(loaded.affinities[0].second) == L"charlie");
    REQUIRE(loaded.mustTogether.size() == 1u);     // must-together persisted (v7)
    CHECK(CanonicalName(loaded.mustTogether[0].first)  == L"bob");
    CHECK(CanonicalName(loaded.mustTogether[0].second) == L"charlie");
    REQUIRE(loaded.layoutItems.size() == 1u);
    CHECK(loaded.layoutItems[0].type  == LayoutItemType::Smartboard);
    CHECK(loaded.layoutItems[0].label == L"Front Board");
}

TEST_CASE("Layout seat occupants round-trip and stay sized to capacity") {
    AppState s; s.Init();
    LayoutItem table;
    table.type     = LayoutItemType::Table4;
    table.bounds   = { 0, 0, 200, 200 };
    table.capacity = LayoutItemDefaultCapacity(LayoutItemType::Table4);
    EnsureSeatSlots(table);
    REQUIRE(table.occupants.size() == 4u);
    table.occupants[0] = L"Alice";
    table.occupants[2] = L"Charlie";
    s.layoutItems.push_back(table);

    const std::string json = BuildStateJson(s);
    AppState loaded; loaded.Init();
    REQUIRE(LoadStateFromJson(json, &loaded));
    REQUIRE(loaded.layoutItems.size() == 1u);
    const auto& occ = loaded.layoutItems[0].occupants;
    REQUIRE(occ.size() == 4u);          // re-sized to seat count on load
    CHECK(occ[0] == L"Alice");
    CHECK(occ[1].empty());
    CHECK(occ[2] == L"Charlie");
    CHECK(occ[3].empty());
}

TEST_CASE("Older files without occupants load with empty, correctly-sized seats") {
    // v4-style layout item with no "occupants" field.
    const std::string json = R"({"version":5,"save_dpi":96,
        "grid":{"rows":2,"cols":2},"mode":"Layout","seats":["","","",""],
        "roster":[],"restrictions":[],"room_w":0,"room_h":0,
        "selected_seat":-1,"selected_layout_item":-1,
        "layout_items":[{"type":"Table4","label":"T","bounds":[0,0,200,200]}]})";
    AppState loaded; loaded.Init();
    REQUIRE(LoadStateFromJson(json, &loaded));
    REQUIRE(loaded.layoutItems.size() == 1u);
    CHECK(loaded.layoutItems[0].occupants.size() == 4u);
    for (const auto& o : loaded.layoutItems[0].occupants) CHECK(o.empty());
}

TEST_CASE("Old grid fields (grid/seats/selected_seat) are ignored on load") {
    // A pre-unify v5 file with a populated rows×cols grid. The grid model is
    // retired, so the grid/seats/selected_seat fields must be ignored without
    // failing the load — only roster/restrictions/layout survive.
    const std::string json = R"({"version":5,"save_dpi":96,
        "grid":{"rows":2,"cols":2},"mode":"Seats","seats":["Alice","Bob","",""],
        "roster":["Alice","Bob"],"restrictions":[],"room_w":0,"room_h":0,
        "selected_seat":1,"selected_layout_item":-1,"layout_items":[]})";
    AppState loaded; loaded.Init();
    REQUIRE(LoadStateFromJson(json, &loaded));
    CHECK(loaded.chartMode == ChartMode::Seats);
    CHECK(loaded.roster == std::vector<std::wstring>{ L"Alice", L"Bob" });
    CHECK(loaded.layoutItems.empty());
}

TEST_CASE("JSON survives unknown extra fields (forward compat)") {
    // Simulate a future version adding a new field; legacy grid fields ignored.
    const std::string future = R"({
        "version": 99,
        "grid": {"rows": 4, "cols": 4},
        "mode": "Seats",
        "selected_seat": -1,
        "selected_layout_item": -1,
        "roster": [],
        "seats": ["","","","","","","","","","","","","","","",""],
        "restrictions": [],
        "layout_items": [],
        "new_field_from_future": true
    })";
    AppState loaded; loaded.Init();
    CHECK(LoadStateFromJson(future, &loaded));
    CHECK(loaded.chartMode == ChartMode::Seats);
}

TEST_CASE("LoadStateFromJson rejects malformed JSON") {
    AppState s; s.Init();
    CHECK_FALSE(LoadStateFromJson("not json at all", &s));
    CHECK_FALSE(LoadStateFromJson("{}", &s)); // missing required fields
}

// ---------------------------------------------------------------------------
// Encoding helpers
// ---------------------------------------------------------------------------

TEST_CASE("Utf8/Wide round-trip") {
    const std::wstring wide = L"Hello 世界"; // "Hello 世界"
    const std::string  utf8 = WideToUtf8(wide);
    CHECK(!utf8.empty());
    CHECK(Utf8ToWide(utf8) == wide);
}

TEST_CASE("EscapeQuoted handles special chars") {
    const std::wstring raw = L"Alice \"Bob\" \\ \n";
    const std::wstring esc = EscapeQuoted(raw);
    CHECK(esc.front() == L'"');
    CHECK(esc.back()  == L'"');
    // The escaped form should contain no unescaped newlines.
    CHECK(esc.find(L'\n') == std::wstring::npos);
}

// ---------------------------------------------------------------------------
// JSON strict validation
// ---------------------------------------------------------------------------

TEST_CASE("LoadStateFromJson rejects unknown mode value") {
    const std::string bad = R"({"version":2,"save_dpi":96,"mode":"ClassMode",
        "selected_layout_item":-1,
        "roster":[],"restrictions":[],"layout_items":[]})";
    AppState s; s.Init();
    CHECK_FALSE(LoadStateFromJson(bad, &s));
}

TEST_CASE("LoadStateFromJson accepts both valid modes") {
    // "Seats"
    {
        const std::string seats = R"({"version":2,"save_dpi":96,"mode":"Seats",
            "selected_layout_item":-1,
            "roster":[],"restrictions":[],"layout_items":[]})";
        AppState s; s.Init();
        CHECK(LoadStateFromJson(seats, &s));
        CHECK(s.chartMode == ChartMode::Seats);
    }
    // "Layout"
    {
        const std::string layout = R"({"version":2,"save_dpi":96,"mode":"Layout",
            "selected_layout_item":-1,
            "roster":[],"restrictions":[],"layout_items":[]})";
        AppState s; s.Init();
        CHECK(LoadStateFromJson(layout, &s));
        CHECK(s.chartMode == ChartMode::Layout);
    }
}

TEST_CASE("LoadStateFromJson rejects unknown layout item type") {
    const std::string bad = R"({"version":2,"save_dpi":96,"mode":"Seats",
        "selected_layout_item":-1,
        "roster":[],"restrictions":[],
        "layout_items":[{"type":"HoverBoard","label":"X","bounds":[0,0,50,50]}]})";
    AppState s; s.Init();
    CHECK_FALSE(LoadStateFromJson(bad, &s));
}

TEST_CASE("LoadStateFromJson rejects duplicate roster names") {
    const std::string bad = R"({"version":2,"save_dpi":96,"mode":"Seats",
        "selected_layout_item":-1,
        "roster":["Alice","alice"],"restrictions":[],"layout_items":[]})";
    AppState s; s.Init();
    CHECK_FALSE(LoadStateFromJson(bad, &s));
}

// ---------------------------------------------------------------------------
// SCAT1 legacy parser — strict validation
// ---------------------------------------------------------------------------

static const std::wstring kMinScat1 =
    L"SCAT1\n"
    L"grid_rows 2\n"
    L"grid_cols 2\n"
    L"mode Seats\n"
    L"roster_count 0\n"
    L"selected -1\n"
    L"seat_count 4\n"
    L"seat 0 \"\"\n"
    L"seat 1 \"\"\n"
    L"seat 2 \"\"\n"
    L"seat 3 \"\"\n"
    L"restriction_count 0\n"
    L"layout_item_count 0\n";

TEST_CASE("Valid minimal SCAT1 file loads successfully") {
    AppState s; s.Init();
    CHECK(LoadStateFromText(kMinScat1, &s));
    // The legacy grid is parsed for validation but no longer stored; only the
    // mode/roster/restrictions/layout survive.
    CHECK(s.chartMode == ChartMode::Seats);
}

TEST_CASE("SCAT1 rejects unknown mode value") {
    std::wstring bad = kMinScat1;
    const size_t p = bad.find(L"mode Seats");
    bad.replace(p, 10, L"mode RoundTable");
    AppState s; s.Init();
    CHECK_FALSE(LoadStateFromText(bad, &s));
}

TEST_CASE("SCAT1 rejects duplicate seat index") {
    std::wstring bad = kMinScat1;
    // Replace seat 3 with a second seat 0
    const size_t p = bad.find(L"seat 3 \"\"");
    bad.replace(p, 9, L"seat 0 \"\"");
    AppState s; s.Init();
    CHECK_FALSE(LoadStateFromText(bad, &s));
}

TEST_CASE("SCAT1 rejects trailing garbage after roster quoted value") {
    std::wstring bad =
        L"SCAT1\n"
        L"grid_rows 2\n"
        L"grid_cols 2\n"
        L"mode Seats\n"
        L"roster_count 1\n"
        L"roster \"Alice\" GARBAGE\n"   // trailing garbage
        L"selected -1\n"
        L"seat_count 4\n"
        L"seat 0 \"\"\n"
        L"seat 1 \"\"\n"
        L"seat 2 \"\"\n"
        L"seat 3 \"\"\n"
        L"restriction_count 0\n"
        L"layout_item_count 0\n";
    AppState s; s.Init();
    CHECK_FALSE(LoadStateFromText(bad, &s));
}

TEST_CASE("SCAT1 rejects extra non-empty lines after data") {
    std::wstring bad = kMinScat1 + L"unexpected extra line\n";
    AppState s; s.Init();
    CHECK_FALSE(LoadStateFromText(bad, &s));
}

TEST_CASE("SCAT1 rejects unknown layout item type") {
    std::wstring bad =
        L"SCAT1\n"
        L"grid_rows 2\n"
        L"grid_cols 2\n"
        L"mode Seats\n"
        L"roster_count 0\n"
        L"selected -1\n"
        L"seat_count 4\n"
        L"seat 0 \"\"\n"
        L"seat 1 \"\"\n"
        L"seat 2 \"\"\n"
        L"seat 3 \"\"\n"
        L"restriction_count 0\n"
        L"layout_item_count 1\n"
        L"layout_item UFO 0,0,50,50\n";
    AppState s; s.Init();
    CHECK_FALSE(LoadStateFromText(bad, &s));
}
