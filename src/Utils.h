#pragma once
#include "AppState.h"
#include <windows.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cwctype>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// g_dpi is owned by SeatingChartApp and passed to Scale() via the free-function
// wrappers below — declared extern so Utils.h can be included everywhere.
extern UINT g_dpi;

// ---------------------------------------------------------------------------
// DPI-aware sizing
// ---------------------------------------------------------------------------

inline int Scale(int v)          { return MulDiv(v, static_cast<int>(g_dpi), 96); }
inline int PanelWidth()          { return Scale(340); }
inline int Margin()              { return Scale(16); }
inline int Gap()                 { return Scale(10); }
inline int SeatWidth()           { return Scale(110); }
inline int SeatHeight()          { return Scale(64); }
inline int SeatRadius()          { return Scale(10); }
inline int HeaderHeight()        { return Scale(52); }
inline int MinChartWidth()       { return Scale(320); }
inline int MinChartHeight()      { return Scale(240); }
inline int MinWindowWidth()      { return MinChartWidth() + PanelWidth() + Margin() * 3; }
inline int MinWindowHeight()     { return Scale(440); }

struct WindowUiMetrics {
    int lineH = Scale(18);
    int labelH = Scale(22);
    int buttonH = Scale(30);
    int editH = Scale(24);
    int gap = Scale(8);
    int pad = Scale(16);
    int sectionGap = Scale(16);
};

[[nodiscard]] inline WindowUiMetrics ComputeWindowUiMetrics(HWND hwnd, HFONT font = nullptr) {
    WindowUiMetrics m{};
    HDC dc = hwnd ? GetDC(hwnd) : nullptr;
    if (!dc) return m;

    HFONT useFont = font;
    if (!useFont && hwnd)
        useFont = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = nullptr;
    if (useFont) oldFont = SelectObject(dc, useFont);

    TEXTMETRICW tm{};
    if (GetTextMetricsW(dc, &tm)) {
        const int line = std::max(1, static_cast<int>(tm.tmHeight + tm.tmExternalLeading));
        m.lineH = line;
        m.labelH = line + std::max(Scale(2), line / 6);
        m.buttonH = std::max(line + Scale(12), Scale(28));
        m.editH = std::max(line + Scale(8), Scale(24));
        m.gap = std::max(Scale(4), line / 2);
        m.pad = std::max(Scale(10), line);
        m.sectionGap = std::max(Scale(12), line);
    }

    if (oldFont) SelectObject(dc, oldFont);
    ReleaseDC(hwnd, dc);
    return m;
}

// ---------------------------------------------------------------------------
// RECT helpers
// ---------------------------------------------------------------------------

[[nodiscard]] inline RECT NormalizeRect(RECT rc) {
    if (rc.left > rc.right)  std::swap(rc.left,  rc.right);
    if (rc.top  > rc.bottom) std::swap(rc.top,   rc.bottom);
    return rc;
}

[[nodiscard]] inline bool PtInRectEx(const RECT& rc, POINT pt) {
    return pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom;
}

[[nodiscard]] inline RECT OffsetRectCopy(RECT rc, int dx, int dy) {
    rc.left += dx; rc.right  += dx;
    rc.top  += dy; rc.bottom += dy;
    return rc;
}

[[nodiscard]] inline RECT InflateRectCopy(RECT rc, int dx, int dy) {
    InflateRect(&rc, dx, dy); return rc;
}

[[nodiscard]] inline RECT UnionRectCopy(const RECT& a, const RECT& b) {
    RECT out{}; UnionRect(&out, &a, &b); return out;
}

[[nodiscard]] inline RECT SnapRectToGrid(RECT rc, int grid = 8) {
    auto snap = [grid](int v) { return (v / grid) * grid; };
    rc.left = snap(rc.left); rc.top    = snap(rc.top);
    rc.right= snap(rc.right);rc.bottom = snap(rc.bottom);
    return rc;
}

// ---------------------------------------------------------------------------
// String helpers  (wstring_view params — zero-copy at call sites)
// ---------------------------------------------------------------------------

[[nodiscard]] inline std::wstring TrimCopy(std::wstring_view sv) {
    const auto b = sv.find_first_not_of(L" \t\r\n");
    const auto e = sv.find_last_not_of(L" \t\r\n");
    return b == std::wstring_view::npos ? std::wstring{} : std::wstring(sv.substr(b, e - b + 1));
}

[[nodiscard]] inline std::wstring CanonicalName(std::wstring_view sv) {
    auto value = TrimCopy(sv);
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    std::wstring compact;
    bool space = false;
    for (wchar_t c : value) {
        if (iswspace(c)) { space = !compact.empty(); continue; }
        if (space)       { compact += L' '; space = false; }
        compact += c;
    }
    return compact;
}

[[nodiscard]] inline std::pair<std::wstring, std::wstring> SplitDisplayName(std::wstring_view sv) {
    auto value = TrimCopy(sv);
    if (value.empty()) return { {}, {} };
    const size_t pos = value.find(L' ');
    if (pos == std::wstring::npos) return { std::move(value), {} };
    return { std::wstring(value.substr(0, pos)), std::wstring(value.substr(pos + 1)) };
}

[[nodiscard]] inline std::wstring DisplayStudentName(std::wstring_view sv, bool showLastNames) {
    auto value = TrimCopy(sv);
    if (showLastNames) return value;
    return SplitDisplayName(value).first;
}

