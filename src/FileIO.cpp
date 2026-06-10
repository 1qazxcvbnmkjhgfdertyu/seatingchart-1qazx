#include "FileIO.h"
#include "Utils.h"
#include <algorithm>
#include <cstring>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <windows.h>

using njson = nlohmann::json; // avoid colliding with any local "json" variables

// ---------------------------------------------------------------------------
// Raw I/O
// ---------------------------------------------------------------------------

bool ReadAllBytes(const std::wstring& path, std::vector<unsigned char>* bytes) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart < 0 ||
        sz.QuadPart > 0x7fffffff) { CloseHandle(f); return false; }
    bytes->resize(static_cast<size_t>(sz.QuadPart));
    DWORD read = 0;
    const bool ok = bytes->empty() ||
        (ReadFile(f, bytes->data(), static_cast<DWORD>(bytes->size()), &read, nullptr) &&
         read == static_cast<DWORD>(bytes->size()));
    CloseHandle(f);
    return ok;
}

bool WriteAllBytesAtomic(const std::wstring& path, const std::vector<unsigned char>& bytes) {
    const size_t slash = path.find_last_of(L"\\/");
    const std::wstring dir = (slash == std::wstring::npos) ? L"" : path.substr(0, slash);
    if (!EnsureDirectoryExists(dir)) return false;
    const std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = bytes.empty() ||
        (WriteFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
         written == static_cast<DWORD>(bytes.size()));
    FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    if (!MoveFileExW(tmp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring r(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), r.data(), n);
    return r;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                       nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string r(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                         r.data(), n, nullptr, nullptr);
    return r;
}

bool ReadTextFileUtf8(const std::wstring& path, std::wstring* out) {
    std::vector<unsigned char> bytes;
    if (!ReadAllBytes(path, &bytes)) return false;
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        const auto* d = reinterpret_cast<const wchar_t*>(bytes.data() + 2);
        out->assign(d, d + (bytes.size() - 2) / sizeof(wchar_t));
        return true;
    }
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
        bytes.erase(bytes.begin(), bytes.begin() + 3);
    *out = Utf8ToWide(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    return true;
}

bool WriteTextFileUtf8Atomic(const std::wstring& path, const std::wstring& text) {
    const auto utf8 = WideToUtf8(text);
    return WriteAllBytesAtomic(path, { utf8.begin(), utf8.end() });
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

std::wstring GetAppDataStateDir() {
    wchar_t buf[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (!len || len >= MAX_PATH) return {};
    return std::wstring(buf) + L"\\SeatingChartApp";
}

std::wstring GetStateFilePath()        { auto d=GetAppDataStateDir(); return d.empty()?L"":d+L"\\state.json"; }
std::wstring GetLegacyStateFilePath()  { auto d=GetAppDataStateDir(); return d.empty()?L"":d+L"\\state.txt";  }
std::wstring GetRosterFilePath()       { auto d=GetAppDataStateDir(); return d.empty()?L"":d+L"\\roster.txt"; }
std::wstring GetRestrictionsFilePath() { auto d=GetAppDataStateDir(); return d.empty()?L"":d+L"\\restrictions.txt"; }

bool EnsureDirectoryExists(const std::wstring& dir) {
    if (dir.empty()) return false;
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

// ---------------------------------------------------------------------------
// Migration helper: old screen-space bounds → room-local coords (v2/v3)
//
// Old saves stored item bounds as client/screen pixel coordinates relative to
// the top-left of the application window, DPI-scaled at saveDpi.  The room
// area started at {Margin()+12, HeaderHeight()+12} = {28, 64} at 96 DPI.
// We normalise to 96 DPI then subtract that room origin.
// ---------------------------------------------------------------------------
static RECT MigrateScreenBoundsToRoom(const RECT& b, int savedDpi) {
    const int dpi = savedDpi > 0 ? savedDpi : 96;
    RECT r{
        MulDiv(static_cast<int>(b.left),   96, dpi),
        MulDiv(static_cast<int>(b.top),    96, dpi),
        MulDiv(static_cast<int>(b.right),  96, dpi),
        MulDiv(static_cast<int>(b.bottom), 96, dpi)
    };
    // Subtract 96-DPI room origin (Margin96=16, HeaderHeight96=52, +12 inflate)
    constexpr int kOriginX = 28; // 16 + 12
    constexpr int kOriginY = 64; // 52 + 12
    r.left -= kOriginX; r.right  -= kOriginX;
    r.top  -= kOriginY; r.bottom -= kOriginY;
    return NormalizeRect(r);
}

// ---------------------------------------------------------------------------
// JSON serialization — version 4 (room-local coordinates, DPI-independent)
// ---------------------------------------------------------------------------

std::string BuildStateJson(const AppState& s) {
    njson j;
    // v8: added "group_affinities" for clustered groups.
    // v7: added "must_together" for hard "must sit together" pairs (same furniture or close).
    // v6: the rows×cols seat grid is retired — furniture owns the seats. The
    // "grid"/"seats"/"selected_seat" fields are no longer written. Older files
    // that still carry them load fine (the fields are ignored on read).
    j["version"]  = 8;
    j["save_dpi"] = static_cast<int>(s.saveDpi); // kept for diagnostics; not used on load v4

    j["mode"]                 = (s.chartMode == ChartMode::Layout) ? "Layout" : "Seats";
    j["selected_layout_item"] = s.selectedLayoutItem.value_or(-1);
    j["room_w"]               = s.roomW;  // logical room units
    j["room_h"]               = s.roomH;
    j["front_edge"]           = WideToUtf8(std::wstring(RoomEdgeName(s.frontEdge)));
    j["auto_assign_limit"]    = static_cast<int>(s.autoAssignSearchLimit);
    j["show_last_names"]      = s.showLastNames;

    j["roster"] = njson::array();
    for (const auto& n : s.roster)
        j["roster"].push_back(WideToUtf8(n));

    // Per-student records (attributes / notes / colour), keyed by canonical name.
    j["students"] = njson::array();
    for (const auto& [key, info] : s.studentInfo) {
        if (info.tags.empty() && info.notes.empty() && info.color == 0 &&
            info.forbiddenDesks.empty()) continue;
        njson js;
        js["name"] = WideToUtf8(key);
        if (info.color != 0)       js["color"] = static_cast<int>(info.color);
        if (!info.notes.empty())   js["notes"] = WideToUtf8(info.notes);
        if (!info.tags.empty()) {
            njson t = njson::array();
            for (const auto& tg : info.tags) t.push_back(WideToUtf8(tg));
            js["tags"] = std::move(t);
        }
        if (!info.forbiddenDesks.empty()) {
            njson f = njson::array();
            for (const auto& fd : info.forbiddenDesks) f.push_back(WideToUtf8(fd));
            js["forbidden"] = std::move(f);
        }
        j["students"].push_back(std::move(js));
    }

    j["restrictions"] = njson::array();
    for (const auto& r : s.restrictions) {
        njson entry;
        entry["first"]  = WideToUtf8(r.first);
        entry["second"] = WideToUtf8(r.second);
        if (r.radius > 0) entry["radius"] = r.radius;  // 0 = same-item (omit)
        j["restrictions"].push_back(entry);
    }

    j["affinities"] = njson::array();   // soft "sit near" preferences (weight optional, default 1)
    for (const auto& a : s.affinities) {
        njson entry;
        entry["first"]  = WideToUtf8(a.first);
        entry["second"] = WideToUtf8(a.second);
        if (a.weight > 1) entry["weight"] = a.weight;
        j["affinities"].push_back(entry);
    }

    j["must_together"] = njson::array();  // hard must-sit-together (v7+)
    for (const auto& t : s.mustTogether) {
        njson entry;
        entry["first"]  = WideToUtf8(t.first);
        entry["second"] = WideToUtf8(t.second);
        j["must_together"].push_back(entry);
    }

    j["group_affinities"] = njson::array(); // groups (v8+)
    for (const auto& g : s.groupAffinities) {
        njson grp = njson::array();
        for (const auto& name : g) grp.push_back(WideToUtf8(name));
        j["group_affinities"].push_back(grp);
    }

    j["layout_items"] = njson::array();
    for (const auto& item : s.layoutItems) {
        njson li;
        li["type"]     = WideToUtf8(std::wstring(LayoutTypeName(item.type)));
        li["label"]    = WideToUtf8(item.label);
        li["rotation"] = item.rotation;
        li["locked"]   = item.locked;
        li["flipped"]  = item.flipped;
        li["capacity"] = item.capacity;
        li["visible"]  = item.visible;
        if (item.groupId > 0) li["group_id"] = item.groupId;
        njson occ = njson::array();
        for (const auto& name : item.occupants) occ.push_back(WideToUtf8(name));
        li["occupants"] = std::move(occ);
        bool anyBlocked = false;
        for (bool b : item.blockedSeats) if (b) { anyBlocked = true; break; }
        if (anyBlocked) {
            njson blk = njson::array();
            for (bool b : item.blockedSeats) blk.push_back(b);
            li["blocked_seats"] = std::move(blk);
        }
        njson bounds = njson::array();
        bounds.push_back(static_cast<int>(item.bounds.left));
        bounds.push_back(static_cast<int>(item.bounds.top));
        bounds.push_back(static_cast<int>(item.bounds.right));
        bounds.push_back(static_cast<int>(item.bounds.bottom));
        li["bounds"] = bounds;
        j["layout_items"].push_back(li);
    }

    return j.dump(2);
}

// Returns kInvalidLayoutType sentinel when name is unrecognised.
static constexpr LayoutItemType kInvalidLayoutType = static_cast<LayoutItemType>(-1);

static LayoutItemType StrictLayoutTypeFromName(const std::string& name) {
    if (name == "Smartboard")    return LayoutItemType::Smartboard;
    if (name == "TrapezoidDesk") return LayoutItemType::TrapezoidDesk;
    if (name == "RectangleDesk") return LayoutItemType::RectangleDesk;
    if (name == "Table4")        return LayoutItemType::Table4;
    if (name == "BigTable")      return LayoutItemType::BigTable;
    if (name == "TrapPair")      return LayoutItemType::TrapPair;
    if (name == "TrapPod")       return LayoutItemType::TrapPod;
    if (name == "LDesk")         return LayoutItemType::LDesk;
    if (name == "UDesk")         return LayoutItemType::UDesk;
    return kInvalidLayoutType;
}

bool LoadStateFromJson(const std::string& text, AppState* out) {
    try {
        const njson j = njson::parse(text);

        const int version = j.value("version", 2);

        // The rows×cols seat grid is retired (v6+). Any "grid"/"seats"/
        // "selected_seat" fields in older files are ignored — furniture owns
        // the seats now.

        // save_dpi only used for migration of v2/v3
        const int savedDpi = j.value("save_dpi", 96);
        if (savedDpi <= 0 || savedDpi > 960) return false;

        // --- mode ---
        const std::string modeStr = j.value("mode", std::string{});
        if (modeStr != "Seats" && modeStr != "Layout") return false;
        const ChartMode mode = (modeStr == "Layout") ? ChartMode::Layout : ChartMode::Seats;
        const bool showLastNames = j.value("show_last_names", true);

        // --- roster ---
        const njson& rosterArr = j["roster"];
        if (!rosterArr.is_array()) return false;
        if (static_cast<int>(rosterArr.size()) > kMaxRosterCount) return false;
        std::vector<std::wstring> roster;
        roster.reserve(rosterArr.size());
        std::unordered_set<std::wstring> rosterCanon;
        for (const auto& n : rosterArr) {
            if (!n.is_string()) return false;
            const auto wname = Utf8ToWide(n.get<std::string>());
            const auto cn = CanonicalName(wname);
            if (cn.empty()) return false;
            if (!rosterCanon.insert(cn).second) return false;
            roster.push_back(wname);
        }

        // --- student records (optional; attributes / notes / colour) ---
        std::unordered_map<std::wstring, StudentInfo> studentInfo;
        if (j.contains("students") && j["students"].is_array()) {
            for (const auto& js : j["students"]) {
                if (!js.is_object()) continue;
                const std::wstring key =
                    CanonicalName(Utf8ToWide(js.value("name", std::string{})));
                if (key.empty()) continue;
                StudentInfo info;
                info.color = static_cast<uint32_t>(js.value("color", 0));
                info.notes = Utf8ToWide(js.value("notes", std::string{}));
                if (js.contains("tags") && js["tags"].is_array())
                    for (const auto& t : js["tags"])
                        if (t.is_string()) info.tags.push_back(Utf8ToWide(t.get<std::string>()));
                if (js.contains("forbidden") && js["forbidden"].is_array())
                    for (const auto& f : js["forbidden"])
                        if (f.is_string()) info.forbiddenDesks.push_back(Utf8ToWide(f.get<std::string>()));
                studentInfo[key] = std::move(info);
            }
        }

        // --- restrictions ---
        const njson& restrArr = j["restrictions"];
        if (!restrArr.is_array()) return false;
        if (static_cast<int>(restrArr.size()) > kMaxRestrictionCount) return false;
        std::vector<Restriction> restrictions;
        restrictions.reserve(restrArr.size());
        for (const auto& r : restrArr) {
            if (!r.is_object()) return false;
            const auto first  = Utf8ToWide(r.value("first",  std::string{}));
            const auto second = Utf8ToWide(r.value("second", std::string{}));
            const auto cf = CanonicalName(first), cs = CanonicalName(second);
            if (cf.empty() || cs.empty() || cf == cs) return false;
            const int radius = r.value("radius", 0);
            if (radius < 0) return false;
            Restriction rule{first, second};
            rule.radius = radius;
            restrictions.push_back(NormalizeRestriction(std::move(rule)));
        }

        // --- affinities (optional soft "sit near" prefs; lenient on bad rows) ---
        std::vector<Restriction> affinities;
        if (j.contains("affinities") && j["affinities"].is_array()) {
            if (static_cast<int>(j["affinities"].size()) > kMaxRestrictionCount) return false;
            for (const auto& a : j["affinities"]) {
                if (!a.is_object()) continue;
                const auto first  = Utf8ToWide(a.value("first",  std::string{}));
                const auto second = Utf8ToWide(a.value("second", std::string{}));
                const auto cf = CanonicalName(first), cs = CanonicalName(second);
                if (cf.empty() || cs.empty() || cf == cs) continue;
                Restriction r{first, second};
                r.weight = a.value("weight", 1);
                affinities.push_back(NormalizeRestriction(r));
            }
        }

        // --- must_together (v7+ hard "must sit together"; optional, lenient) ---
        std::vector<Restriction> mustTogether;
        if (j.contains("must_together") && j["must_together"].is_array()) {
            if (static_cast<int>(j["must_together"].size()) > kMaxRestrictionCount) return false;
            for (const auto& t : j["must_together"]) {
                if (!t.is_object()) continue;
                const auto first  = Utf8ToWide(t.value("first",  std::string{}));
                const auto second = Utf8ToWide(t.value("second", std::string{}));
                const auto cf = CanonicalName(first), cs = CanonicalName(second);
                if (cf.empty() || cs.empty() || cf == cs) continue;
                mustTogether.push_back(NormalizeRestriction({first, second}));
            }
        } else if (version >= 7 && j.contains("togethers")) {
            // legacy alt name during dev
            // (not strictly needed)
        }

        // --- group_affinities (v8+ lists of students to cluster) ---
        std::vector<std::vector<std::wstring>> groupAffinities;
        if (j.contains("group_affinities") && j["group_affinities"].is_array()) {
            for (const auto& g : j["group_affinities"]) {
                if (g.is_array()) {
                    std::vector<std::wstring> grp;
                    for (const auto& n : g) {
                        if (n.is_string()) grp.push_back(Utf8ToWide(n.get<std::string>()));
                    }
                    if (!grp.empty()) groupAffinities.push_back(std::move(grp));
                }
            }
        }

        // --- room dimensions (v3+ logical room units; 0 = auto) ---
        const int roomW = j.value("room_w", 0);
        const int roomH = j.value("room_h", 0);
        if (roomW < 0 || roomH < 0) return false;

        // --- front edge (optional; defaults to Top for older files) ---
        const RoomEdge frontEdge =
            RoomEdgeFromName(Utf8ToWide(j.value("front_edge", std::string("Top"))));

        // --- auto assign limit (v7+ configurable) ---
        const size_t autoAssignLimit = j.value("auto_assign_limit", static_cast<int>(kDefaultAutoAssignSearchLimit));

        // --- layout items ---
        const njson& liArr = j["layout_items"];
        if (!liArr.is_array()) return false;
        if (static_cast<int>(liArr.size()) > kMaxLayoutItemCount) return false;
        std::vector<LayoutItem> layoutItems;
        layoutItems.reserve(liArr.size());
        for (const auto& li : liArr) {
            if (!li.is_object()) return false;
            const auto typeName = li.value("type", std::string{});
            const LayoutItemType liType = StrictLayoutTypeFromName(typeName);
            if (liType == kInvalidLayoutType) return false;
            auto lbl = Utf8ToWide(li.value("label", std::string{}));
            if (lbl.empty()) lbl = std::wstring(LayoutTypeName(liType));
            const njson& b = li["bounds"];
            if (!b.is_array() || b.size() != 4) return false;
            RECT rc{ b[0].get<int>(), b[1].get<int>(), b[2].get<int>(), b[3].get<int>() };
            rc = NormalizeRect(rc);
            LayoutItem item;
            item.type     = liType;
            item.bounds   = rc;
            item.label    = lbl;
            // Accept any rotation; normalize to 0–359. (Legacy files only ever
            // stored 0/90/180/270; v5+ may store arbitrary angles.)
            item.rotation = ((li.value("rotation", 0) % 360) + 360) % 360;
            item.locked   = li.value("locked",   false);
            item.flipped  = li.value("flipped",  false);
            item.capacity = li.value("capacity", 0);
            item.visible  = li.value("visible",  true);
            item.groupId  = std::max(0, li.value("group_id", 0));
            if (li.contains("occupants") && li["occupants"].is_array())
                for (const auto& nm : li["occupants"])
                    item.occupants.push_back(Utf8ToWide(nm.is_string()
                        ? nm.get<std::string>() : std::string{}));
            EnsureSeatSlots(item);   // size occupants[] and blockedSeats[] to seat count
            if (li.contains("blocked_seats") && li["blocked_seats"].is_array()) {
                const auto& blkArr = li["blocked_seats"];
                for (size_t k = 0; k < blkArr.size() && k < item.blockedSeats.size(); ++k)
                    if (blkArr[k].is_boolean()) item.blockedSeats[k] = blkArr[k].get<bool>();
            }
            layoutItems.push_back(std::move(item));
        }

        // --- selected_layout_item ---
        const int seli = j.value("selected_layout_item", -1);
        if (seli < -1 || seli >= static_cast<int>(layoutItems.size())) return false;

        // --- migrate v2/v3: screen-space bounds → room-local coords ---
        // v4+ stores bounds already in room-local coordinates.
        if (version < 4) {
            for (auto& item : layoutItems)
                item.bounds = MigrateScreenBoundsToRoom(item.bounds, savedDpi);
        }

        // --- migrate <v5: rotation used to swap width/height for 90/270 ---
        // v5+ treats rotation as a pure display transform with the footprint
        // stored unrotated, so un-swap legacy 90/270 bounds about their centre.
        if (version < 5) {
            for (auto& item : layoutItems) {
                if (item.rotation == 90 || item.rotation == 270) {
                    const LONG cx = (item.bounds.left + item.bounds.right) / 2;
                    const LONG cy = (item.bounds.top  + item.bounds.bottom) / 2;
                    const LONG w  = item.bounds.right  - item.bounds.left;
                    const LONG h  = item.bounds.bottom - item.bounds.top;
                    item.bounds = { cx - h/2, cy - w/2, cx + h/2, cy + w/2 };
                }
            }
        }

        // All validation passed — commit to *out.
        if (out) {
            out->saveDpi   = static_cast<UINT>(std::max(1, savedDpi));
            out->chartMode = mode;
            out->roster    = std::move(roster);
            out->studentInfo        = std::move(studentInfo);
            out->restrictions       = std::move(restrictions);
            out->affinities         = std::move(affinities);
            out->mustTogether       = std::move(mustTogether);
            out->autoAssignSearchLimit = autoAssignLimit;
            out->groupAffinities    = std::move(groupAffinities);
            out->layoutItems        = std::move(layoutItems);
            out->selectedLayoutItem = (seli >= 0) ? std::optional<int>(seli) : std::nullopt;
            out->selectedLayoutItems.clear();
            if (seli >= 0) out->selectedLayoutItems.push_back(seli);
            // v2/v3 had screen-pixel room dimensions → reset to auto (0)
            out->roomW = (version >= 4) ? roomW : 0;
            out->roomH = (version >= 4) ? roomH : 0;
            out->frontEdge = frontEdge;
            out->showLastNames = showLastNames;
        }
        return true;
    } catch (...) { return false; }
}

bool ValidateStateJson(const std::string& text) {
    return LoadStateFromJson(text, nullptr);
}

// ---------------------------------------------------------------------------
// SCAT1 legacy reader (read-only; new saves write JSON)
// ---------------------------------------------------------------------------

static std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    size_t s = 0;
    while (s <= text.size()) {
        const size_t e = text.find_first_of(L"\r\n", s);
        if (e == std::wstring::npos) { lines.push_back(text.substr(s)); break; }
        lines.push_back(text.substr(s, e - s));
        size_t next = e;
        while (next < text.size() && (text[next] == L'\r' || text[next] == L'\n')) ++next;
        s = next;
    }
    return lines;
}

static bool NoTrailingGarbage(const std::wstring& line, size_t pos) {
    while (pos < line.size()) {
        if (!iswspace(line[pos])) return false;
        ++pos;
    }
    return true;
}

// Legacy SCAT1 grid bounds. The rows×cols seat model is retired, but the
// read-only SCAT1 parser still validates these ranges so malformed old files
// are rejected rather than silently half-loaded.
static constexpr int kScat1DefaultRows = 8, kScat1DefaultCols = 6;
static constexpr int kScat1MinRows = 2, kScat1MaxRows = 12;
static constexpr int kScat1MinCols = 2, kScat1MaxCols = 10;

static bool ParseStateTextCore(const std::wstring& text, AppState* out) {
    const auto lines = SplitLines(text);
    size_t idx = 0;

    auto next = [&]() -> const std::wstring& {
        static const std::wstring empty;
        return idx < lines.size() ? lines[idx] : empty;
    };
    auto consume = [&] { if (idx < lines.size()) ++idx; };

    if (next() != L"SCAT1") return false;
    consume();

    int lr = kScat1DefaultRows, lc = kScat1DefaultCols;
    if (next().rfind(L"grid_rows ", 0) == 0) {
        if (!ParseIntStrict(next().substr(10), kScat1MinRows, kScat1MaxRows, &lr)) return false;
        consume();
    }
    if (next().rfind(L"grid_cols ", 0) == 0) {
        if (!ParseIntStrict(next().substr(10), kScat1MinCols, kScat1MaxCols, &lc)) return false;
        consume();
    }

    if (next().rfind(L"mode ", 0) != 0) return false;
    const std::wstring modeVal = lines[idx].substr(5);
    if (modeVal != L"Seats" && modeVal != L"Layout") return false;
    const ChartMode mode = (modeVal == L"Layout") ? ChartMode::Layout : ChartMode::Seats;
    consume();

    int rc = 0;
    if (!ParsePrefixedInt(next(), L"roster_count ", 0, kMaxRosterCount, &rc)) return false;
    consume();
    std::vector<std::wstring> roster;
    roster.reserve(static_cast<size_t>(rc));
    std::unordered_set<std::wstring> rosterCanon;
    for (int i = 0; i < rc; ++i) {
        if (idx >= lines.size()) return false;
        std::wstring v;
        if (!ParseQuotedValue(lines[idx], L"roster", &v)) return false;
        const auto cn = CanonicalName(v);
        if (cn.empty() || !rosterCanon.insert(cn).second) return false;
        roster.push_back(v);
        ++idx;
    }

    int sel = -1;
    if (!ParsePrefixedInt(next(), L"selected ", -1, lr * lc - 1, &sel)) return false;
    consume();

    int sc = 0;
    if (!ParsePrefixedInt(next(), L"seat_count ", 0, lr * lc, &sc)) return false;
    consume();
    if (sc != lr * lc) return false;

    std::vector<std::wstring> occ(static_cast<size_t>(sc));
    std::vector<bool> seenIdx(static_cast<size_t>(sc), false);
    std::unordered_set<std::wstring> occupantCanon;
    for (int i = 0; i < sc; ++i) {
        if (idx >= lines.size()) return false;
        const std::wstring& line = lines[idx++];
        if (line.rfind(L"seat ", 0) != 0) return false;
        size_t pos = 5;
        while (pos < line.size() && line[pos] == L' ') ++pos;
        const size_t ns = pos;
        while (pos < line.size() && iswdigit(line[pos])) ++pos;
        if (ns == pos || pos >= line.size() || line[pos] != L' ') return false;
        int si = -1;
        if (!ParseIntStrict(line.substr(ns, pos - ns), 0, sc - 1, &si)) return false;
        if (seenIdx[static_cast<size_t>(si)]) return false;
        seenIdx[static_cast<size_t>(si)] = true;
        std::wstring occupant;
        if (!ParseQuotedValue(L"seat " + line.substr(pos + 1), L"seat", &occupant)) return false;
        if (!occupant.empty()) {
            const auto cn = CanonicalName(occupant);
            if (!cn.empty() && !occupantCanon.insert(cn).second) return false;
        }
        occ[static_cast<size_t>(si)] = occupant;
    }
    for (bool seen : seenIdx) if (!seen) return false;

    int resc = 0;
    if (!ParsePrefixedInt(next(), L"restriction_count ", 0, kMaxRestrictionCount, &resc)) return false;
    consume();
    std::vector<Restriction> restrictions;
    restrictions.reserve(static_cast<size_t>(resc));
    for (int i = 0; i < resc; ++i) {
        if (idx >= lines.size()) return false;
        std::wstring f, s;
        if (!ParseQuotedPair(lines[idx++], L"restriction", &f, &s)) return false;
        const auto cf = CanonicalName(f), cs = CanonicalName(s);
        if (cf.empty() || cs.empty() || cf == cs) return false;
        restrictions.push_back(NormalizeRestriction({f, s}));
    }

    int lc2 = 0;
    if (!ParsePrefixedInt(next(), L"layout_item_count ", 0, kMaxLayoutItemCount, &lc2)) return false;
    consume();
    std::vector<LayoutItem> layoutItems;
    layoutItems.reserve(static_cast<size_t>(lc2));
    for (int i = 0; i < lc2; ++i) {
        if (idx >= lines.size()) return false;
        const std::wstring& line = lines[idx++];
        if (line.rfind(L"layout_item ", 0) != 0) return false;
        size_t pos = 12;
        while (pos < line.size() && line[pos] == L' ') ++pos;
        const size_t te = line.find(L' ', pos);
        if (te == std::wstring::npos) return false;
        const std::wstring typeName = line.substr(pos, te - pos);
        const LayoutItemType liType = StrictLayoutTypeFromName(WideToUtf8(typeName));
        if (liType == kInvalidLayoutType) return false;
        const size_t rs = te + 1;
        const size_t re = line.find(L' ', rs);
        const auto rectText = (re == std::wstring::npos) ? line.substr(rs) : line.substr(rs, re - rs);
        RECT rc2{};
        if (!WStringToRect(rectText, &rc2)) return false;
        LayoutItem item;
        item.type   = liType;
        item.bounds = NormalizeRect(rc2);
        item.label  = std::wstring(LayoutTypeName(liType));
        if (re != std::wstring::npos) {
            std::wstring lbl;
            if (ParseQuotedValue(L"label " + line.substr(re + 1), L"label", &lbl)) {
                lbl = TrimCopy(lbl);
                if (!lbl.empty()) item.label = lbl;
            }
        }
        layoutItems.push_back(item);
    }

    while (idx < lines.size()) {
        const std::wstring& extra = lines[idx++];
        if (!TrimCopy(extra).empty()) return false;
    }

    // SCAT1 assumed 96 DPI screen coords — migrate to room-local.
    for (auto& item : layoutItems)
        item.bounds = MigrateScreenBoundsToRoom(item.bounds, 96);

    // The legacy SCAT1 grid (rows/cols/seats/selected) is still parsed above for
    // strict format validation, but the rows×cols seat model is retired — only
    // the roster, restrictions and furniture layout are loaded.
    if (out) {
        out->saveDpi = 96;
        out->roster = roster;
        out->restrictions = restrictions;
        out->affinities.clear();   // legacy SCAT1 has no affinities
        out->layoutItems  = layoutItems;
        out->chartMode    = mode;
        out->selectedLayoutItem  = std::nullopt;
        out->selectedLayoutItems.clear();
        out->roomW = 0; out->roomH = 0;
        out->frontEdge = RoomEdge::Top;   // legacy files have no front concept
    }
    return true;
}

bool ValidateStateText(const std::wstring& text) { return ParseStateTextCore(text, nullptr); }
bool LoadStateFromText(const std::wstring& text, AppState* state) { return ParseStateTextCore(text, state); }

// ---------------------------------------------------------------------------
// High-level load / save
// ---------------------------------------------------------------------------

bool LoadState(AppState* state) {
    const auto jsonPath = GetStateFilePath();
    if (!jsonPath.empty()) {
        std::vector<unsigned char> bytes;
        if (ReadAllBytes(jsonPath, &bytes) && !bytes.empty()) {
            const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            if (LoadStateFromJson(text, state)) return true;
        }
    }
    const auto legacyPath = GetLegacyStateFilePath();
    if (legacyPath.empty()) return false;
    std::wstring content;
    if (!ReadTextFileUtf8(legacyPath, &content)) return false;
    return LoadStateFromText(content, state);
}

void SaveStateNow(AppState* state, bool showSuccessStatus) {
    const auto path = GetStateFilePath();
    if (path.empty()) { state->status = L"Could not resolve AppData path"; return; }

    state->saveDpi = g_dpi ? g_dpi : 96;

    const std::string stateJson = BuildStateJson(*state);
    const std::vector<unsigned char> bytes(stateJson.begin(), stateJson.end());

    std::vector<unsigned char> prev;
    if (ReadAllBytes(path, &prev) && !prev.empty()) {
        const std::string prevText(reinterpret_cast<const char*>(prev.data()), prev.size());
        if (ValidateStateJson(prevText))
            CopyFileW(path.c_str(), (path + L".bak").c_str(), FALSE);
    }

    if (!WriteAllBytesAtomic(path, bytes)) { state->status = L"Save failed"; return; }
    state->dirty = false;
    if (showSuccessStatus) state->status = L"Saved";
}

void ScheduleAutoSave(AppState* state, HWND hwnd) {
    state->dirty = true;
    if (!hwnd) { SaveStateNow(state, false); return; }
    SetTimer(hwnd, kAutoSaveTimerId, kAutoSaveDelayMs, nullptr);
}