[[nodiscard]] inline std::vector<std::wstring> SplitRosterInput(std::wstring_view text,
    std::unordered_map<std::wstring, std::vector<std::wstring>>* outTags = nullptr) {
    std::vector<std::wstring> items;
    std::wstring cur;
    for (wchar_t c : text) {
        if (c == L'\r' || c == L'\n' || c == L',' || c == L';' || c == L'\t') {
            auto t = TrimCopy(cur);
            if (!t.empty()) {
                // Support richer syntax: "Name [tag1, tag2]" or "Name[tag1|tag2]"
                std::vector<std::wstring> tags;
                auto bracket = t.find(L'[');
                if (bracket != std::wstring::npos) {
                    auto namePart = TrimCopy(t.substr(0, bracket));
                    auto tagPart = t.substr(bracket + 1);
                    auto endb = tagPart.find(L']');
                    if (endb != std::wstring::npos) tagPart = tagPart.substr(0, endb);
                    // split tags on , | or space
                    std::wstring tagCur;
                    for (wchar_t tc : tagPart) {
                        if (tc == L',' || tc == L'|' || tc == L' ' || tc == L'\t') {
                            auto tg = TrimCopy(tagCur);
                            if (!tg.empty()) tags.push_back(std::move(tg));
                            tagCur.clear();
                        } else tagCur += tc;
                    }
                    auto tg = TrimCopy(tagCur);
                    if (!tg.empty()) tags.push_back(std::move(tg));
                    t = namePart.empty() ? t : namePart;  // use cleaned name
                }
                if (outTags && !tags.empty()) {
                    auto cn = CanonicalName(t);
                    if (!cn.empty()) (*outTags)[cn] = std::move(tags);
                }
                items.push_back(std::move(t));
            }
            cur.clear();
        } else { cur += c; }
    }
    auto t = TrimCopy(cur);
    if (!t.empty()) {
        // same bracket logic for last
        std::vector<std::wstring> tags;
        auto bracket = t.find(L'[');
        if (bracket != std::wstring::npos) {
            auto namePart = TrimCopy(t.substr(0, bracket));
            auto tagPart = t.substr(bracket + 1);
            auto endb = tagPart.find(L']');
            if (endb != std::wstring::npos) tagPart = tagPart.substr(0, endb);
            std::wstring tagCur;
            for (wchar_t tc : tagPart) {
                if (tc == L',' || tc == L'|' || tc == L' ' || tc == L'\t') {
                    auto tg = TrimCopy(tagCur);
                    if (!tg.empty()) tags.push_back(std::move(tg));
                    tagCur.clear();
                } else tagCur += tc;
            }
            auto tg = TrimCopy(tagCur);
            if (!tg.empty()) tags.push_back(std::move(tg));
            t = namePart.empty() ? t : namePart;
        }
        if (outTags && !tags.empty()) {
            auto cn = CanonicalName(t);
            if (!cn.empty()) (*outTags)[cn] = std::move(tags);
        }
        items.push_back(std::move(t));
    }
    return items;
}

[[nodiscard]] inline Restriction NormalizeRestriction(Restriction r) {
    r.first  = TrimCopy(r.first);
    r.second = TrimCopy(r.second);
    if (CanonicalName(r.second) < CanonicalName(r.first)) std::swap(r.first, r.second);
    if (r.weight < 1) r.weight = 1;
    if (r.weight > 10) r.weight = 10;
    return r;
}

[[nodiscard]] inline bool RestrictionEquals(const Restriction& a, const Restriction& b) {
    return CanonicalName(a.first)  == CanonicalName(b.first) &&
           CanonicalName(a.second) == CanonicalName(b.second);
}

[[nodiscard]] inline std::vector<Restriction> SplitRestrictionInput(std::wstring_view text) {
    std::vector<Restriction> rules;
    std::wstring cur;
    auto flush = [&]() {
        auto line = TrimCopy(cur); cur.clear();
        if (line.empty()) return;
        // Optional trailing "@<units>" keep-apart radius, e.g. "Alice | Bob @150".
        int radius = 0;
        const auto at = line.rfind(L'@');
        if (at != std::wstring::npos) {
            const std::wstring num = TrimCopy(line.substr(at + 1));
            if (!num.empty() && std::all_of(num.begin(), num.end(),
                    [](wchar_t ch){ return iswdigit(static_cast<wint_t>(ch)) != 0; })) {
                radius = static_cast<int>(std::clamp<long>(wcstol(num.c_str(), nullptr, 10), 0, 100000));
                line   = TrimCopy(line.substr(0, at));
            }
        }
        for (const std::wstring& sep : { std::wstring(L"!="), std::wstring(L"|"), std::wstring(L","), std::wstring(L"<>") }) {
            auto pos = line.find(sep);
            if (pos != std::wstring::npos) {
                auto l = TrimCopy(line.substr(0, pos));
                auto r = TrimCopy(line.substr(pos + sep.size()));
                if (!l.empty() && !r.empty()) {
                    Restriction rule{l, r};
                    rule.radius = radius;
                    rules.push_back(NormalizeRestriction(std::move(rule)));
                }
                return;
            }
        }
    };
    for (wchar_t c : text) { if (c == L'\r' || c == L'\n') flush(); else cur += c; }
    flush();
    std::vector<Restriction> dedup;
    for (const auto& rule : rules) {
        const bool exists = std::any_of(dedup.begin(), dedup.end(),
            [&](const Restriction& x) { return RestrictionEquals(x, rule); });
        if (!exists && !CanonicalName(rule.first).empty() &&
            !CanonicalName(rule.second).empty() &&
            CanonicalName(rule.first) != CanonicalName(rule.second))
            dedup.push_back(rule);
    }
    return dedup;
}

// Parse group/cluster lines for affinity, e.g. "Group: Alice Bob Charlie" or "Cluster A B C".
// Returns list of groups (each a vector of names).
[[nodiscard]] inline std::vector<std::vector<std::wstring>> SplitGroupInput(std::wstring_view text) {
    std::vector<std::vector<std::wstring>> groups;
    std::wstring cur;
    auto flush = [&]() {
        auto line = TrimCopy(cur); cur.clear();
        if (line.empty()) return;
        // strip "Group:" or "Cluster:" prefix
        for (const auto& pref : {std::wstring(L"Group:"), std::wstring(L"Cluster:"), std::wstring(L"group:"), std::wstring(L"cluster:")}) {
            if (line.size() > pref.size() && line.substr(0, pref.size()) == pref) {
                line = TrimCopy(line.substr(pref.size()));
                break;
            }
        }
        if (line.empty()) return;
        std::vector<std::wstring> grp;
        std::wstring name;
        for (wchar_t c : line) {
            if (c == L' ' || c == L'\t' || c == L',' || c == L';') {
                auto t = TrimCopy(name);
                if (!t.empty()) grp.push_back(std::move(t));
                name.clear();
            } else {
                name += c;
            }
        }
        auto t = TrimCopy(name);
        if (!t.empty()) grp.push_back(std::move(t));
        if (grp.size() >= 2) groups.push_back(std::move(grp));
    };
    for (wchar_t c : text) { if (c == L'\r' || c == L'\n') flush(); else cur += c; }
    flush();
    return groups;
}

// Parse hard "must sit together" lines. Separators "==" or "=" or "must".
// e.g. "Alice == Bob" or "Alice = Bob". Returns deduped, normalized list (no radius).
[[nodiscard]] inline std::vector<Restriction> SplitTogetherInput(std::wstring_view text) {
    std::vector<Restriction> rules;
    std::wstring cur;
    auto flush = [&]() {
        auto line = TrimCopy(cur); cur.clear();
        if (line.empty()) return;
        for (const std::wstring& sep : { std::wstring(L"=="), std::wstring(L" = "), std::wstring(L"="), std::wstring(L" must "), std::wstring(L"++") }) {
            auto pos = line.find(sep);
            if (pos != std::wstring::npos) {
                auto l = TrimCopy(line.substr(0, pos));
                auto r = TrimCopy(line.substr(pos + sep.size()));
                if (!l.empty() && !r.empty())
                    rules.push_back(NormalizeRestriction({l, r}));
                return;
            }
        }
    };
    for (wchar_t c : text) { if (c == L'\r' || c == L'\n') flush(); else cur += c; }
    flush();
    std::vector<Restriction> dedup;
    for (const auto& rule : rules) {
        const bool exists = std::any_of(dedup.begin(), dedup.end(),
            [&](const Restriction& x) { return RestrictionEquals(x, rule); });
        if (!exists && !CanonicalName(rule.first).empty() &&
            !CanonicalName(rule.second).empty() &&
            CanonicalName(rule.first) != CanonicalName(rule.second))
            dedup.push_back(rule);
    }
    return dedup;
}

// Parse "sit near" affinity lines (separators '+' or '&'), e.g. "Alice + Bob @5".
// Soft preferences. Optional @N for weight 1-10. Returns deduped, canonically-ordered list.
[[nodiscard]] inline std::vector<Restriction> SplitAffinityInput(std::wstring_view text) {
    std::vector<Restriction> rules;
    std::wstring cur;
    auto flush = [&]() {
        auto line = TrimCopy(cur); cur.clear();
        if (line.empty()) return;
        int weight = 1;
        const auto at = line.rfind(L'@');
        if (at != std::wstring::npos) {
            const std::wstring num = TrimCopy(line.substr(at + 1));
            if (!num.empty() && std::all_of(num.begin(), num.end(),
                    [](wchar_t ch){ return iswdigit(static_cast<wint_t>(ch)) != 0; })) {
                weight = static_cast<int>(std::clamp<long>(wcstol(num.c_str(), nullptr, 10), 1, 10));
                line   = TrimCopy(line.substr(0, at));
            }
        }
        for (const std::wstring& sep : { std::wstring(L"+"), std::wstring(L"&") }) {
            auto pos = line.find(sep);
            if (pos != std::wstring::npos) {
                auto l = TrimCopy(line.substr(0, pos));
                auto r = TrimCopy(line.substr(pos + sep.size()));
                if (!l.empty() && !r.empty()) {
                    Restriction rule{l, r};
                    rule.weight = weight;
                    rules.push_back(NormalizeRestriction(std::move(rule)));
                }
                return;
            }
        }
    };
    for (wchar_t c : text) { if (c == L'\r' || c == L'\n') flush(); else cur += c; }
    flush();
    std::vector<Restriction> dedup;
    for (const auto& rule : rules) {
        const bool exists = std::any_of(dedup.begin(), dedup.end(),
            [&](const Restriction& x) { return RestrictionEquals(x, rule); });
        if (!exists && !CanonicalName(rule.first).empty() &&
            !CanonicalName(rule.second).empty() &&
            CanonicalName(rule.first) != CanonicalName(rule.second))
            dedup.push_back(rule);
    }
    return dedup;
}

[[nodiscard]] inline bool FindDuplicateCanonicalName(const std::vector<std::wstring>& roster,
                                                      std::wstring* out = nullptr) {
    std::unordered_set<std::wstring> seen;
    for (const auto& n : roster) {
        auto cn = CanonicalName(n);
        if (cn.empty()) continue;
        if (!seen.insert(cn).second) { if (out) *out = n; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Win32 string helpers
// ---------------------------------------------------------------------------

[[nodiscard]] inline std::wstring GetWindowTextStr(HWND hwnd) {
    if (!hwnd) return {};
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return {};
    std::wstring text(static_cast<size_t>(len + 1), L'\0');
    const int copied = GetWindowTextW(hwnd, text.data(), len + 1);
    text.resize(static_cast<size_t>(std::max(0, copied)));
    return text;
}

[[nodiscard]] inline bool ParseIntStrict(std::wstring_view sv, int lo, int hi, int* out) {
    if (!out) return false;
    auto val = TrimCopy(sv);
    if (val.empty()) return false;
    wchar_t* end = nullptr; errno = 0;
    const long parsed = wcstol(val.c_str(), &end, 10);
    if (errno == ERANGE || end == val.c_str() || *end != L'\0' ||
        parsed < lo || parsed > hi) return false;
    *out = static_cast<int>(parsed);
    return true;
}

[[nodiscard]] inline bool ParsePrefixedInt(std::wstring_view line, std::wstring_view prefix,
                                            int lo, int hi, int* out) {
    if (line.substr(0, prefix.size()) != prefix) return false;
    return ParseIntStrict(line.substr(prefix.size()), lo, hi, out);
}

// ---------------------------------------------------------------------------
// Layout helpers
// ---------------------------------------------------------------------------

[[nodiscard]] inline std::wstring_view LayoutTypeName(LayoutItemType t) {
    switch (t) {
    case LayoutItemType::Smartboard:    return L"Smartboard";
    case LayoutItemType::TrapezoidDesk: return L"TrapezoidDesk";
    case LayoutItemType::RectangleDesk: return L"RectangleDesk";
    case LayoutItemType::Table4:        return L"Table4";
    case LayoutItemType::BigTable:      return L"BigTable";
    case LayoutItemType::TrapPair:      return L"TrapPair";
    case LayoutItemType::TrapPod:       return L"TrapPod";
    case LayoutItemType::LDesk:         return L"LDesk";
    case LayoutItemType::UDesk:         return L"UDesk";
    }
    return L"RectangleDesk";
}

[[nodiscard]] inline LayoutItemType LayoutTypeFromName(std::wstring_view name) {
    if (name == L"Smartboard")    return LayoutItemType::Smartboard;
    if (name == L"TrapezoidDesk") return LayoutItemType::TrapezoidDesk;
    if (name == L"Table4")        return LayoutItemType::Table4;
    if (name == L"BigTable")      return LayoutItemType::BigTable;
    if (name == L"TrapPair")      return LayoutItemType::TrapPair;
    if (name == L"TrapPod")       return LayoutItemType::TrapPod;
    if (name == L"LDesk")         return LayoutItemType::LDesk;
    if (name == L"UDesk")         return LayoutItemType::UDesk;
    return LayoutItemType::RectangleDesk;
}

// Default seat capacity for a given item type (0 = not a seating item).
[[nodiscard]] inline int LayoutItemDefaultCapacity(LayoutItemType t) {
    switch (t) {
    case LayoutItemType::TrapezoidDesk:
    case LayoutItemType::RectangleDesk: return 1;
    case LayoutItemType::Table4:        return 4;
    case LayoutItemType::BigTable:      return 6;
    case LayoutItemType::TrapPair:      return 4;   // 2 trapezoid desks
    case LayoutItemType::TrapPod:       return 4;   // 4 trapezoid desks (composite pod)
    case LayoutItemType::LDesk:         return 2;
    case LayoutItemType::UDesk:         return 3;
    default:                            return 0;
    }
}

// Effective seat count for an item (respects per-item capacity override).
[[nodiscard]] inline int LayoutItemSeats(const LayoutItem& item) {
    const int def = LayoutItemDefaultCapacity(item.type);
    if (item.type == LayoutItemType::BigTable)
        return item.capacity > 0 ? item.capacity : def;
    return def;
}

// Keep item.occupants sized to the seat count (preserving existing names).
inline void EnsureSeatSlots(LayoutItem& item) {
    const size_t n = static_cast<size_t>(std::max(0, LayoutItemSeats(item)));
    if (item.occupants.size() != n) item.occupants.resize(n);
}

// Flatten every furniture seat into one ordered list of (item, slot) refs.
// This is the canonical seat ordering used by roster fill and auto-assign.
[[nodiscard]] inline std::vector<LayoutSeatRef>
EnumerateLayoutSeats(const std::vector<LayoutItem>& items) {
    std::vector<LayoutSeatRef> out;
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
        for (int s = 0; s < static_cast<int>(items[static_cast<size_t>(i)].occupants.size()); ++s)
            out.push_back({ i, s });
    return out;
}

// Total seat slots across all furniture.
[[nodiscard]] inline int TotalLayoutSeats(const std::vector<LayoutItem>& items) {
    int n = 0;
    for (const auto& it : items) n += static_cast<int>(it.occupants.size());
    return n;
}

// Room-local (unrotated) seat-slot rectangles for an item, in a stable order
// that matches item.occupants[]. Empty for non-seating items (e.g. Smartboard).
// Callers rotate slot centres by item.rotation for rendering / hit-testing.
[[nodiscard]] inline std::vector<RECT> LayoutSeatSlots(const LayoutItem& item) {
    std::vector<RECT> slots;
    const int n = LayoutItemSeats(item);
    if (n <= 0) return slots;
    const RECT& b = item.bounds;
    const int W = static_cast<int>(b.right - b.left);
    const int H = static_cast<int>(b.bottom - b.top);

    switch (item.type) {
    case LayoutItemType::RectangleDesk:
    case LayoutItemType::TrapezoidDesk:
        slots.push_back(b);                       // single seat = whole footprint
        break;

    case LayoutItemType::Table4: {
        const int mx = (b.left + b.right) / 2, my = (b.top + b.bottom) / 2;
        slots.push_back({ b.left, b.top,  mx,      my       });
        slots.push_back({ mx,     b.top,  b.right, my       });
        slots.push_back({ b.left, my,     mx,      b.bottom });
        slots.push_back({ mx,     my,     b.right, b.bottom });
        break;
    }

    case LayoutItemType::BigTable: {
        // Distribute seats around the perimeter using the same counts as the
        // rendered seat bumps, so occupant initials sit on their seats.
        const int perim  = std::max(1, 2 * (W + H));
        const int top     = std::max(0, (n * W) / perim);
        const int bottom  = top;
        const int side    = std::max(0, (n - top - bottom) / 2);
        const int left    = side;
        const int right   = n - top - bottom - left;
        const int r = std::clamp(std::min(W, H) / 5, 10, 40); // slot half-extent
        auto edge = [&](int count, bool horiz, int fixed, int start, int span, bool inwardNeg) {
            if (count <= 0) return;
            const int step = span / (count + 1);
            for (int k = 1; k <= count; ++k) {
                const int along = start + k * step;
                const int nudged = fixed + (inwardNeg ? r : -r); // pull inside the table
                const int cx = horiz ? along  : nudged;
                const int cy = horiz ? nudged : along;
                slots.push_back({ cx - r, cy - r, cx + r, cy + r });
            }
        };
        edge(top,    true,  b.top,    b.left, W, true);
        edge(bottom, true,  b.bottom, b.left, W, false);
        edge(left,   false, b.left,   b.top,  H, true);
        edge(right,  false, b.right,  b.top,  H, false);
        break;
    }

    // Desk-shaped (rectangular) seat slots, centred on each desk region, so a
    // full name can be drawn in the middle of the shape. Fractions of bounds.
    case LayoutItemType::TrapPair: {
        auto slot = [&](double cxF, double cyF, double wF, double hF) {
            const int sx = b.left + static_cast<int>(std::lround(cxF * W));
            const int sy = b.top  + static_cast<int>(std::lround(cyF * H));
            const int hw = static_cast<int>(std::lround(wF * W / 2));
            const int hh = static_cast<int>(std::lround(hF * H / 2));
            slots.push_back({ sx - hw, sy - hh, sx + hw, sy + hh });
        };
        slot(0.33, 0.27, 0.30, 0.26); slot(0.67, 0.27, 0.30, 0.26); // top desk (2)
        slot(0.33, 0.73, 0.30, 0.26); slot(0.67, 0.73, 0.30, 0.26); // bottom desk (2)
        break;
    }

    case LayoutItemType::TrapPod: {
        auto slot = [&](double cxF, double cyF, double wF, double hF) {
            const int sx = b.left + static_cast<int>(std::lround(cxF * W));
            const int sy = b.top  + static_cast<int>(std::lround(cyF * H));
            const int hw = static_cast<int>(std::lround(wF * W / 2));
            const int hh = static_cast<int>(std::lround(hF * H / 2));
            slots.push_back({ sx - hw, sy - hh, sx + hw, sy + hh });
        };
        slot(0.50, 0.28, 0.30, 0.22); // top middle desk
        slot(0.50, 0.72, 0.30, 0.22); // bottom middle desk
        slot(0.16, 0.50, 0.20, 0.30); // left side desk
        slot(0.84, 0.50, 0.20, 0.30); // right side desk
        break;
    }

    default:
        break;
    }
    return slots;
}

// ---------------------------------------------------------------------------
// Quoted string encoding (state file)
// ---------------------------------------------------------------------------

[[nodiscard]] inline std::wstring EscapeQuoted(std::wstring_view sv) {
    std::wstring out = L"\"";
    for (wchar_t c : sv) {
        switch (c) {
        case L'\\': out += L"\\\\"; break;
        case L'"':  out += L"\\\""; break;
        case L'\n': out += L"\\n";  break;
        case L'\r': out += L"\\r";  break;
        case L'\t': out += L"\\t";  break;
        default:    out += c;       break;
        }
    }
    return out + L"\"";
}

[[nodiscard]] inline bool ParseQuotedValue(std::wstring_view line, std::wstring_view prefix,
                                            std::wstring* out) {
    if (line.substr(0, prefix.size()) != prefix) return false;
    size_t pos = prefix.size();
    if (pos >= line.size() || line[pos] != L' ') return false; ++pos;
    if (pos >= line.size() || line[pos] != L'"') return false; ++pos;
    std::wstring res; bool esc = false;
    for (; pos < line.size(); ++pos) {
        wchar_t c = line[pos];
        if (esc) {
            switch (c) {
            case L'\\': res += L'\\'; break; case L'"':  res += L'"';  break;
            case L'n':  res += L'\n'; break; case L'r':  res += L'\r'; break;
            case L't':  res += L'\t'; break; default:    res += c;     break;
            }
            esc = false; continue;
        }
        if (c == L'\\') { esc = true; continue; }
        if (c == L'"') {
            // Reject trailing non-whitespace after closing quote.
            for (size_t trail = pos + 1; trail < line.size(); ++trail)
                if (!iswspace(static_cast<wint_t>(line[trail]))) return false;
            *out = std::move(res);
            return true;
        }
        res += c;
    }
    return false;
}

[[nodiscard]] inline bool ParseQuotedPair(std::wstring_view line, std::wstring_view prefix,
                                           std::wstring* first, std::wstring* second) {
    if (line.substr(0, prefix.size()) != prefix) return false;
    size_t pos = prefix.size();
    if (pos >= line.size() || line[pos] != L' ') return false; ++pos;
    auto pq = [&](std::wstring* out) -> bool {
        while (pos < line.size() && line[pos] == L' ') ++pos;
        if (pos >= line.size() || line[pos] != L'"') return false; ++pos;
        std::wstring res; bool esc = false;
        for (; pos < line.size(); ++pos) {
            wchar_t c = line[pos];
            if (esc) {
                switch (c) {
                case L'\\': res += L'\\'; break; case L'"':  res += L'"';  break;
                case L'n':  res += L'\n'; break; case L'r':  res += L'\r'; break;
                case L't':  res += L'\t'; break; default:    res += c;     break;
                }
                esc = false; continue;
            }
            if (c == L'\\') { esc = true; continue; }
            if (c == L'"')  { *out = std::move(res); ++pos; return true; }
            res += c;
        }
        return false;
    };
    std::wstring l, r;
    if (!pq(&l) || !pq(&r)) return false;
    // Reject trailing non-whitespace after the second closing quote.
    while (pos < line.size() && iswspace(static_cast<wint_t>(line[pos]))) ++pos;
    if (pos < line.size()) return false;
    *first = std::move(l); *second = std::move(r);
    return true;
}

[[nodiscard]] inline std::wstring RectToWString(const RECT& rc) {
    return std::to_wstring(rc.left)  + L',' + std::to_wstring(rc.top) + L',' +
           std::to_wstring(rc.right) + L',' + std::to_wstring(rc.bottom);
}

[[nodiscard]] inline bool WStringToRect(std::wstring_view sv, RECT* rc) {
    std::wstringstream ss{ std::wstring(sv) };
    wchar_t comma = 0;
    if (!(ss >> rc->left   >> comma) || comma != L',') return false;
    if (!(ss >> rc->top    >> comma) || comma != L',') return false;
    if (!(ss >> rc->right  >> comma) || comma != L',') return false;
    if (!(ss >> rc->bottom))                           return false;
    // Reject trailing non-whitespace after the four integers.
    wchar_t trail{};
    if (ss >> trail) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Layout-mode room / viewport coordinate system
// ---------------------------------------------------------------------------

// Default logical room size when roomW/roomH == 0.
constexpr int kDefaultRoomW = 1200;
constexpr int kDefaultRoomH = 800;

// Snap grid size used for layout items (room-local units).
constexpr int kRoomSnapGrid = 10;

// Coarse cell size for the block authoring grid (room units). A single block is
// one cell square; merging blocks unions their (cell-aligned) bounds.
constexpr int kBlockGridCell = 100;

// Effective room dimensions (substitutes defaults when 0).
[[nodiscard]] inline int EffectiveRoomW(int roomW) {
    return roomW > 0 ? roomW : kDefaultRoomW;
}
[[nodiscard]] inline int EffectiveRoomH(int roomH) {
    return roomH > 0 ? roomH : kDefaultRoomH;
}

// ---------------------------------------------------------------------------
// Room "front" edge — naming + seat ranking
// ---------------------------------------------------------------------------

[[nodiscard]] inline std::wstring_view RoomEdgeName(RoomEdge e) {
    switch (e) {
    case RoomEdge::Top:    return L"Top";
    case RoomEdge::Bottom: return L"Bottom";
    case RoomEdge::Left:   return L"Left";
    case RoomEdge::Right:  return L"Right";
    }
    return L"Top";
}

[[nodiscard]] inline RoomEdge RoomEdgeFromName(std::wstring_view n) {
    if (n == L"Bottom") return RoomEdge::Bottom;
    if (n == L"Left")   return RoomEdge::Left;
    if (n == L"Right")  return RoomEdge::Right;
    return RoomEdge::Top;
}

// "How far back" a seat centre sits from the front edge, in room-local units.
// 0 = on the front edge; larger = further from the front. Front-row rules rank
// seats by this (smaller = more front).
[[nodiscard]] inline int SeatFrontDistance(POINT centerRoom, RoomEdge front,
                                           int roomW, int roomH) {
    const int rw = EffectiveRoomW(roomW), rh = EffectiveRoomH(roomH);
    switch (front) {
    case RoomEdge::Top:    return centerRoom.y;
    case RoomEdge::Bottom: return rh - centerRoom.y;
    case RoomEdge::Left:   return centerRoom.x;
    case RoomEdge::Right:  return rw - centerRoom.x;
    }
    return centerRoom.y;
}

// Transform that maps room-local coordinates ↔ screen coordinates.
// Room origin (0,0) maps to roomScreenRect.{left,top}.
struct LayoutViewTransform {
    RECT   chartBounds;    // screen rect the entire chart occupies
    RECT   roomScreenRect; // screen rect where the room boundary is drawn
    int    roomW;          // effective logical room width  (room-local units)
    int    roomH;          // effective logical room height (room-local units)
    double scale;          // room units → screen pixels
};

// Build the transform so the room is centred inside chartBounds with padding.
[[nodiscard]] inline LayoutViewTransform
ComputeLayoutViewTransform(const RECT& chartBounds, int roomW, int roomH) {
    const int rw  = EffectiveRoomW(roomW);
    const int rh  = EffectiveRoomH(roomH);
    const int cw  = std::max(1, static_cast<int>(chartBounds.right  - chartBounds.left));
    const int ch  = std::max(1, static_cast<int>(chartBounds.bottom - chartBounds.top));
    const int pad = 20; // screen-pixel padding around room inside chart area
    const double sx    = (cw - pad * 2) / static_cast<double>(rw);
    const double sy    = (ch - pad * 2) / static_cast<double>(rh);
    const double scale = std::max(0.01, std::min(sx, sy));
    const int srW = static_cast<int>(rw * scale);
    const int srH = static_cast<int>(rh * scale);
    const int rl  = chartBounds.left + (cw - srW) / 2;
    const int rt  = chartBounds.top  + (ch - srH) / 2;
    RECT roomSR{ rl, rt, rl + srW, rt + srH };
    return { chartBounds, roomSR, rw, rh, scale };
}

// Room-local RECT → screen RECT.
[[nodiscard]] inline RECT RoomToScreenRect(const RECT& r, const LayoutViewTransform& tx) {
    return {
        tx.roomScreenRect.left + static_cast<LONG>(r.left  * tx.scale),
        tx.roomScreenRect.top  + static_cast<LONG>(r.top   * tx.scale),
        tx.roomScreenRect.left + static_cast<LONG>(r.right  * tx.scale),
        tx.roomScreenRect.top  + static_cast<LONG>(r.bottom * tx.scale)
    };
}

// Screen RECT → room-local RECT.
[[nodiscard]] inline RECT ScreenToRoomRect(const RECT& r, const LayoutViewTransform& tx) {
    if (tx.scale < 0.001) return {};
    return {
        static_cast<LONG>((r.left   - tx.roomScreenRect.left) / tx.scale),
        static_cast<LONG>((r.top    - tx.roomScreenRect.top)  / tx.scale),
        static_cast<LONG>((r.right  - tx.roomScreenRect.left) / tx.scale),
        static_cast<LONG>((r.bottom - tx.roomScreenRect.top)  / tx.scale)
    };
}

// Screen POINT → room-local POINT.
[[nodiscard]] inline POINT ScreenToRoomPoint(POINT pt, const LayoutViewTransform& tx) {
    if (tx.scale < 0.001) return {};
    return {
        static_cast<LONG>((pt.x - tx.roomScreenRect.left) / tx.scale),
        static_cast<LONG>((pt.y - tx.roomScreenRect.top)  / tx.scale)
    };
}

// Room-local POINT → screen POINT.
[[nodiscard]] inline POINT RoomToScreenPoint(POINT pt, const LayoutViewTransform& tx) {
    return {
        tx.roomScreenRect.left + static_cast<LONG>(pt.x * tx.scale),
        tx.roomScreenRect.top  + static_cast<LONG>(pt.y * tx.scale)
    };
}

// ---------------------------------------------------------------------------
// Rotation helpers (PowerPoint-style direct manipulation)
// ---------------------------------------------------------------------------

inline constexpr double kPi = 3.14159265358979323846;

// Screen distance (logical px before DPI scaling) of the rotation handle above
// the item's top-centre.
inline constexpr int kRotateHandleScreenDist = 24;

// Rotate POINT p about pivot c by deg degrees. Positive deg = clockwise on a
// y-down screen (matches the GDI world transform used for rendering).
[[nodiscard]] inline POINT RotatePointAround(POINT p, POINT c, double deg) {
    if (deg == 0.0) return p;
    const double rad = deg * kPi / 180.0;
    const double cs = std::cos(rad), sn = std::sin(rad);
    const double dx = static_cast<double>(p.x - c.x);
    const double dy = static_cast<double>(p.y - c.y);
    return {
        c.x + static_cast<LONG>(std::lround(dx * cs - dy * sn)),
        c.y + static_cast<LONG>(std::lround(dx * sn + dy * cs))
    };
}

// Screen-space geometry of a single item's selection handles. corners[] order
// matches ResizeHandle: 0=TopLeft, 1=TopRight, 2=BottomLeft, 3=BottomRight.
struct LayoutHandleGeometry {
    POINT center{};
    POINT corners[4]{};
    POINT topCenter{};   // rotated mid-point of the top edge
    POINT rotate{};      // rotation handle position (above topCenter)
};

[[nodiscard]] inline LayoutHandleGeometry
ComputeLayoutHandleGeometry(const RECT& roomBounds, int rotationDeg,
                            const LayoutViewTransform& tx) {
    const RECT  sr = RoomToScreenRect(roomBounds, tx);
    const POINT c{ (sr.left + sr.right) / 2, (sr.top + sr.bottom) / 2 };
    const int   dist = std::max(16, Scale(kRotateHandleScreenDist));
    const double deg = static_cast<double>(rotationDeg);
    LayoutHandleGeometry g{};
    g.center     = c;
    g.corners[0] = RotatePointAround({ sr.left,  sr.top    }, c, deg);
    g.corners[1] = RotatePointAround({ sr.right, sr.top    }, c, deg);
    g.corners[2] = RotatePointAround({ sr.left,  sr.bottom }, c, deg);
    g.corners[3] = RotatePointAround({ sr.right, sr.bottom }, c, deg);
    g.topCenter  = RotatePointAround({ c.x,      sr.top        }, c, deg);
    g.rotate     = RotatePointAround({ c.x,      sr.top - dist }, c, deg);
    return g;
}

// Screen-space centre of each seat slot, rotation-aware and parallel to
// item.occupants[]. Used to render occupant initials on furniture seats.
[[nodiscard]] inline std::vector<POINT>
LayoutSeatSlotScreenCenters(const LayoutItem& item, const LayoutViewTransform& tx) {
    std::vector<POINT> out;
    const auto slots = LayoutSeatSlots(item);
    if (slots.empty()) return out;
    const RECT  sr = RoomToScreenRect(item.bounds, tx);
    const POINT c{ (sr.left + sr.right) / 2, (sr.top + sr.bottom) / 2 };
    out.reserve(slots.size());
    for (const RECT& s : slots) {
        const RECT  ss = RoomToScreenRect(s, tx);
        const POINT center{ (ss.left + ss.right) / 2, (ss.top + ss.bottom) / 2 };
        out.push_back(RotatePointAround(center, c, static_cast<double>(item.rotation)));
    }
    return out;
}

// Room-local (rotation-aware) centre of each seat slot, parallel to
// item.occupants[]. Like LayoutSeatSlotScreenCenters but in room units (no view
// transform) — used by the solver to measure seat-to-seat keep-apart distance.
[[nodiscard]] inline std::vector<POINT>
LayoutSeatSlotRoomCenters(const LayoutItem& item) {
    std::vector<POINT> out;
    const auto slots = LayoutSeatSlots(item);
    if (slots.empty()) return out;
    const POINT c{ (item.bounds.left + item.bounds.right) / 2,
                   (item.bounds.top  + item.bounds.bottom) / 2 };
    out.reserve(slots.size());
    for (const RECT& s : slots) {
        const POINT centre{ (s.left + s.right) / 2, (s.top + s.bottom) / 2 };
        out.push_back(RotatePointAround(centre, c, static_cast<double>(item.rotation)));
    }
    return out;
}

// Index into LayoutHandleGeometry::corners for a given ResizeHandle (or -1).
[[nodiscard]] inline int ResizeHandleCornerIndex(ResizeHandle h) {
    switch (h) {
    case ResizeHandle::TopLeft:     return 0;
    case ResizeHandle::TopRight:    return 1;
    case ResizeHandle::BottomLeft:  return 2;
    case ResizeHandle::BottomRight: return 3;
    case ResizeHandle::None:        return -1;
    }
    return -1;
}

// Clamp a room-local RECT so it stays within {0, 0, roomW, roomH}.
[[nodiscard]] inline RECT ClampToRoomBounds(RECT rc, int roomW, int roomH,
                                             int minW = 20, int minH = 20) {
    rc = NormalizeRect(rc);
    int w = static_cast<int>(rc.right  - rc.left);
    int h = static_cast<int>(rc.bottom - rc.top);
    w = std::clamp(w, minW, std::max(minW, roomW));
    h = std::clamp(h, minH, std::max(minH, roomH));
    const int l = std::clamp(static_cast<int>(rc.left), 0, std::max(0, roomW - w));
    const int t = std::clamp(static_cast<int>(rc.top),  0, std::max(0, roomH - h));
    return { l, t, l + w, t + h };
}

// Union of the given items' bounds (by index), snapped to the block grid. Used
// to merge several blocks into one labelled region (e.g. a smartboard).
[[nodiscard]] inline RECT MergedBlockBounds(const std::vector<LayoutItem>& items,
                                            const std::vector<int>& indices,
                                            int cell = kBlockGridCell) {
    RECT uni{}; bool first = true;
    for (int i : indices) {
        if (i < 0 || i >= static_cast<int>(items.size())) continue;
        if (first) { uni = items[static_cast<size_t>(i)].bounds; first = false; }
        else        UnionRect(&uni, &uni, &items[static_cast<size_t>(i)].bounds);
    }
    if (first) return RECT{0, 0, 0, 0};
    return SnapRectToGrid(NormalizeRect(uni), cell);
}
