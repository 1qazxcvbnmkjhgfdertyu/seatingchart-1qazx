#include "SeatingChartApp.h"
#include "CrashLog.h"
#include "FileIO.h"
#include "Print.h"
#include "Templates.h"
#include "Utils.h"
#include <algorithm>
#include <commctrl.h>
#include <cmath>
#include <commdlg.h>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <random>
#include <limits>
#include <string>
#include <unordered_set>
#include <windowsx.h>

// g_dpi is used by Scale() / Utils.h helpers.
UINT g_dpi = 96;

// ---------------------------------------------------------------------------
// Internal utilities
// ---------------------------------------------------------------------------

constexpr UINT_PTR kRosterListSubclassId = 1;
constexpr int kSidebarMinWidth = 170;
constexpr int kSidebarMaxWidth = 520;
constexpr int kSidebarSplitterWidth = 6;
constexpr UINT WM_APP_LAYOUT_FLOATING_TOOLS = WM_APP + 20;
constexpr UINT WM_APP_INSPECTOR_SPIN        = WM_APP + 21;

static bool IsCommandActivation(int notif) {
    return notif == 0 || notif == BN_CLICKED;
}

struct BigUInt {
    static constexpr uint32_t kBase = 1000000000u;
    std::vector<uint32_t> digits;

    BigUInt() = default;
    BigUInt(uint64_t value) { Assign(value); }

    void Assign(uint64_t value) {
        digits.clear();
        if (value == 0) return;
        while (value > 0) {
            digits.push_back(static_cast<uint32_t>(value % kBase));
            value /= kBase;
        }
    }

    void Trim() {
        while (!digits.empty() && digits.back() == 0) digits.pop_back();
    }

    [[nodiscard]] bool IsZero() const { return digits.empty(); }

    [[nodiscard]] std::string ToString() const {
        if (digits.empty()) return "0";
        std::string out = std::to_string(digits.back());
        for (size_t i = digits.size(); i-- > 1;) {
            std::string chunk = std::to_string(digits[i - 1]);
            out += std::string(9 - chunk.size(), '0');
            out += chunk;
        }
        return out;
    }

    BigUInt& operator+=(const BigUInt& other) {
        const size_t maxLen = std::max(digits.size(), other.digits.size());
        digits.resize(maxLen, 0);
        uint64_t carry = 0;
        for (size_t i = 0; i < maxLen; ++i) {
            const uint64_t sum = carry + digits[i] +
                (i < other.digits.size() ? other.digits[i] : 0u);
            digits[i] = static_cast<uint32_t>(sum % kBase);
            carry = sum / kBase;
        }
        if (carry != 0) digits.push_back(static_cast<uint32_t>(carry));
        return *this;
    }

    BigUInt& operator*=(uint32_t factor) {
        if (factor == 0 || IsZero()) {
            digits.clear();
            return *this;
        }
        uint64_t carry = 0;
        for (uint32_t& digit : digits) {
            const uint64_t value = (static_cast<uint64_t>(digit) * factor) + carry;
            digit = static_cast<uint32_t>(value % kBase);
            carry = value / kBase;
        }
        while (carry != 0) {
            digits.push_back(static_cast<uint32_t>(carry % kBase));
            carry /= kBase;
        }
        return *this;
    }

    BigUInt& operator/=(uint32_t divisor) {
        uint64_t rem = 0;
        for (size_t i = digits.size(); i-- > 0;) {
            const uint64_t cur = digits[i] + rem * kBase;
            digits[i] = static_cast<uint32_t>(cur / divisor);
            rem = cur % divisor;
        }
        Trim();
        return *this;
    }

    [[nodiscard]] BigUInt Multiply(const BigUInt& other) const {
        if (IsZero() || other.IsZero()) return BigUInt(0);
        BigUInt out;
        out.digits.assign(digits.size() + other.digits.size(), 0);
        for (size_t i = 0; i < digits.size(); ++i) {
            uint64_t carry = 0;
            for (size_t j = 0; j < other.digits.size(); ++j) {
                const uint64_t cur = out.digits[i + j] +
                    (static_cast<uint64_t>(digits[i]) * other.digits[j]) + carry;
                out.digits[i + j] = static_cast<uint32_t>(cur % kBase);
                carry = cur / kBase;
            }
            size_t pos = i + other.digits.size();
            while (carry != 0) {
                const uint64_t cur = out.digits[pos] + carry;
                out.digits[pos] = static_cast<uint32_t>(cur % kBase);
                carry = cur / kBase;
                ++pos;
            }
        }
        out.Trim();
        return out;
    }
};

static long double PermutationCount(int n, int r) {
    if (n < 0 || r < 0 || r > n) return 0;
    long double out = 1.0L;
    for (int i = 0; i < r; ++i)
        out *= static_cast<long double>(n - i);
    return out;
}

static long double BinomialCount(int n, int k) {
    if (n < 0 || k < 0 || k > n) return 0;
    k = std::min(k, n - k);
    long double out = 1.0L;
    for (int i = 1; i <= k; ++i) {
        out *= static_cast<long double>(n - k + i);
        out /= static_cast<long double>(i);
    }
    return out;
}

static BigUInt BinomialCountExact(int n, int k) {
    if (n < 0 || k < 0 || k > n) return 0;
    k = std::min(k, n - k);
    BigUInt out(1);
    for (int i = 1; i <= k; ++i) {
        out *= (n - k + i);
        out /= i;
    }
    return out;
}

static std::wstring FormatPermutationEstimate(long double value) {
    if (!(value > 0.0L)) return L"0";
    if (value < 1000.0L)
        return std::to_wstring(static_cast<unsigned long long>(value + 0.5L));

    if (value < 1.0e15L) {
        unsigned long long n = static_cast<unsigned long long>(value + 0.5L);
        std::wstring ws = std::to_wstring(n);
        for (int i = static_cast<int>(ws.size()) - 3; i > 0; i -= 3)
            ws.insert(static_cast<size_t>(i), 1, L',');
        return ws;
    }

    const int exp10 = static_cast<int>(std::floor(std::log10(value)));
    const long double mantissa = value / std::pow(10.0L, static_cast<long double>(exp10));
    wchar_t buf[64]{};
    swprintf_s(buf, L"%.2Lf × 10^%d", mantissa, exp10);
    return buf;
}

static long double EstimateRuleAwarePermutations(const AppState& state) {
    const int totalSeats = TotalLayoutSeats(state.layoutItems);
    if (state.roster.empty() || totalSeats <= 0) return 0;

    std::unordered_set<std::wstring> fixedRosterStudents;
    for (const auto& item : state.layoutItems) {
        for (const auto& occ : item.occupants) {
            const auto cn = CanonicalName(occ);
            if (!cn.empty()) fixedRosterStudents.insert(cn);
        }
    }

    int fixedCount = 0;
    for (const auto& name : state.roster) {
        if (fixedRosterStudents.count(CanonicalName(name))) ++fixedCount;
    }

    const int freeStudents = std::max(0, static_cast<int>(state.roster.size()) - fixedCount);
    const int openSeats    = std::max(0, totalSeats - fixedCount);
    if (freeStudents == 0) return 0;
    if (freeStudents > openSeats) return 0;

    long double raw = PermutationCount(openSeats, freeStudents);

    // Rule-aware heuristic shrinkage. Exact constrained counting is not practical
    // here, but the estimate should respond monotonically as teachers add rules.
    size_t factor = 1;
    factor += state.restrictions.size();
    factor += state.affinities.size();
    factor += state.mustTogether.size() * 2;
    for (const auto& grp : state.groupAffinities)
        if (grp.size() > 1) factor += (grp.size() - 1) * 2;

    if (factor > 1) raw /= static_cast<long double>(factor);
    return raw;
}

static std::vector<std::vector<std::wstring>> PartitionRosterByPattern(
    const std::vector<std::wstring>& roster, const std::vector<int>& pattern) {
    std::vector<std::vector<std::wstring>> groups;
    groups.reserve(pattern.size());
    size_t pos = 0;
    for (int sz : pattern) {
        std::vector<std::wstring> grp;
        grp.reserve(static_cast<size_t>(std::max(0, sz)));
        for (int j = 0; j < sz && pos < roster.size(); ++j, ++pos)
            grp.push_back(roster[pos]);
        if (!grp.empty()) groups.push_back(std::move(grp));
    }
    return groups;
}

static long double OrderedGroupArrangementCount(int studentCount, const std::vector<int>& pattern) {
    if (studentCount <= 0 || pattern.empty()) return 0;
    int total = 0;
    for (int sz : pattern) total += sz;
    if (total != studentCount) return 0;

    long double out = 1.0L;
    int remaining = studentCount;
    for (int sz : pattern) {
        out *= BinomialCount(remaining, sz);
        remaining -= sz;
    }
    return out;
}

static BigUInt OrderedGroupArrangementCountExact(int studentCount, const std::vector<int>& pattern) {
    if (studentCount <= 0 || pattern.empty()) return 0;
    int total = 0;
    for (int sz : pattern) total += sz;
    if (total != studentCount) return 0;

    BigUInt out(1);
    int remaining = studentCount;
    for (int sz : pattern) {
        const BigUInt choose = BinomialCountExact(remaining, sz);
        out = out.Multiply(choose);
        remaining -= sz;
    }
    return out;
}

static std::wstring FormatExactInteger(const BigUInt& value) {
    if (value.IsZero()) return L"0";
    std::string text = value.ToString();
    std::wstring out(text.begin(), text.end());
    for (int i = static_cast<int>(out.size()) - 3; i > 0; i -= 3)
        out.insert(static_cast<size_t>(i), 1, L',');
    return out;
}

static int Popcount64(uint64_t value) {
    int count = 0;
    while (value != 0) {
        value &= (value - 1);
        ++count;
    }
    return count;
}

static void RegisterToolWindowClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }
        HWND owner = reinterpret_cast<HWND>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg) {
        case WM_COMMAND:
            if (owner) return SendMessageW(owner, WM_COMMAND, wParam, lParam);
            break;
        case WM_SIZE:
            if (owner) SendMessageW(owner, WM_APP_LAYOUT_FLOATING_TOOLS, 0, 0);
            return 0;
        case WM_VSCROLL: {
            SCROLLINFO si{sizeof(si), SIF_ALL};
            GetScrollInfo(hwnd, SB_VERT, &si);
            int pos = si.nPos;
            switch (LOWORD(wParam)) {
            case SB_LINEUP: pos -= 24; break;
            case SB_LINEDOWN: pos += 24; break;
            case SB_PAGEUP: pos -= static_cast<int>(si.nPage); break;
            case SB_PAGEDOWN: pos += static_cast<int>(si.nPage); break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: pos = si.nTrackPos; break;
            default: break;
            }
            pos = std::clamp(pos, si.nMin, std::max(si.nMin, si.nMax - static_cast<int>(si.nPage) + 1));
            SCROLLINFO out{sizeof(out), SIF_POS, 0, 0, 0, pos};
            SetScrollInfo(hwnd, SB_VERT, &out, TRUE);
            if (owner) SendMessageW(owner, WM_APP_LAYOUT_FLOATING_TOOLS, 0, 0);
            return 0;
        }
        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lParam);
            if (hdr->code == UDN_DELTAPOS) {
                auto* ud = reinterpret_cast<NMUPDOWN*>(lParam);
                if (owner) SendMessageW(owner, WM_APP_INSPECTOR_SPIN,
                                        static_cast<WPARAM>(hdr->idFrom),
                                        static_cast<LPARAM>(ud->iDelta));
                return TRUE; // prevent internal position update
            }
            break;
        }
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    };
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SeatingChartFloatingTools";
    RegisterClassW(&wc);
    registered = true;
}

LRESULT CALLBACK RosterListSubclassProc(HWND hwnd, UINT msg, WPARAM wParam,
                                        LPARAM lParam, UINT_PTR, DWORD_PTR refData) {
    auto* app = reinterpret_cast<SeatingChartApp*>(refData);
    if (!app) return DefSubclassProc(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_LBUTTONDOWN: {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        app->BeginRosterDragFromList(pt);
        break;
    }
    case WM_MOUSEMOVE: {
        POINT screenPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(hwnd, &screenPt);
        app->UpdateRosterDrag(screenPt);
        break;
    }
    case WM_LBUTTONUP: {
        POINT screenPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(hwnd, &screenPt);
        app->EndRosterDrag(screenPt, true);
        break;
    }
    case WM_CAPTURECHANGED:
        app->EndRosterDrag({}, false);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, RosterListSubclassProc, kRosterListSubclassId);
        break;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Subclass proc for the floating cell-edit EDIT control.
// Enter       → commit what is typed right now; do NOT advance to next field.
// Tab         → commit + advance to the next field (last-name after first-name).
// Escape      → cancel (discard).
// KillFocus   → commit (user clicked elsewhere; always save what was typed).
constexpr UINT_PTR kCellEditSubclassId = 3;
LRESULT CALLBACK CellEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam,
                                       LPARAM lParam, UINT_PTR, DWORD_PTR refData) {
    auto* app = reinterpret_cast<SeatingChartApp*>(refData);
    if (!app) return DefSubclassProc(hwnd, msg, wParam, lParam);
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            // Enter = commit and jump to the first-name field of the next student
            app->AdvanceToNextStudent();
            return 0;
        }
        if (wParam == VK_TAB) {
            // Tab = commit + move to next column
            app->CommitInlineCellEdit(/*advance=*/true);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            app->CancelInlineCellEdit();
            return 0;
        }
    }
    if (msg == WM_KILLFOCUS) {
        // Clicking elsewhere always commits — the student is saved with whatever
        // was typed.  We never silently discard what the user typed.
        app->CommitInlineCellEdit(/*advance=*/false);
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, CellEditSubclassProc, kCellEditSubclassId);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void SeatingChartApp::SetStatus(const std::wstring& text) {
    state_.status = text;
    UpdateSidebarText(state_, controls_);
    RefreshAutoAssignFooter();
    for (HWND h : {controls_.summaryLabel, controls_.statusLabel, controls_.footerMetaLabel, controls_.footerProgress})
        if (h) RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE);
}

void SeatingChartApp::RefreshAutoAssignFooter() {
    const bool onGroupsTab = sidebar_.ActiveTab() == 3;
    const auto estimatedTotal = EstimateRuleAwarePermutations(state_);
    std::wstring exactText;
    if (onGroupsTab) exactText = ExactGroupPermutationText();
    if (controls_.footerProgress) {
        if (onGroupsTab) {
            SendMessageW(controls_.footerProgress, PBM_SETPOS, 0, 0);
        } else {
        const size_t limit = aaProgressLimit_ > 0 ? aaProgressLimit_ : kDefaultAutoAssignSearchLimit;
        const int pct = (aaRunning_ && limit > 0)
            ? static_cast<int>(std::clamp((aaProgressSteps_ * 100) / limit,
                                          static_cast<size_t>(0),
                                          static_cast<size_t>(100)))
            : 0;
        SendMessageW(controls_.footerProgress, PBM_SETPOS, pct, 0);
        }
    }
    if (controls_.footerMetaLabel) {
        if (onGroupsTab) {
            SetWindowTextW(controls_.footerMetaLabel,
                (L"Exact groupings left: " + exactText).c_str());
        } else if (aaRunning_) {
            const long double remaining =
                std::max(0.0L, estimatedTotal - static_cast<long double>(aaProgressSteps_));
            SetWindowTextW(controls_.footerMetaLabel,
                (L"Permutations left: " + FormatPermutationEstimate(remaining)).c_str());
        } else {
            SetWindowTextW(controls_.footerMetaLabel,
                (L"Permutations left: " + FormatPermutationEstimate(estimatedTotal)).c_str());
        }
    }
    if (controls_.groupSummaryLabel) {
        SetWindowTextW(controls_.groupSummaryLabel,
            BuildGroupsSummaryText(onGroupsTab ? exactText : ExactGroupPermutationText()).c_str());
    }
    if (controls_.shuffleGroupsBtn && onGroupsTab) {
        const bool canShuffle = !CurrentGroupPattern().empty() &&
            exactText != L"0";
        SetWindowTextW(controls_.shuffleGroupsBtn, canShuffle ? L"Shuffle Groups"
                                                              : L"No Valid Groups");
        EnableWindow(controls_.shuffleGroupsBtn, canShuffle);
    } else if (controls_.shuffleGroupsBtn) {
        SetWindowTextW(controls_.shuffleGroupsBtn, L"Shuffle Groups");
    }
}

void SeatingChartApp::InvalidateChart() {
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// Host services for the layout sub-controllers
// ---------------------------------------------------------------------------

void SeatingChartApp::RecomputeLayoutTransform() {
    layoutTx_ = ComputeLayoutViewTransform(layout_.chart, state_.roomW, state_.roomH);
}

void SeatingChartApp::SyncLayoutInspector() {
    SyncLayoutInspectorWithSelection(state_, controls_);
}

void SeatingChartApp::RefreshButtons() {
    UpdateButtonState(state_, controls_, aaRunning_);
}

void SeatingChartApp::ScheduleSave() {
    ScheduleAutoSave(&state_, hwnd_);
}

void SeatingChartApp::InvalidateSelection() {
    // Selection lives in room coords; a precise region is not worth the math —
    // invalidate the whole chart so the selection chrome repaints cleanly.
    InvalidateChart();
}

void SeatingChartApp::NotifyStateChanged() {
    SyncLayoutInspector();
    RefreshButtons();
    InvalidateChart();
    ScheduleSave();
}

// ---------------------------------------------------------------------------
// Seat operations
// ---------------------------------------------------------------------------

void SeatingChartApp::ClearAllSeats() {
    if (MessageBoxW(hwnd_, L"Clear all seat assignments?", L"Confirm Clear All",
                    MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES) return;
    AppState::Transaction tx(state_);
    for (auto& item : tx->layoutItems)
        for (auto& occ : item.occupants) occ.clear();
    tx.Commit();
    SetStatus(L"All seats cleared");
    InvalidateChart();
    ScheduleAutoSave(&state_, hwnd_);
}

void SeatingChartApp::QuickFillSeats() {
    if (state_.roster.empty()) { SetStatus(L"No roster loaded"); return; }
    if (TotalLayoutSeats(state_.layoutItems) == 0)
        { SetStatus(L"Add furniture with seats first"); return; }
    AppState::Transaction tx(state_);
    std::unordered_set<std::wstring> assigned;
    for (const auto& item : tx->layoutItems)
        for (const auto& occ : item.occupants) {
            auto cn = CanonicalName(occ); if (!cn.empty()) assigned.insert(cn);
        }
    // Smart-ish quick fill: collect unassigned in roster order, shuffle for randomness (inspired by refs),
    // then assign to empty seats. This gives variety vs pure sequential.
    std::vector<std::wstring> unassigned;
    for (const auto& cand : tx->roster) {
        const auto cn = CanonicalName(cand);
        if (cn.empty() || assigned.count(cn)) continue;
        unassigned.push_back(cand);
    }
    // Simple shuffle for random fill (good enough without full solver; respects no dups)
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(unassigned.begin(), unassigned.end(), g);

    int filled = 0;
    size_t ui = 0;
    const auto seatRefs = EnumerateLayoutSeats(tx->layoutItems);
    for (const auto& ref : seatRefs) {
        auto& occ = tx->layoutItems[static_cast<size_t>(ref.first)]
                        .occupants[static_cast<size_t>(ref.second)];
        if (!occ.empty()) continue;
        if (ui >= unassigned.size()) break;
        const auto& cand = unassigned[ui++];
        occ = cand; assigned.insert(CanonicalName(cand)); ++filled;
    }
    if (filled > 0) {
        tx.Commit();
        SetStatus(L"Quick filled " + std::to_wstring(filled) + L" seats");
        InvalidateChart();
        ScheduleAutoSave(&state_, hwnd_);
    }
}

void SeatingChartApp::ExportSeatingCsv() {
    // Quick win CSV export: Name, Desk (label), Seat#, Tags, Notes
    const auto seatRefs = EnumerateLayoutSeats(state_.layoutItems);
    if (seatRefs.empty()) {
        SetStatus(L"No seats to export");
        return;
    }

    wchar_t fileName[MAX_PATH] = L"seating.csv";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd_;
    ofn.lpstrFile   = fileName;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrFilter = L"CSV Files\0*.csv\0All Files\0*.*\0\0";
    ofn.lpstrDefExt = L"csv";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn)) {
        SetStatus(L"CSV export cancelled");
        return;
    }

    std::wstring csv = L"Name,Desk,Seat,Tags,Notes\r\n";
    for (size_t i = 0; i < seatRefs.size(); ++i) {
        const auto& ref = seatRefs[i];
        const auto& item = state_.layoutItems[static_cast<size_t>(ref.first)];
        const std::wstring& occ = item.occupants[static_cast<size_t>(ref.second)];
        if (occ.empty()) continue;

        const StudentInfo* info = state_.FindStudent(occ);
        std::wstring tags;
        if (info) {
            for (size_t t = 0; t < info->tags.size(); ++t) {
                if (t > 0) tags += L";";
                tags += info->tags[t];
            }
        }
        std::wstring notes = info ? info->notes : L"";

        // Escape quotes for CSV
        auto escape = [](const std::wstring& s) {
            if (s.find(L',') != std::wstring::npos || s.find(L'"') != std::wstring::npos) {
                std::wstring out = L"\"";
                for (wchar_t c : s) { if (c == L'"') out += L'"'; out += c; }
                return out + L"\"";
            }
            return s;
        };

        csv += escape(occ) + L",";
        csv += escape(item.label.empty() ? std::wstring(LayoutTypeName(item.type)) : item.label) + L",";
        csv += std::to_wstring(ref.second + 1) + L","; // 1-based seat
        csv += escape(tags) + L",";
        csv += escape(notes) + L"\r\n";
    }

    if (WriteTextFileUtf8Atomic(fileName, csv)) {
        SetStatus(L"Exported seating CSV");
    } else {
        SetStatus(L"Failed to write CSV");
    }
}

void SeatingChartApp::ShowSeatingReport() {
    std::wstring report = L"Seating Chart Report\r\n\r\n";
    const auto seatRefs = EnumerateLayoutSeats(state_.layoutItems);
    int assigned = 0;
    for (size_t i = 0; i < seatRefs.size(); ++i) {
        const auto& ref = seatRefs[i];
        const auto& item = state_.layoutItems[static_cast<size_t>(ref.first)];
        const std::wstring& occ = item.occupants[static_cast<size_t>(ref.second)];
        if (!occ.empty()) ++assigned;
        std::wstring desk = item.label.empty() ? std::wstring(LayoutTypeName(item.type)) : item.label;
        report += occ.empty() ? L"(empty)" : occ;
        report += L" @ " + desk + L" seat " + std::to_wstring(ref.second + 1) + L"\r\n";
    }
    report += L"\r\nTotal seats: " + std::to_wstring(seatRefs.size());
    report += L"\r\nAssigned: " + std::to_wstring(assigned);
    if (!state_.roster.empty()) {
        report += L"\r\nRoster size: " + std::to_wstring(state_.roster.size());
        report += L"\r\nUnplaced: " + std::to_wstring(static_cast<int>(state_.roster.size()) - assigned);
    }
    report += L"\r\nRestrictions: " + std::to_wstring(state_.restrictions.size());
    report += L"\r\nMust together: " + std::to_wstring(state_.mustTogether.size());
    report += L"\r\nGroups: " + std::to_wstring(state_.groupAffinities.size());
    if (state_.lastAffinitySatisfaction > 0) {
        int pct = static_cast<int>(state_.lastAffinitySatisfaction * 100.0 + 0.5);
        report += L"\r\nLast affinity score: " + std::to_wstring(pct) + L"%";
    }
    MessageBoxW(hwnd_, report.c_str(), L"Seating Report", MB_OK | MB_ICONINFORMATION);
    SetStatus(L"Report shown");
}

void SeatingChartApp::ExportHtml() {
    wchar_t fileName[MAX_PATH] = L"seating.html";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd_;
    ofn.lpstrFile   = fileName;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrFilter = L"HTML Files\0*.html\0All Files\0*.*\0\0";
    ofn.lpstrDefExt = L"html";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn)) {
        SetStatus(L"HTML export cancelled");
        return;
    }

    std::wstring html = L"<!DOCTYPE html><html><head><meta charset='utf-8'><title>Seating Chart</title></head><body>";
    html += L"<h1>Seating Chart</h1><table border='1'><tr><th>Name</th><th>Desk</th><th>Seat</th><th>Tags</th><th>Notes</th></tr>";
    const auto seatRefs = EnumerateLayoutSeats(state_.layoutItems);
    for (size_t i = 0; i < seatRefs.size(); ++i) {
        const auto& ref = seatRefs[i];
        const auto& item = state_.layoutItems[static_cast<size_t>(ref.first)];
        const std::wstring& occ = item.occupants[static_cast<size_t>(ref.second)];
        if (occ.empty()) continue;
        const StudentInfo* info = state_.FindStudent(occ);
        std::wstring tags, notes;
        if (info) {
            for (size_t t=0; t<info->tags.size(); ++t) { if(t>0) tags+=L", "; tags += info->tags[t]; }
            notes = info->notes;
        }
        std::wstring desk = item.label.empty() ? std::wstring(LayoutTypeName(item.type)) : item.label;
        html += L"<tr><td>" + occ + L"</td><td>" + desk + L"</td><td>" + std::to_wstring(ref.second+1) + L"</td><td>" + tags + L"</td><td>" + notes + L"</td></tr>";
    }
    html += L"</table></body></html>";

    if (WriteTextFileUtf8Atomic(fileName, html)) {
        SetStatus(L"Exported HTML");
    } else {
        SetStatus(L"Failed to write HTML");
    }
}

// ---------------------------------------------------------------------------
// Roster / restrictions
// ---------------------------------------------------------------------------

void SeatingChartApp::RefreshFilteredRosterList() {
    if (!controls_.rosterList || !controls_.rosterFilter) return;

    std::wstring filterText = GetWindowTextStr(controls_.rosterFilter);
    std::wstring f = CanonicalName(filterText); // reuse for case/space insen

    SendMessageW(controls_.rosterList, LB_RESETCONTENT, 0, 0);

    for (const auto& n : state_.roster) {
        bool match = f.empty();
        if (!match) {
            if (CanonicalName(n).find(f) != std::wstring::npos) {
                match = true;
            } else {
                const StudentInfo* info = state_.FindStudent(n);
                if (info) {
                    for (const auto& t : info->tags) {
                        if (CanonicalName(t).find(f) != std::wstring::npos) {
                            match = true;
                            break;
                        }
                    }
                }
            }
        }
        if (match) {
            SendMessageW(controls_.rosterList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n.c_str()));
        }
    }
}

void SeatingChartApp::ApplyRowsPreset() {
    WriteAppLog(L"ApplyRowsPreset started");
    if (state_.chartMode != ChartMode::Layout) {
        SetChartMode(ChartMode::Layout);
    }
    {
        AppState::Transaction tx(state_);
        tx->layoutItems.clear();
        tx->selectedLayoutItem = std::nullopt;
        tx->selectedLayoutItems.clear();
        tx->selectedLayoutSeat = std::nullopt;
        tx->frontEdge = RoomEdge::Top;

        const int roomW = EffectiveRoomW(tx->roomW);
        const int boardW = 480;
        const int boardH = 70;
        LayoutItem board;
        board.type = LayoutItemType::Smartboard;
        board.bounds = { (roomW - boardW) / 2, 30, (roomW + boardW) / 2, 30 + boardH };
        board.label = L"Smartboard";
        board.capacity = 0;
        EnsureSeatSlots(board);
        tx->layoutItems.push_back(board);

        const int cols = 6;
        const int rows = 4;
        const int desk = 82;
        const int gapX = 44;
        const int gapY = 48;
        const int totalW = cols * desk + (cols - 1) * gapX;
        const int startX = (roomW - totalW) / 2;
        const int startY = 180;
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                const int x = startX + col * (desk + gapX);
                const int y = startY + row * (desk + gapY);
                LayoutItem item;
                item.type = LayoutItemType::RectangleDesk;
                item.bounds = { x, y, x + desk, y + desk };
                item.label = L"Desk";
                item.capacity = LayoutItemDefaultCapacity(item.type);
                EnsureSeatSlots(item);
                tx->layoutItems.push_back(item);
            }
        }
        tx.Commit();
    }
    InvalidateChart();
    SetStatus(L"Applied Rows preset: 24 desks facing the smartboard");
    UpdateButtonState(state_, controls_, aaRunning_);
    WriteAppLog(L"ApplyRowsPreset finished");
}

void SeatingChartApp::ApplyUPreset() {
    WriteAppLog(L"ApplyUPreset started");
    if (state_.chartMode != ChartMode::Layout) {
        SetChartMode(ChartMode::Layout);
    }
    {
        AppState::Transaction tx(state_);
        tx->layoutItems.clear();
        tx->selectedLayoutItem = std::nullopt;
        tx->selectedLayoutItems.clear();
        tx->selectedLayoutSeat = std::nullopt;
        tx->frontEdge = RoomEdge::Top;

        const int roomW = EffectiveRoomW(tx->roomW);
        LayoutItem board;
        board.type = LayoutItemType::Smartboard;
        board.bounds = { (roomW - 480) / 2, 30, (roomW + 480) / 2, 100 };
        board.label = L"Smartboard";
        EnsureSeatSlots(board);
        tx->layoutItems.push_back(board);

        const int desk = 66;
        const int leftX = 150;
        const int rightX = roomW - 150 - desk;
        const int startY = 155;
        const int stepY = 72;
        for (int row = 0; row < 8; ++row) {
            const int y = startY + row * stepY;
            for (int side = 0; side < 2; ++side) {
                const int x = side == 0 ? leftX : rightX;
                LayoutItem item;
                item.type = LayoutItemType::RectangleDesk;
                item.bounds = { x, y, x + desk, y + desk };
                item.label = L"Desk";
                item.capacity = LayoutItemDefaultCapacity(item.type);
                EnsureSeatSlots(item);
                tx->layoutItems.push_back(item);
            }
        }

        const int bottomCount = 8;
        const int gapX = 42;
        const int totalW = bottomCount * desk + (bottomCount - 1) * gapX;
        const int bottomX = (roomW - totalW) / 2;
        const int bottomY = 700;
        for (int col = 0; col < bottomCount; ++col) {
            const int x = bottomX + col * (desk + gapX);
            LayoutItem item;
            item.type = LayoutItemType::RectangleDesk;
            item.bounds = { x, bottomY, x + desk, bottomY + desk };
            item.label = L"Desk";
            item.capacity = LayoutItemDefaultCapacity(item.type);
            EnsureSeatSlots(item);
            tx->layoutItems.push_back(item);
        }
        tx.Commit();
    }
    InvalidateChart();
    SetStatus(L"Applied U preset: 24 desks around a discussion space");
    UpdateButtonState(state_, controls_, aaRunning_);
    WriteAppLog(L"ApplyUPreset finished");
}

void SeatingChartApp::ApplyHorseshoePreset() {
    WriteAppLog(L"ApplyHorseshoePreset started");
    if (state_.chartMode != ChartMode::Layout) {
        SetChartMode(ChartMode::Layout);
    }
    {
        AppState::Transaction tx(state_);
        tx->layoutItems.clear();
        tx->selectedLayoutItem = std::nullopt;
        tx->selectedLayoutItems.clear();
        tx->selectedLayoutSeat = std::nullopt;
        tx->frontEdge = RoomEdge::Top;

        const int roomW = EffectiveRoomW(tx->roomW);
        LayoutItem board;
        board.type = LayoutItemType::Smartboard;
        board.bounds = { (roomW - 480) / 2, 30, (roomW + 480) / 2, 100 };
        board.label = L"Smartboard";
        EnsureSeatSlots(board);
        tx->layoutItems.push_back(board);

        const int desk = 66;
        const double cx = roomW / 2.0;
        const double cy = 280.0;
        const double rx = 455.0;
        const double ry = 450.0;
        constexpr double pi = 3.14159265358979323846;
        for (int i = 0; i < 24; ++i) {
            const double t = (20.0 + (140.0 * i / 23.0)) * pi / 180.0;
            const int x = static_cast<int>(std::lround(cx + std::cos(t) * rx - desk / 2.0));
            const int y = static_cast<int>(std::lround(cy + std::sin(t) * ry - desk / 2.0));
            LayoutItem item;
            item.type = LayoutItemType::RectangleDesk;
            item.bounds = { x, y, x + desk, y + desk };
            item.label = L"Desk";
            item.capacity = LayoutItemDefaultCapacity(item.type);
            EnsureSeatSlots(item);
            tx->layoutItems.push_back(item);
        }
        tx.Commit();
    }
    InvalidateChart();
    SetStatus(L"Applied Horseshoe preset: 24 desks in a rounded discussion layout");
    UpdateButtonState(state_, controls_, aaRunning_);
    WriteAppLog(L"ApplyHorseshoePreset finished");
}

// ---------------------------------------------------------------------------
// Roster / restrictions
// ---------------------------------------------------------------------------

void SeatingChartApp::ApplyRoster(std::vector<std::wstring> roster) {
    AppState::Transaction tx(state_);
    tx->roster = std::move(roster);
    tx.Commit();
    RefreshFilteredRosterList();
    SyncRosterView(state_, controls_);
    RefreshGroupConfigList();
    generatedGroups_.clear();
    SyncGroupsOutput();
    SyncRosterEditFromRoster(state_, controls_);
    const auto rp = GetRosterFilePath();
    if (!rp.empty()) {
        std::wstring text;
        for (const auto& n : state_.roster) { text += n; text += L"\r\n"; }
        if (!WriteTextFileUtf8Atomic(rp, text)) {
            SetStatus(L"Roster imported, but saving the roster file failed");
            ScheduleAutoSave(&state_, hwnd_);
            return;
        }
    }
    SetStatus(L"Roster imported");
    ScheduleAutoSave(&state_, hwnd_);
}

void SeatingChartApp::ImportRosterFromEdit() {
    std::unordered_map<std::wstring, std::vector<std::wstring>> parsedTags;
    const auto roster = SplitRosterInput(GetWindowTextStr(controls_.rosterEdit), &parsedTags);
    if (roster.empty()) { SetStatus(L"Paste or type names into the roster box first"); return; }
    std::wstring dup;
    if (FindDuplicateCanonicalName(roster, &dup)) {
        SetStatus(L"Roster has duplicate name: " + dup); return;
    }
    ApplyRoster(roster);
    // Merge any [tags] parsed from the richer roster syntax into studentInfo.
    if (!parsedTags.empty()) {
        AppState::Transaction tx(state_);
        for (const auto& n : roster) {
            auto cn = CanonicalName(n);
            auto it = parsedTags.find(cn);
            if (it != parsedTags.end() && !it->second.empty()) {
                auto& info = state_.StudentRecord(n);
                for (const auto& tg : it->second) {
                    auto ctg = CanonicalName(tg);
                    if (ctg.empty()) continue;
                    if (std::find(info.tags.begin(), info.tags.end(), tg) == info.tags.end())
                        info.tags.push_back(tg);
                }
            }
        }
        tx.Commit();
    }
}

void SeatingChartApp::ApplyRestrictions(std::vector<Restriction> restrictions,
                                        std::vector<Restriction> affinities,
                                        std::vector<Restriction> mustTogether,
                                        std::vector<std::vector<std::wstring>> groups) {
    AppState::Transaction tx(state_);
    tx->restrictions = std::move(restrictions);
    tx->affinities   = std::move(affinities);
    tx->mustTogether = std::move(mustTogether);
    tx.Commit();
    SyncRestrictionEditFromRules(state_, controls_);
    const auto rp = GetRestrictionsFilePath();
    if (!rp.empty()) {
        std::wstring text;
        for (const auto& r : state_.restrictions)
            text += r.first + L" | " + r.second + (r.radius > 0 ? (L" @" + std::to_wstring(r.radius)) : L"") + L"\r\n";
        for (const auto& a : state_.affinities)
            text += a.first + L" + " + a.second + L"\r\n";
        for (const auto& t : state_.mustTogether)
            text += t.first + L" == " + t.second + L"\r\n";
        if (!WriteTextFileUtf8Atomic(rp, text)) {
            SetStatus(L"Rules updated, but saving the rules file failed");
            ScheduleAutoSave(&state_, hwnd_);
            if (!groups.empty()) {
                ApplyGroupRules(std::move(groups));
            }
            return;
        }
    }
    SetStatus(std::to_wstring(state_.restrictions.size()) + L" keep-apart, "
              + std::to_wstring(state_.affinities.size()) + L" sit-near, "
              + std::to_wstring(state_.mustTogether.size()) + L" must-together rules");
    ScheduleAutoSave(&state_, hwnd_);
    if (!groups.empty()) {
        ApplyGroupRules(std::move(groups));
    }
}

void SeatingChartApp::ApplyGroupRules(std::vector<std::vector<std::wstring>> groups) {
    AppState::Transaction tx(state_);
    tx->groupAffinities = std::move(groups);
    tx.Commit();
    SyncGroupRulesEditFromState(state_, controls_);
    RefreshAutoAssignFooter();
    ScheduleAutoSave(&state_, hwnd_);
}

bool SeatingChartApp::LoadRosterFromFile(const std::wstring& path) {
    std::wstring content;
    if (!ReadTextFileUtf8(path, &content)) { SetStatus(L"Could not read roster file"); return false; }
    std::unordered_map<std::wstring, std::vector<std::wstring>> parsedTags;
    const auto roster = SplitRosterInput(content, &parsedTags);
    if (roster.empty()) { SetStatus(L"No names found in file"); return false; }
    std::wstring dup;
    if (FindDuplicateCanonicalName(roster, &dup)) {
        SetStatus(L"Roster file has duplicate name: " + dup); return false;
    }
    ApplyRoster(roster);
    // Merge [tags] from file content (supports "Name [tag1,tag2]" lines or CSV-ish)
    if (!parsedTags.empty()) {
        AppState::Transaction tx(state_);
        for (const auto& n : roster) {
            auto cn = CanonicalName(n);
            auto it = parsedTags.find(cn);
            if (it != parsedTags.end() && !it->second.empty()) {
                auto& info = state_.StudentRecord(n);
                for (const auto& tg : it->second) {
                    auto ctg = CanonicalName(tg);
                    if (ctg.empty()) continue;
                    if (std::find(info.tags.begin(), info.tags.end(), tg) == info.tags.end())
                        info.tags.push_back(tg);
                }
            }
        }
        tx.Commit();
    }
    return true;
}

bool SeatingChartApp::PromptAndLoadRosterFile() {
    wchar_t fileName[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd_;
    ofn.lpstrFile   = fileName;    ofn.nMaxFile  = MAX_PATH;
    ofn.lpstrFilter = L"Roster Files\0*.txt;*.csv;*.tsv\0All Files\0*.*\0\0";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    const auto dir  = GetAppDataStateDir();
    if (!dir.empty()) ofn.lpstrInitialDir = dir.c_str();
    if (!GetOpenFileNameW(&ofn)) return false;
    return LoadRosterFromFile(fileName);
}

void SeatingChartApp::AssignRosterSelectionToSeat() {
    // Assign the highlighted roster name to the focused furniture seat.
    if (!controls_.rosterList) return;
    if (!state_.selectedLayoutSeat) {
        SetStatus(L"Click a seat on the chart first, then choose a name");
        return;
    }
    const LRESULT sel = SendMessageW(controls_.rosterList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;
    const int len = static_cast<int>(SendMessageW(controls_.rosterList, LB_GETTEXTLEN, sel, 0));
    if (len < 0) return;
    std::wstring text(static_cast<size_t>(len + 1), L'\0');
    SendMessageW(controls_.rosterList, LB_GETTEXT, sel, reinterpret_cast<LPARAM>(text.data()));
    text.resize(static_cast<size_t>(len));

    const auto seat = *state_.selectedLayoutSeat;
    if (seat.first < 0 || seat.first >= static_cast<int>(state_.layoutItems.size())) return;
    auto& occs = state_.layoutItems[static_cast<size_t>(seat.first)].occupants;
    if (seat.second < 0 || seat.second >= static_cast<int>(occs.size())) return;
    if (state_.layoutItems[static_cast<size_t>(seat.first)].locked) {
        SetStatus(L"Seat is on a locked item — Unlock to edit"); return;
    }
    state_.AssignStudentToSeatExclusive(seat, text);
    SetStatus(L"Assigned " + text);
    InvalidateChart();
    ScheduleSave();
}

// Build the sizes for groups given N students and base size k.
// Returns a vector where each entry is the size of one group.
// Groups will have size k or k+1 (never less than k, never more than k+1),
// except when that's mathematically impossible — then distributes as evenly as possible.
static std::vector<int> BuildGroupPattern(int N, int k) {
    if (N < 2 || k < 2) return {};

    const int q = N / k;   // groups if all were size k
    const int r = N % k;   // leftover students

    std::vector<int> pat;
    if (r == 0) {
        // Perfect split: q groups of k
        pat.assign(static_cast<size_t>(q), k);
    } else if (q >= r) {
        // (q-r) groups of k, r groups of (k+1)
        if (q - r > 0) pat.assign(static_cast<size_t>(q - r), k);
        pat.insert(pat.end(), static_cast<size_t>(r), k + 1);
    } else {
        // Can't stay within [k, k+1] — distribute as evenly as possible.
        // ceil(N/k) groups, sizes floor(N/numGroups) and ceil(N/numGroups).
        const int numGroups = (N + k - 1) / k;
        const int base      = N / numGroups;
        const int extra     = N % numGroups;
        if (numGroups - extra > 0)
            pat.assign(static_cast<size_t>(numGroups - extra), base);
        pat.insert(pat.end(), static_cast<size_t>(extra), base + 1);
    }

    return pat;
}

static std::wstring FormatGroupPattern(const std::vector<int>& pat) {
    if (pat.empty()) return L"—";
    const int minSz = *std::min_element(pat.begin(), pat.end());
    const int maxSz = *std::max_element(pat.begin(), pat.end());
    const auto szStr = [](int s) { return std::to_wstring(s); };
    std::wstring out = std::to_wstring(pat.size()) + L" group" + (pat.size() > 1 ? L"s" : L"");
    if (minSz == maxSz)
        out += L" of " + szStr(minSz);
    else
        out += L" of " + szStr(minSz) + L" or " + szStr(maxSz);
    return out;
}

// Canonical key for a pair (order-independent).
static std::wstring GroupPairKey(const std::wstring& a, const std::wstring& b) {
    const auto ca = CanonicalName(a), cb = CanonicalName(b);
    return (ca < cb) ? (ca + L"|" + cb) : (cb + L"|" + ca);
}

static std::wstring ListBoxTextAt(HWND list, int index) {
    if (!list || index < 0) return {};
    const int len = static_cast<int>(SendMessageW(list, LB_GETTEXTLEN, index, 0));
    if (len <= 0) return {};
    std::wstring text(static_cast<size_t>(len + 1), L'\0');
    SendMessageW(list, LB_GETTEXT, index, reinterpret_cast<LPARAM>(text.data()));
    text.resize(static_cast<size_t>(len));
    return text;
}

bool SeatingChartApp::BeginRosterDragFromList(POINT listClientPt) {
    rosterDragPrimed_ = false;
    rosterDragging_ = false;
    rosterDragName_.clear();
    dragPreview_ = {};

    if (!controls_.rosterList || state_.chartMode != ChartMode::Seats) return false;
    const DWORD hit = static_cast<DWORD>(SendMessageW(
        controls_.rosterList, LB_ITEMFROMPOINT, 0, MAKELPARAM(listClientPt.x, listClientPt.y)));
    if (HIWORD(hit) != 0) return false;

    rosterDragName_ = ListBoxTextAt(controls_.rosterList, LOWORD(hit));
    if (rosterDragName_.empty()) return false;

    rosterDragStartScreen_ = listClientPt;
    ClientToScreen(controls_.rosterList, &rosterDragStartScreen_);
    rosterDragPrimed_ = true;
    return true;
}

void SeatingChartApp::UpdateRosterDrag(POINT screenPt) {
    if (!rosterDragPrimed_ && !rosterDragging_) return;

    const int dx = std::abs(screenPt.x - rosterDragStartScreen_.x);
    const int dy = std::abs(screenPt.y - rosterDragStartScreen_.y);
    if (!rosterDragging_) {
        if (dx < GetSystemMetrics(SM_CXDRAG) && dy < GetSystemMetrics(SM_CYDRAG)) return;
        rosterDragging_ = true;
        dragPreview_.active = true;
        dragPreview_.studentName = rosterDragName_;
        SetCapture(controls_.rosterList);
        SetStatus(L"Drag " + rosterDragName_ + L" onto a seat");
    }

    POINT clientPt = screenPt;
    ScreenToClient(hwnd_, &clientPt);
    dragPreview_.cursorPt = clientPt;
    dragPreview_.overChart = PtInRectEx(layout_.chart, clientPt);
    dragPreview_.valid = false;
    dragPreview_.targetSeat = false;
    dragPreview_.seat = { -1, -1 };

    if (state_.chartMode == ChartMode::Seats) {
        if (const auto seat = selection_.HitTestSeatSlot(clientPt)) {
            const int it = seat->first;
            const int sl = seat->second;
            if (it >= 0 && it < static_cast<int>(state_.layoutItems.size())) {
                const auto& item = state_.layoutItems[static_cast<size_t>(it)];
                const bool slotOk = sl >= 0 && sl < static_cast<int>(item.occupants.size());
                dragPreview_.targetSeat = slotOk;
                dragPreview_.valid = slotOk && !item.locked &&
                    item.occupants[static_cast<size_t>(sl)].empty();
                dragPreview_.seat = *seat;
                if (slotOk) state_.selectedLayoutSeat = *seat;
            }
        }
    }

    SetCursor(LoadCursor(nullptr, dragPreview_.valid ? IDC_HAND : IDC_NO));
    InvalidateChart();
}

void SeatingChartApp::EndRosterDrag(POINT screenPt, bool commit) {
    const bool wasDragging = rosterDragging_;
    if (!rosterDragPrimed_ && !rosterDragging_) return;

    if (wasDragging && commit) UpdateRosterDrag(screenPt);

    const bool shouldAssign = wasDragging && commit && dragPreview_.valid;
    const LayoutSeatRef seat = dragPreview_.seat;
    const std::wstring name = rosterDragName_;

    rosterDragPrimed_ = false;
    rosterDragging_ = false;
    rosterDragName_.clear();
    dragPreview_ = {};

    if (GetCapture() == controls_.rosterList) ReleaseCapture();

    if (shouldAssign) {
        state_.AssignStudentToSeatExclusive(seat, name);
        state_.selectedLayoutSeat = seat;
        SetStatus(L"Assigned " + name);
        ScheduleSave();
    } else if (wasDragging && commit) {
        SetStatus(L"Drop on a seat to assign");
    }

    InvalidateChart();
    RefreshButtons();
}

void SeatingChartApp::DeleteSelectedRosterStudents() {
    if (!controls_.rosterView) return;
    const int total = ListView_GetItemCount(controls_.rosterView);
    // Collect selected indices (all items, since list may show a partial roster after filter)
    std::vector<int> selected;
    for (int i = 0; i < total; ++i) {
        if (ListView_GetItemState(controls_.rosterView, i, LVIS_SELECTED) & LVIS_SELECTED) {
            if (i < static_cast<int>(state_.roster.size()))
                selected.push_back(i);
        }
    }
    if (selected.empty()) return;

    // Collect canonical names for seat-clearing
    std::vector<std::wstring> canonicals;
    for (int idx : selected)
        canonicals.push_back(CanonicalName(state_.roster[static_cast<size_t>(idx)]));

    // Delete from highest index downward to keep indices stable
    AppState::Transaction tx(state_);
    for (int i = static_cast<int>(selected.size()) - 1; i >= 0; --i) {
        tx->roster.erase(tx->roster.begin() + selected[static_cast<size_t>(i)]);
    }
    // Clear seats occupied by removed students
    for (auto& item : tx->layoutItems)
        for (auto& occ : item.occupants) {
            const auto cn = CanonicalName(occ);
            if (!cn.empty() && std::find(canonicals.begin(), canonicals.end(), cn) != canonicals.end())
                occ.clear();
        }
    tx.Commit();

    SyncRosterView(state_, controls_);
    RefreshRosterList(state_, controls_);
    RefreshGroupConfigList();
    InvalidateChart();
    SetStatus(L"Deleted " + std::to_wstring(selected.size()) + L" student" +
              (selected.size() == 1 ? L"" : L"s"));
    ScheduleSave();
}

void SeatingChartApp::BulkTagSelectedRoster() {
    if (!controls_.rosterList) return;
    int count = SendMessageW(controls_.rosterList, LB_GETSELCOUNT, 0, 0);
    if (count <= 0) return;
    std::vector<int> sel(count);
    SendMessageW(controls_.rosterList, LB_GETSELITEMS, count, reinterpret_cast<LPARAM>(sel.data()));
    AppState::Transaction tx(state_);
    for (int i : sel) {
        if (i < 0 || i >= static_cast<int>(state_.roster.size())) continue;
        const auto& name = state_.roster[i];
        auto& info = state_.StudentRecord(name);
        std::wstring tag = L"Behavior";
        auto it = std::find(info.tags.begin(), info.tags.end(), tag);
        if (it == info.tags.end()) {
            info.tags.push_back(tag);
        } else {
            info.tags.erase(it);
        }
    }
    tx.Commit();
    RefreshFilteredRosterList();
    SetStatus(L"Bulk tag toggled for " + std::to_wstring(count) + L" students");
}

// ---------------------------------------------------------------------------
// Auto-assign
// ---------------------------------------------------------------------------

void SeatingChartApp::StartAutoAssign() {
    if (aaRunning_) return;

    // Pre-solve check with specific warnings (from validator + local)
    auto problems = ValidateAutoAssignPreconditions(state_);
    if (!problems.empty()) {
        std::wstring msg = L"The following problems prevent auto-assign:\r\n\r\n";
        for (const auto& p : problems) {
            msg += L"• " + p + L"\r\n";
        }
        msg += L"\r\nFix these issues and try again.";
        MessageBoxW(hwnd_, msg.c_str(), L"Auto-Assign Pre-check", MB_OK | MB_ICONWARNING);
        SetStatus(L"Auto-assign aborted due to problems");
        return;
    }

    std::wstring dup;
    if (state_.roster.empty()) { SetStatus(L"No roster loaded"); return; }
    if (TotalLayoutSeats(state_.layoutItems) == 0)
        { SetStatus(L"Add furniture with seats first"); return; }
    if (static_cast<int>(state_.roster.size()) > TotalLayoutSeats(state_.layoutItems))
        { SetStatus(L"Roster has more students than seats"); return; }
    if (FindDuplicateCanonicalName(state_.roster, &dup))
        { SetStatus(L"Roster has duplicate name: " + dup); return; }

    aaStartRevision_ = state_.Revision();
    aaRunning_ = true;
    aaProgressSteps_ = 0;
    aaProgressLimit_ = state_.autoAssignSearchLimit > 0 ? state_.autoAssignSearchLimit : kDefaultAutoAssignSearchLimit;
    UpdateButtonState(state_, controls_, aaRunning_);
    RefreshAutoAssignFooter();
    SetStatus(L"Assigning seats…");
    UpdateWindow(controls_.sidebar);

    if (!BeginAutoAssign(hwnd_, state_, aaCancel_, aaThread_)) {
        aaRunning_ = false;
        aaProgressSteps_ = 0;
        SetStatus(L"Could not start auto-assign");
        UpdateButtonState(state_, controls_, aaRunning_);
        RefreshAutoAssignFooter();
    }
}

LRESULT SeatingChartApp::OnAutoAssignDone(AutoAssignResult* result) {
    if (!result->success) {
        SetStatus(result->errorMessage);
    } else if (state_.Revision() != aaStartRevision_) {
        SetStatus(L"Auto-assign discarded — chart changed while running");
    } else {
        AppState::Transaction tx(state_);
        const auto seatRefs = EnumerateLayoutSeats(tx->layoutItems);
        int count = 0;
        for (size_t i = 0; i < result->assignments.size() && i < result->roster.size(); ++i) {
            const int si = result->assignments[i];
            if (si >= 0 && si < static_cast<int>(seatRefs.size())) {
                const auto& ref = seatRefs[static_cast<size_t>(si)];
                tx->layoutItems[static_cast<size_t>(ref.first)]
                    .occupants[static_cast<size_t>(ref.second)] = result->roster[i];
                ++count;
            }
        }
        if (count > 0) {
            tx.Commit();
            state_.lastAffinitySatisfaction = result->affinitySatisfaction;
            std::wstring status = L"Assigned " + std::to_wstring(count) + L" students automatically";
            if (result->affinitySatisfaction > 0.0 && result->affinitySatisfaction < 1.0) {
                int pct = static_cast<int>(result->affinitySatisfaction * 100.0 + 0.5);
                status += L" (" + std::to_wstring(pct) + L"% affinities met)";
            } else if (result->affinitySatisfaction >= 1.0) {
                status += L" (all affinities met)";
            }
            if (result->stepsUsed > 0) {
                status += L" in " + std::to_wstring(result->stepsUsed / 1000) + L"k steps";
            }
            if (result->elapsedMs > 0) {
                int secs = static_cast<int>(result->elapsedMs / 1000);
                int ms = static_cast<int>(result->elapsedMs) % 1000;
                status += L" (" + std::to_wstring(secs) + L"." + std::to_wstring(ms / 100) + L"s)";
            }
            SetStatus(status);
            InvalidateChart();
            UpdateSidebarText(state_, controls_);
            ScheduleAutoSave(&state_, hwnd_);
        }
    }
    delete result;
    if (aaThread_) { CloseHandle(aaThread_); aaThread_ = nullptr; }
    aaRunning_ = false;
    aaProgressSteps_ = 0;
    UpdateButtonState(state_, controls_, aaRunning_);
    RefreshAutoAssignFooter();
    return 0;
}

LRESULT SeatingChartApp::OnAutoAssignProgress(size_t steps) {
    const size_t limit = state_.autoAssignSearchLimit > 0 ? state_.autoAssignSearchLimit : 500000;
    aaProgressSteps_ = steps;
    aaProgressLimit_ = limit;
    RefreshAutoAssignFooter();
    const size_t pct = (limit > 0) ? (steps * 100 / limit) : 0;
    SetStatus(L"Assigning seats… " + std::to_wstring(steps / 1000) +
              L"k / " + std::to_wstring(limit / 1000) +
              L"k steps (" + std::to_wstring(pct) + L"%)");
    return 0;
}

// ---------------------------------------------------------------------------
// Layout inspector — controller-owned: reads the sidebar edit controls and
// pushes the values into the selected item via the LayoutEditor's clamping.
// ---------------------------------------------------------------------------

void SeatingChartApp::ApplyLayoutInspector() {
    if (!state_.selectedLayoutItem) return;
    const int idx = *state_.selectedLayoutItem;
    if (idx >= static_cast<int>(state_.layoutItems.size())) return;
    AppState::Transaction tx(state_);
    LayoutItem& item = state_.layoutItems[static_cast<size_t>(idx)];
    if (item.locked) { SetStatus(L"Item is locked — unlock first"); return; }
    item.label = TrimCopy(GetWindowTextStr(controls_.layoutLabelEdit));
    if (item.label.empty()) item.label = std::wstring(LayoutTypeName(item.type));
    auto gi = [](HWND h, int fb) {
        const auto t = GetWindowTextStr(h); wchar_t* e = nullptr;
        const long v = wcstol(t.c_str(), &e, 10); return (e == t.c_str()) ? fb : static_cast<int>(v);
    };
    const int x = gi(controls_.layoutXEdit,     static_cast<int>(item.bounds.left));
    const int y = gi(controls_.layoutYEdit,      static_cast<int>(item.bounds.top));
    const int w = std::max(Scale(20), gi(controls_.layoutWidthEdit,  static_cast<int>(item.bounds.right  - item.bounds.left)));
    const int h = std::max(Scale(20), gi(controls_.layoutHeightEdit, static_cast<int>(item.bounds.bottom - item.bounds.top)));
    item.bounds = editor_.ClampToRoom(SnapRectToGrid({x, y, x+w, y+h}, kRoomSnapGrid));
    if (item.type == LayoutItemType::BigTable) {
        const int cap = gi(controls_.layoutCapacityEdit,
                           item.capacity > 0 ? item.capacity : LayoutItemDefaultCapacity(item.type));
        item.capacity = std::clamp(cap, 1, 50);
        EnsureSeatSlots(item);   // keep occupants[] sized to the new capacity
    }
    tx.Commit();
    SyncLayoutInspectorWithSelection(state_, controls_);
    InvalidateChart();
    SetStatus(L"Layout item updated");
    ScheduleAutoSave(&state_, hwnd_);
}

void SeatingChartApp::OnInspectorSpin(int spinId, int delta) {
    if (!IsLayoutMode() || !state_.selectedLayoutItem) return;
    const int idx = *state_.selectedLayoutItem;
    if (idx >= static_cast<int>(state_.layoutItems.size())) return;
    if (state_.layoutItems[static_cast<size_t>(idx)].locked) return;

    HWND edit = nullptr;
    switch (spinId) {
    case kLayoutXSpinId: edit = controls_.layoutXEdit;     break;
    case kLayoutYSpinId: edit = controls_.layoutYEdit;     break;
    case kLayoutWSpinId: edit = controls_.layoutWidthEdit; break;
    case kLayoutHSpinId: edit = controls_.layoutHeightEdit;break;
    default: return;
    }
    if (!edit) return;

    wchar_t buf[32] = {};
    GetWindowTextW(edit, buf, 32);
    int val = _wtoi(buf) + delta;
    SetWindowTextW(edit, std::to_wstring(val).c_str());
    ApplyLayoutInspector();
}

void SeatingChartApp::SetLayoutSelectionHint() {
    if (state_.chartMode != ChartMode::Layout || state_.selectedLayoutItems.empty())
        return;
    if (state_.selectedLayoutItems.size() > 1) {
        SetStatus(L"Drag to move selection \xB7 right-click for align tools");
        return;
    }
    const bool locked = state_.selectedLayoutItem &&
        state_.layoutItems[static_cast<size_t>(*state_.selectedLayoutItem)].locked;
    SetStatus(locked
        ? L"Locked \xB7 use Unlock to edit"
        : L"Drag to move \xB7 corners resize \xB7 top handle rotates \xB7 Shift snaps");
}

void SeatingChartApp::AssignLayoutSeatViaMenu(LayoutSeatRef seat, POINT screenAnchor) {
    const int it = seat.first, sl = seat.second;
    if (it < 0 || it >= static_cast<int>(state_.layoutItems.size())) return;
    LayoutItem& item = state_.layoutItems[static_cast<size_t>(it)];
    EnsureSeatSlots(item);
    if (sl < 0 || sl >= static_cast<int>(item.occupants.size())) return;
    if (item.locked) { SetStatus(L"Seat is on a locked item — Unlock to edit"); return; }

    state_.selectedLayoutSeat = seat;
    InvalidateChart();

    constexpr UINT kIdRemove     = 50001;
    constexpr UINT kIdRosterBase = 50100;
    const std::wstring current = item.occupants[static_cast<size_t>(sl)];
    const std::wstring curCanon = CanonicalName(current);

    // Canonical names already seated anywhere in the layout (to flag duplicates).
    std::unordered_set<std::wstring> seated;
    for (const auto& li : state_.layoutItems)
        for (const auto& occ : li.occupants) {
            const auto cn = CanonicalName(occ);
            if (!cn.empty()) seated.insert(cn);
        }

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    if (!current.empty()) {
        AppendMenuW(menu, MF_STRING, kIdRemove, (L"Remove " + current).c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    int added = 0;
    for (size_t r = 0; r < state_.roster.size() && r < 400; ++r) {
        const std::wstring& nm = state_.roster[r];
        if (nm.empty()) continue;
        const auto cn = CanonicalName(nm);
        const bool elsewhere = seated.count(cn) && cn != curCanon;
        std::wstring text = nm;
        if (elsewhere) text += L"  (seated)";
        AppendMenuW(menu, MF_STRING | (cn == curCanon ? MF_CHECKED : 0u),
                    kIdRosterBase + r, text.c_str());
        ++added;
    }
    if (added == 0 && current.empty())
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"(load a roster to assign names)");

    const int id = static_cast<int>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenAnchor.x, screenAnchor.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    if (id <= 0) return;

    if (id == static_cast<int>(kIdRemove)) {
        state_.ClearStudentFromSeat(seat);
        SetStatus(L"Seat cleared");
    } else if (id >= static_cast<int>(kIdRosterBase)) {
        const size_t r = static_cast<size_t>(id - static_cast<int>(kIdRosterBase));
        if (r >= state_.roster.size()) return;
        const std::wstring name = state_.roster[r];
        state_.AssignStudentToSeatExclusive(seat, name);
        SetStatus(L"Assigned " + name);
    } else {
        return;
    }
    InvalidateChart();
    ScheduleSave();
}

// ---------------------------------------------------------------------------
// NOTE: Item creation/duplication, z-ordering, transforms (rotate/flip/lock/
// align/nudge), room sizing, template integration, and all interactive
// drag/resize logic now live in LayoutEditor. Selection mutation, hit testing
// and rubber-band logic now live in SelectionManager. The controller delegates
// to editor_ / selection_ from its message handlers below.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Hover
// ---------------------------------------------------------------------------

void SeatingChartApp::UpdateHover(POINT screenPt) {
    if (!PtInRectEx(layout_.chart, screenPt)) {
        if (hoverItem_ >= 0) {
            // Item is in room coords; approximate invalidation with full chart.
            InvalidateChart(); hoverItem_ = -1;
        }
        return;
    }
    if (!trackingMouse_) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme); trackingMouse_ = true;
    }
    // One chart now: hover the furniture item under the cursor in both tools.
    const int nh = selection_.HitTestItem(screenPt);
    if (nh != hoverItem_) { InvalidateChart(); hoverItem_ = nh; }
}

// ---------------------------------------------------------------------------
// Rubber-band — controller tracks the band (for painting) and hands the final
// screen rect to SelectionManager to compute the new selection.
// ---------------------------------------------------------------------------

void SeatingChartApp::EndRubberBand(POINT endPt) {
    rubberBandEnd_ = endPt;
    rubberBandSelecting_ = false;
    if (selection_.FinalizeRubberBand(rubberBandStart_, rubberBandEnd_) < 0) {
        // Band too small — treated as a click; just clear the overlay.
        InvalidateChart();
        return;
    }
    InvalidateChart();
}

// ---------------------------------------------------------------------------
// Mode / grid
// ---------------------------------------------------------------------------

void SeatingChartApp::SetChartMode(ChartMode mode) {
    state_.chartMode = mode;
    editor_.ClearDragState();
    rubberBandSelecting_ = false;
    if (mode == ChartMode::Seats) {
        // Assign tool: drop furniture selection so edit handles aren't shown.
        selection_.ClearSelection();
        SetStatus(L"Assign · double-click a seat to place a student");
    } else {
        // Arrange tool: drop any focused seat highlight.
        state_.selectedLayoutSeat = std::nullopt;
        SetStatus(L"Arrange · add furniture, drag to move, drag handles to resize/rotate");
    }
    UpdateButtonState(state_, controls_, aaRunning_);
    sidebar_.Recalculate(controls_.sidebar, state_, controls_, renderer_);
    InvalidateChart();
}

void SeatingChartApp::PerformUndo() {
    state_.Undo();
    FullUIRefresh(); SetStatus(L"Undo");
    RecomputeLayoutTransform();
    ScheduleAutoSave(&state_, hwnd_);
}

void SeatingChartApp::PerformRedo() {
    state_.Redo();
    FullUIRefresh(); SetStatus(L"Redo");
    RecomputeLayoutTransform();
    ScheduleAutoSave(&state_, hwnd_);
}

void SeatingChartApp::FullUIRefresh() {
    RefreshSelectionFlags();
    SyncAllEditsFromState();
    UpdateButtonState(state_, controls_, aaRunning_);
    sidebar_.Recalculate(controls_.sidebar, state_, controls_, renderer_);
    InvalidateChart();
}

void SeatingChartApp::SyncAllEditsFromState() {
    RefreshFilteredRosterList();
    SyncRosterView(state_, controls_);
    SyncRulesLists(state_, controls_);
    RefreshGroupConfigList();
    SyncGroupsOutput();
    SyncRosterEditFromRoster(state_, controls_);
    SyncRestrictionEditFromRules(state_, controls_);
    SyncGroupRulesEditFromState(state_, controls_);
    SyncLayoutInspectorWithSelection(state_, controls_);
}

void SeatingChartApp::BuildMenuBar() {
    HMENU menu = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU arrange = CreatePopupMenu();
    HMENU presets = CreatePopupMenu();
    HMENU extra = CreatePopupMenu();
    if (!menu || !file || !arrange || !presets || !extra) return;

    AppendMenuW(file, MF_STRING, kCaptureChartId, L"Capture");
    AppendMenuW(file, MF_STRING, kExportChartId, L"Export Image...");
    AppendMenuW(file, MF_STRING, kPrintChartId, L"Print...");
    AppendMenuW(file, MF_STRING, kExportCsvId, L"Export CSV...");
    AppendMenuW(file, MF_STRING, kExportHtmlId, L"Export HTML...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, kSaveNowId, L"Save Now");

    AppendMenuW(arrange, MF_STRING, kLayoutModeId, L"Enter Arrange Mode");
    AppendMenuW(arrange, MF_STRING, kSeatModeId, L"Enter Assign Mode");
    AppendMenuW(arrange, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(arrange, MF_STRING, kSaveTemplateId, L"Save Template...");
    AppendMenuW(arrange, MF_STRING, kLoadTemplateId, L"Load Template...");
    AppendMenuW(arrange, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(arrange, MF_STRING, kApplyRoomSizeId, L"Change Room Size...");

    AppendMenuW(presets, MF_STRING, kPresetRowsId, L"Rows");
    AppendMenuW(presets, MF_STRING, kPresetUId, L"U");
    AppendMenuW(presets, MF_STRING, kPresetHorseshoeId, L"Horseshoe");

    AppendMenuW(extra, MF_STRING, kShowAlignmentToolsId, L"Alignment Tools");
    AppendMenuW(extra, MF_STRING, kShowObjectInspectorId, L"Object Inspector");

    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"File");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(arrange), L"Arrange");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(presets), L"Presets");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(extra), L"Extra Windows");
    SetMenu(hwnd_, menu);
}

void SeatingChartApp::LayoutFloatingTools() {
    const int pad = Scale(12), gap = Scale(8), bh = Scale(28);
    auto updateScroll = [&](HWND parent, int contentH) {
        RECT rc{}; GetClientRect(parent, &rc);
        const int viewH = std::max(1, static_cast<int>(rc.bottom - rc.top));
        SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0,
                      std::max(0, contentH - 1),
                      static_cast<UINT>(viewH), 0};
        SCROLLINFO cur{sizeof(cur), SIF_POS};
        GetScrollInfo(parent, SB_VERT, &cur);
        si.nPos = std::clamp(cur.nPos, 0, std::max(0, contentH - viewH));
        SetScrollInfo(parent, SB_VERT, &si, TRUE);
        return si.nPos;
    };
    auto placeButtons = [&](HWND parent, std::initializer_list<HWND> hs, int& y) {
        RECT rc{}; GetClientRect(parent, &rc);
        const int scroll = updateScroll(parent, y + static_cast<int>(hs.size()) / 2 * (bh + gap) + pad);
        const int w = std::max(1, static_cast<int>(rc.right - rc.left) - pad * 2 - GetSystemMetrics(SM_CXVSCROLL));
        const int cw = std::max(1, (w - gap) / 2);
        int col = 0;
        for (HWND h : hs) {
            if (!h) continue;
            SetParent(h, parent);
            ShowWindow(h, SW_SHOW);
            MoveWindow(h, pad + col * (cw + gap), y - scroll, col == 0 ? cw : w - cw - gap, bh, TRUE);
            if (++col == 2) { col = 0; y += bh + gap; }
        }
        if (col) y += bh + gap;
        updateScroll(parent, y + pad);
    };

    if (alignmentToolsWindow_ && IsWindowVisible(alignmentToolsWindow_)
            && !IsIconic(alignmentToolsWindow_)) {
        int y = pad;
        placeButtons(alignmentToolsWindow_,
            {controls_.alignLeft, controls_.alignRight, controls_.alignTop, controls_.alignBottom,
             controls_.alignCenterH, controls_.alignCenterV, controls_.distributeH, controls_.distributeV}, y);
    }

    if (objectInspectorWindow_ && IsWindowVisible(objectInspectorWindow_)
            && !IsIconic(objectInspectorWindow_)) {
        RECT rc{}; GetClientRect(objectInspectorWindow_, &rc);
        int contentH = pad + 7 * (bh + gap) + pad;
        const int scroll = updateScroll(objectInspectorWindow_, contentH);
        const int w = std::max(1, static_cast<int>(rc.right - rc.left) - pad * 2 - GetSystemMetrics(SM_CXVSCROLL));
        const int spinW = Scale(20);
        int y = pad;

        // Row with label + edit only (no spinner)
        auto row = [&](HWND label, HWND edit) {
            if (!label || !edit) return;
            SetParent(label, objectInspectorWindow_);
            SetParent(edit, objectInspectorWindow_);
            ShowWindow(label, SW_SHOW);
            ShowWindow(edit, SW_SHOW);
            MoveWindow(label, pad, y - scroll, Scale(54), bh, TRUE);
            MoveWindow(edit, pad + Scale(62), y - scroll, std::max(40, w - Scale(62)), bh, TRUE);
            y += bh + gap;
        };

        // Row with label + edit + spinner arrows (X/Y/W/H); spinner auto-applies on click
        auto rowWithSpin = [&](HWND label, HWND edit, HWND spin) {
            if (!label || !edit) return;
            const int editW = std::max(20, w - Scale(62) - spinW);
            SetParent(label, objectInspectorWindow_);
            SetParent(edit,  objectInspectorWindow_);
            if (spin) SetParent(spin, objectInspectorWindow_);
            ShowWindow(label, SW_SHOW);
            ShowWindow(edit,  SW_SHOW);
            if (spin) ShowWindow(spin, SW_SHOW);
            MoveWindow(label, pad,                         y - scroll, Scale(54), bh, TRUE);
            MoveWindow(edit,  pad + Scale(62),             y - scroll, editW,     bh, TRUE);
            if (spin) MoveWindow(spin, pad + Scale(62) + editW, y - scroll, spinW, bh, TRUE);
            y += bh + gap;
        };

        row(controls_.layoutNameLabel, controls_.layoutLabelEdit);
        rowWithSpin(controls_.layoutXLabel, controls_.layoutXEdit, controls_.layoutXSpin);
        rowWithSpin(controls_.layoutYLabel, controls_.layoutYEdit, controls_.layoutYSpin);
        rowWithSpin(controls_.layoutWidthLabel,  controls_.layoutWidthEdit,  controls_.layoutWSpin);
        rowWithSpin(controls_.layoutHeightLabel, controls_.layoutHeightEdit, controls_.layoutHSpin);
        row(controls_.layoutCapacityLabel, controls_.layoutCapacityEdit);
        SetParent(controls_.applyLayoutItem, objectInspectorWindow_);
        ShowWindow(controls_.applyLayoutItem, SW_SHOW);
        MoveWindow(controls_.applyLayoutItem, pad, y - scroll, w, bh, TRUE);
        updateScroll(objectInspectorWindow_, contentH);
    }
}

void SeatingChartApp::ShowAlignmentToolsWindow() {
    RegisterToolWindowClass();
    if (!alignmentToolsWindow_) {
        alignmentToolsWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW, L"SeatingChartFloatingTools",
            L"Alignment Tools", WS_OVERLAPPEDWINDOW | WS_VSCROLL,
            CW_USEDEFAULT, CW_USEDEFAULT, Scale(280), Scale(180),
            hwnd_, nullptr, GetModuleHandleW(nullptr), hwnd_);
    }
    ShowWindow(alignmentToolsWindow_, IsIconic(alignmentToolsWindow_) ? SW_RESTORE : SW_SHOW);
    LayoutFloatingTools();
}

void SeatingChartApp::ShowObjectInspectorWindow() {
    RegisterToolWindowClass();
    if (!objectInspectorWindow_) {
        objectInspectorWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW, L"SeatingChartFloatingTools",
            L"Object Inspector", WS_OVERLAPPEDWINDOW | WS_VSCROLL,
            CW_USEDEFAULT, CW_USEDEFAULT, Scale(300), Scale(260),
            hwnd_, nullptr, GetModuleHandleW(nullptr), hwnd_);
    }
    SyncLayoutInspectorWithSelection(state_, controls_);
    ShowWindow(objectInspectorWindow_, IsIconic(objectInspectorWindow_) ? SW_RESTORE : SW_SHOW);
    LayoutFloatingTools();
}

bool SeatingChartApp::HitSidebarSplitter(POINT pt) const {
    RECT r{layout_.panel.left - Scale(kSidebarSplitterWidth), layout_.panel.top,
           layout_.panel.left + Scale(kSidebarSplitterWidth), layout_.panel.bottom};
    return PtInRectEx(r, pt);
}

void SeatingChartApp::ResizeSidebarLive(POINT pt) {
    RECT rc{}; GetClientRect(hwnd_, &rc);
    const int cw = std::max(1, static_cast<int>(rc.right - rc.left));
    const int ch = std::max(1, static_cast<int>(rc.bottom - rc.top));
    const int stripW = classListBox_ ? Scale(kClassStripW) : 0;
    const int maxW = std::min(Scale(kSidebarMaxWidth), std::max(Scale(kSidebarMinWidth), cw - stripW - Margin() - Scale(220)));
    // Panel is on the right; dragging the LEFT edge: newWidth = distance from pt.x to right boundary
    const int newWidth = std::clamp(cw - stripW - static_cast<int>(pt.x),
                                    Scale(kSidebarMinWidth), Scale(kSidebarMaxWidth));
    sidebarWidth_ = std::min(newWidth, maxW);

    layout_.client = rc;
    layout_.panel = { cw - stripW - sidebarWidth_, 0, cw - stripW, ch };
    layout_.chart = { Margin(), HeaderHeight(),
                      std::max(Margin() + Scale(40), cw - stripW - sidebarWidth_ - Margin()),
                      std::max(HeaderHeight() + Scale(40), ch - Margin()) };
    layoutTx_ = ComputeLayoutViewTransform(layout_.chart, state_.roomW, state_.roomH);

    if (controls_.sidebar) {
        SetWindowPos(controls_.sidebar, nullptr,
                     layout_.panel.left, layout_.panel.top,
                     layout_.panel.right - layout_.panel.left,
                     layout_.panel.bottom - layout_.panel.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}

void SeatingChartApp::PromptAndApplyRoomSize() {
    struct PromptState {
        HWND parent{};
        HWND wEdit{};
        HWND hEdit{};
        int width{};
        int height{};
        bool accepted = false;
    } ps{hwnd_};

    const wchar_t* cls = L"SeatingChartRoomSizePrompt";
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
            auto* s = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (msg == WM_NCCREATE) {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
                s = reinterpret_cast<PromptState*>(cs->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
            }
            switch (msg) {
            case WM_CREATE: {
                // All coordinates scaled for DPI — dialog outer size is Scale(224)×Scale(148).
                auto makeLabel = [&](const wchar_t* text, int x, int y) {
                    CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                        Scale(x), Scale(y), Scale(68), Scale(20),
                        hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
                };
                makeLabel(L"Width",  12, 15);
                makeLabel(L"Height", 12, 47);
                s->wEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                    Scale(84), Scale(13), Scale(116), Scale(24),
                    hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
                s->hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                    Scale(84), Scale(45), Scale(116), Scale(24),
                    hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
                SetWindowTextW(s->wEdit, std::to_wstring(s->width).c_str());
                SetWindowTextW(s->hEdit, std::to_wstring(s->height).c_str());
                CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                    Scale(36), Scale(80), Scale(72), Scale(28),
                    hwnd, reinterpret_cast<HMENU>(IDOK), GetModuleHandleW(nullptr), nullptr);
                CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE,
                    Scale(116), Scale(80), Scale(72), Scale(28),
                    hwnd, reinterpret_cast<HMENU>(IDCANCEL), GetModuleHandleW(nullptr), nullptr);
                return 0;
            }
            case WM_COMMAND:
                if (LOWORD(wParam) == IDOK) {
                    int w = 0, h = 0;
                    const bool parsedW = ParseIntStrict(GetWindowTextStr(s->wEdit), 0, 100000, &w);
                    const bool parsedH = ParseIntStrict(GetWindowTextStr(s->hEdit), 0, 100000, &h);
                    (void)parsedW;
                    (void)parsedH;
                    s->width = w;
                    s->height = h;
                    s->accepted = true;
                    DestroyWindow(hwnd);
                    return 0;
                }
                if (LOWORD(wParam) == IDCANCEL) {
                    DestroyWindow(hwnd);
                    return 0;
                }
                break;
            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        };
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = cls;
        RegisterClassW(&wc);
        registered = true;
    }

    ps.width = state_.roomW > 0 ? state_.roomW : kDefaultRoomW;
    ps.height = state_.roomH > 0 ? state_.roomH : kDefaultRoomH;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, cls, L"Change Room Size",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
        Scale(224), Scale(148), hwnd_, nullptr, GetModuleHandleW(nullptr), &ps);
    if (!dlg) return;

    RECT pr{}, dr{};
    GetWindowRect(hwnd_, &pr);
    GetWindowRect(dlg, &dr);
    SetWindowPos(dlg, HWND_TOP, pr.left + ((pr.right - pr.left) - (dr.right - dr.left)) / 2,
        pr.top + ((pr.bottom - pr.top) - (dr.bottom - dr.top)) / 2, 0, 0, SWP_NOSIZE);
    EnableWindow(hwnd_, FALSE);
    ShowWindow(dlg, SW_SHOW);
    MSG msg{};
    while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(hwnd_, TRUE);
    SetActiveWindow(hwnd_);

    if (!ps.accepted) {
        SetStatus(L"Room size unchanged");
        return;
    }
    AppState::Transaction tx(state_);
    tx->roomW = ps.width;
    tx->roomH = ps.height;
    tx.Commit();
    RecomputeLayoutTransform();
    SyncAllEditsFromState();
    UpdateButtonState(state_, controls_, aaRunning_);
    InvalidateChart();
    ScheduleSave();
    SetStatus(ps.width > 0 && ps.height > 0
        ? L"Room set to " + std::to_wstring(ps.width) + L"x" + std::to_wstring(ps.height) + L" units"
        : L"Room size set to default");
}

// ---------------------------------------------------------------------------
// Front-edge helpers — hit-test, nearest-edge, commit
// ---------------------------------------------------------------------------

RECT SeatingChartApp::FrontIndicatorRect() const {
    const RECT& room  = layoutTx_.roomScreenRect;
    const RoomEdge fe = state_.frontEdge;
    const bool vert   = (fe == RoomEdge::Left || fe == RoomEdge::Right);
    const int  band   = vert ? std::max(Scale(22), 18) : std::max(Scale(20), 16);
    RECT strip = room;
    switch (fe) {
    case RoomEdge::Top:    strip.bottom = room.top    + band; break;
    case RoomEdge::Bottom: strip.top    = room.bottom - band; break;
    case RoomEdge::Left:   strip.right  = room.left   + band; break;
    case RoomEdge::Right:  strip.left   = room.right  - band; break;
    }
    return strip;
}

bool SeatingChartApp::HitTestFrontIndicator(POINT clientPt) const {
    if (!PtInRectEx(layout_.chart, clientPt)) return false;
    return PtInRectEx(FrontIndicatorRect(), clientPt);
}

RoomEdge SeatingChartApp::NearestRoomEdge(POINT clientPt) const {
    const RECT& room  = layoutTx_.roomScreenRect;
    const int dTop    = std::abs(clientPt.y - room.top);
    const int dBottom = std::abs(clientPt.y - room.bottom);
    const int dLeft   = std::abs(clientPt.x - room.left);
    const int dRight  = std::abs(clientPt.x - room.right);
    const int minD    = std::min({dTop, dBottom, dLeft, dRight});
    if (minD == dTop)    return RoomEdge::Top;
    if (minD == dBottom) return RoomEdge::Bottom;
    if (minD == dLeft)   return RoomEdge::Left;
    return RoomEdge::Right;
}

void SeatingChartApp::CommitFrontEdge(RoomEdge edge) {
    AppState::Transaction tx(state_);
    tx->frontEdge = edge;
    tx.Commit();
    static const wchar_t* names[] = { L"Top", L"Bottom", L"Left", L"Right" };
    const int idx = (edge == RoomEdge::Top)    ? 0 :
                    (edge == RoomEdge::Bottom)  ? 1 :
                    (edge == RoomEdge::Left)    ? 2 : 3;
    SetStatus(std::wstring(L"Front of room: ") + names[idx]);
    ScheduleSave();
    InvalidateChart();
}

// ---------------------------------------------------------------------------
// Class management
// ---------------------------------------------------------------------------

std::wstring SeatingChartApp::GetClassFilePath(int idx) const {
    const auto base = GetAppDataStateDir();
    if (base.empty()) return L"";
    return base + L"\\class_" + std::to_wstring(idx) + L".json";
}

void SeatingChartApp::InitClassList() {
    // Try to load classes.json; if not found, build a default one-class list
    const auto base = GetAppDataStateDir();
    const auto manifest = base.empty() ? L"" : base + L"\\classes.json";
    classList_.clear();
    activeClassIdx_ = 0;

    if (!manifest.empty() && GetFileAttributesW(manifest.c_str()) != INVALID_FILE_ATTRIBUTES) {
        try {
            std::ifstream f(WideToUtf8(manifest));
            if (f.is_open()) {
                const std::string text((std::istreambuf_iterator<char>(f)),
                                       std::istreambuf_iterator<char>());
                nlohmann::json j = nlohmann::json::parse(text);
                for (auto& entry : j.value("classes", nlohmann::json::array())) {
                    ClassInfo ci;
                    ci.name = Utf8ToWide(entry.value("name", std::string("Class 1")));
                    ci.file = Utf8ToWide(entry.value("file", std::string("class_0.json")));
                    classList_.push_back(std::move(ci));
                }
                activeClassIdx_ = j.value("active", 0);
                if (activeClassIdx_ < 0 || activeClassIdx_ >= static_cast<int>(classList_.size()))
                    activeClassIdx_ = 0;
            }
        } catch (...) {}
    }

    if (classList_.empty()) {
        classList_.push_back({ state_.className.empty() ? L"Class 1" : state_.className,
                               L"class_0.json" });
        activeClassIdx_ = 0;
        SaveClassList();
    }
}

void SeatingChartApp::SaveClassList() {
    const auto base = GetAppDataStateDir();
    if (base.empty()) return;
    (void)EnsureDirectoryExists(base);
    try {
        nlohmann::json j;
        nlohmann::json arr = nlohmann::json::array();
        for (auto& ci : classList_) {
            nlohmann::json e;
            e["name"] = WideToUtf8(ci.name);
            e["file"] = WideToUtf8(ci.file);
            arr.push_back(e);
        }
        j["classes"] = arr;
        j["active"]  = activeClassIdx_;
        const auto path = base + L"\\classes.json";
        std::ofstream f(WideToUtf8(path));
        if (f.is_open()) f << j.dump(2);
    } catch (...) {}
}

void SeatingChartApp::SyncClassListBox() {
    if (!classListBox_) return;
    SendMessageW(classListBox_, LB_RESETCONTENT, 0, 0);
    for (auto& ci : classList_)
        SendMessageW(classListBox_, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(ci.name.c_str()));
    if (activeClassIdx_ >= 0 && activeClassIdx_ < static_cast<int>(classList_.size()))
        SendMessageW(classListBox_, LB_SETCURSEL, activeClassIdx_, 0);
}

void SeatingChartApp::SwitchToClass(int idx) {
    if (idx == activeClassIdx_ || idx < 0 || idx >= static_cast<int>(classList_.size())) return;
    // Dismiss any active inline cell edit before switching — prevents focus battle
    CancelInlineCellEdit();
    // Save current class
    SaveStateNow(&state_, false);
    const auto curFile = GetClassFilePath(activeClassIdx_);
    if (!curFile.empty()) {
        // Copy the canonical state file to the per-class file
        const auto stateFile = GetStateFilePath();
        if (!stateFile.empty()) {
            std::ifstream src(WideToUtf8(stateFile), std::ios::binary);
            std::ofstream dst(WideToUtf8(curFile),   std::ios::binary);
            if (src && dst) dst << src.rdbuf();
        }
    }
    // Load the new class
    activeClassIdx_ = idx;
    state_.className = classList_[idx].name;
    const auto newFile = GetClassFilePath(idx);
    if (!newFile.empty() && GetFileAttributesW(newFile.c_str()) != INVALID_FILE_ATTRIBUTES) {
        state_.Init();
        std::vector<unsigned char> bytes;
        if (ReadAllBytes(newFile, &bytes) && !bytes.empty()) {
            const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            (void)LoadStateFromJson(text, &state_);
        }
        state_.ClearUndoHistory();
    } else {
        // New/fresh class
        state_.Init();
        state_.className = classList_[idx].name;
    }
    SaveClassList();
    SyncAllEditsFromState();
    SyncRosterView(state_, controls_);
    SyncRulesLists(state_, controls_);
    RefreshSelectionFlags();
    UpdateButtonState(state_, controls_, aaRunning_);
    generatedGroups_.clear();   // stale groups belong to the old class
    SyncGroupsOutput();
    SyncClassListBox();
    InvalidateChart();
    SetStatus(L"Switched to " + classList_[idx].name);
}

void SeatingChartApp::NewClass() {
    const int idx = static_cast<int>(classList_.size());
    const std::wstring name = L"Class " + std::to_wstring(idx + 1);
    classList_.push_back({ name, L"class_" + std::to_wstring(idx) + L".json" });
    SaveClassList();
    SwitchToClass(idx);
}

void SeatingChartApp::RenameClass(int idx, const std::wstring& name) {
    if (idx < 0 || idx >= static_cast<int>(classList_.size())) return;
    const std::wstring trimmed = TrimCopy(name);
    if (trimmed.empty()) return;
    classList_[static_cast<size_t>(idx)].name = trimmed;
    if (idx == activeClassIdx_) {
        state_.className = trimmed;
    }
    SaveClassList();
    SyncClassListBox();
    SetStatus(L"Renamed class to " + trimmed);
}

// ---------------------------------------------------------------------------
// Roster: add / edit / remove student
// ---------------------------------------------------------------------------

bool SeatingChartApp::PromptStudentName(const std::wstring& title,
                                         std::wstring& first, std::wstring& last) {
    struct Dlg { HWND parent; HWND eFirst, eLast; bool ok;
                 std::wstring initFirst, initLast; } d{hwnd_, {}, {}, false, first, last};
    const wchar_t* cls = L"SeatingChartStudentDlg";
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = [](HWND hw, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
            auto* s = reinterpret_cast<Dlg*>(GetWindowLongPtrW(hw, GWLP_USERDATA));
            if (msg == WM_NCCREATE) {
                s = reinterpret_cast<Dlg*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
                SetWindowLongPtrW(hw, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
            }
            switch (msg) {
            case WM_CREATE: {
                auto lbl = [&](const wchar_t* t, int y){
                    CreateWindowExW(0,L"STATIC",t,WS_CHILD|WS_VISIBLE,
                        Scale(12),y,Scale(70),Scale(20),hw,nullptr,GetModuleHandleW(nullptr),nullptr);};
                lbl(L"First Name:", Scale(14));
                lbl(L"Last Name:",  Scale(48));
                s->eFirst = CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",
                    WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
                    Scale(86),Scale(12),Scale(140),Scale(24),hw,nullptr,GetModuleHandleW(nullptr),nullptr);
                s->eLast  = CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",
                    WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
                    Scale(86),Scale(46),Scale(140),Scale(24),hw,nullptr,GetModuleHandleW(nullptr),nullptr);
                SetWindowTextW(s->eFirst, s->initFirst.c_str());
                SetWindowTextW(s->eLast,  s->initLast.c_str());
                CreateWindowExW(0,L"BUTTON",L"OK",WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,
                    Scale(36),Scale(82),Scale(76),Scale(28),hw,(HMENU)IDOK,GetModuleHandleW(nullptr),nullptr);
                CreateWindowExW(0,L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE,
                    Scale(120),Scale(82),Scale(76),Scale(28),hw,(HMENU)IDCANCEL,GetModuleHandleW(nullptr),nullptr);
                return 0; }
            case WM_COMMAND:
                if (LOWORD(wp) == IDOK) {
                    wchar_t buf[128]{};
                    GetWindowTextW(s->eFirst, buf, 128); s->initFirst = buf;
                    GetWindowTextW(s->eLast,  buf, 128); s->initLast  = buf;
                    s->ok = true; DestroyWindow(hw); return 0;
                }
                if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hw); return 0; }
                break;
            case WM_CLOSE: DestroyWindow(hw); return 0;
            }
            return DefWindowProcW(hw, msg, wp, lp);
        };
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = cls;
        RegisterClassW(&wc); reg = true;
    }
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, cls, title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, Scale(244), Scale(144),
        hwnd_, nullptr, GetModuleHandleW(nullptr), &d);
    if (!dlg) return false;
    RECT pr{}, dr{};
    GetWindowRect(hwnd_, &pr); GetWindowRect(dlg, &dr);
    SetWindowPos(dlg, HWND_TOP,
        pr.left + ((pr.right-pr.left)-(dr.right-dr.left))/2,
        pr.top  + ((pr.bottom-pr.top)-(dr.bottom-dr.top))/2, 0, 0, SWP_NOSIZE);
    EnableWindow(hwnd_, FALSE);
    ShowWindow(dlg, SW_SHOW);
    MSG m{}; while (IsWindow(dlg) && GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m); DispatchMessageW(&m); }
    EnableWindow(hwnd_, TRUE); SetActiveWindow(hwnd_);
    if (!d.ok) return false;
    first = d.initFirst; last = d.initLast;
    return true;
}

bool SeatingChartApp::PromptRulePair(const std::wstring& title,
                                      std::wstring& nameA, std::wstring& nameB) {
    // Reuse PromptStudentName as a two-field "Name A / Name B" dialog
    return PromptStudentName(title, nameA, nameB);
}

bool SeatingChartApp::PromptSingleText(const std::wstring& title,
                                       const std::wstring& label,
                                       std::wstring& value) {
    struct Dlg {
        HWND parent;
        HWND edit;
        bool ok;
        std::wstring init;
        std::wstring label;
    } d{ hwnd_, {}, false, value, label };
    const wchar_t* cls = L"SeatingChartSingleTextDlg";
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = [](HWND hw, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
            auto* s = reinterpret_cast<Dlg*>(GetWindowLongPtrW(hw, GWLP_USERDATA));
            if (msg == WM_NCCREATE) {
                s = reinterpret_cast<Dlg*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
                SetWindowLongPtrW(hw, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
            }
            switch (msg) {
            case WM_CREATE: {
                CreateWindowExW(0, L"STATIC", s->label.c_str(), WS_CHILD | WS_VISIBLE,
                    Scale(12), Scale(14), Scale(200), Scale(20), hw, nullptr,
                    GetModuleHandleW(nullptr), nullptr);
                s->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                    Scale(12), Scale(40), Scale(220), Scale(24), hw, nullptr,
                    GetModuleHandleW(nullptr), nullptr);
                SetWindowTextW(s->edit, s->init.c_str());
                CreateWindowExW(0, L"BUTTON", L"OK",
                    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                    Scale(36), Scale(78), Scale(76), Scale(28), hw,
                    reinterpret_cast<HMENU>(IDOK), GetModuleHandleW(nullptr), nullptr);
                CreateWindowExW(0, L"BUTTON", L"Cancel",
                    WS_CHILD | WS_VISIBLE,
                    Scale(120), Scale(78), Scale(76), Scale(28), hw,
                    reinterpret_cast<HMENU>(IDCANCEL), GetModuleHandleW(nullptr), nullptr);
                return 0;
            }
            case WM_COMMAND:
                if (LOWORD(wp) == IDOK) {
                    wchar_t buf[256]{};
                    GetWindowTextW(s->edit, buf, 256);
                    s->init = buf;
                    s->ok = true;
                    DestroyWindow(hw);
                    return 0;
                }
                if (LOWORD(wp) == IDCANCEL) {
                    DestroyWindow(hw);
                    return 0;
                }
                break;
            case WM_CLOSE:
                DestroyWindow(hw);
                return 0;
            }
            return DefWindowProcW(hw, msg, wp, lp);
        };
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = cls;
        RegisterClassW(&wc);
        reg = true;
    }
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, cls, title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, Scale(256), Scale(148),
        hwnd_, nullptr, GetModuleHandleW(nullptr), &d);
    if (!dlg) return false;
    RECT pr{}, dr{};
    GetWindowRect(hwnd_, &pr); GetWindowRect(dlg, &dr);
    SetWindowPos(dlg, HWND_TOP,
        pr.left + ((pr.right - pr.left) - (dr.right - dr.left)) / 2,
        pr.top  + ((pr.bottom - pr.top) - (dr.bottom - dr.top)) / 2,
        0, 0, SWP_NOSIZE);
    EnableWindow(hwnd_, FALSE);
    ShowWindow(dlg, SW_SHOW);
    MSG m{};
    while (IsWindow(dlg) && GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    EnableWindow(hwnd_, TRUE);
    SetActiveWindow(hwnd_);
    if (!d.ok) return false;
    value = d.init;
    return true;
}

void SeatingChartApp::ToggleShowLastNames() {
    AppState::Transaction tx(state_);
    tx->showLastNames = !tx->showLastNames;
    tx.Commit();
    SyncAllEditsFromState();
    UpdateButtonState(state_, controls_, aaRunning_);
    InvalidateChart();
    ScheduleSave();
}

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------

void SeatingChartApp::RefreshGroupConfigList() {
    if (!controls_.groupConfigList) return;
    SendMessageW(controls_.groupConfigList, LB_RESETCONTENT, 0, 0);
    const int N = static_cast<int>(state_.roster.size());
    if (N < 4) {
        SendMessageW(controls_.groupConfigList, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(N == 0 ? L"(No students in roster)"
                                                     : L"(Need at least 4 students)"));
        if (controls_.groupSizeEdit) {
            wchar_t buf[16]{};
            GetWindowTextW(controls_.groupSizeEdit, buf, static_cast<int>(std::size(buf)));
            if (wcscmp(buf, L"2") != 0)
                SetWindowTextW(controls_.groupSizeEdit, L"2");
        }
        groupSizePref_ = 2;
        return;
    }

    int bestRow = 0;
    std::vector<int> options;
    const int maxK = (N + 1) / 2;
    for (int k = 2; k <= maxK; ++k) {
        const auto pattern = BuildGroupPattern(N, k);
        if (static_cast<int>(pattern.size()) < 2) continue;
        options.push_back(k);
    }
    if (options.empty()) options.push_back(2);

    for (int i = 0; i < static_cast<int>(options.size()); ++i) {
        const int base = options[static_cast<size_t>(i)];
        const auto pattern = BuildGroupPattern(N, base);
        const std::wstring line = L"Groups of " + std::to_wstring(base) +
                                  L"  —  " + FormatGroupPattern(pattern);
        const int idx = static_cast<int>(SendMessageW(
            controls_.groupConfigList, LB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(line.c_str())));
        if (idx >= 0) {
            SendMessageW(controls_.groupConfigList, LB_SETITEMDATA, idx, static_cast<LPARAM>(base));
        }
        if (base == groupSizePref_) bestRow = i;
    }
    if (bestRow >= static_cast<int>(options.size())) bestRow = 0;
    groupSizePref_ = options[static_cast<size_t>(bestRow)];
    SendMessageW(controls_.groupConfigList, LB_SETCURSEL, bestRow, 0);
    // Also refresh the new combobox-based group size selector
    RefreshGroupCombo();
}

void SeatingChartApp::ShuffleGroups() {
    const int N = static_cast<int>(state_.roster.size());
    if (N == 0) { generatedGroups_.clear(); SyncGroupsOutput(); return; }

    // Get base size from the combo (fallback to groupSizePref_)
    int base = groupSizePref_;
    if (controls_.groupSizeCombo) {
        const int sel = static_cast<int>(SendMessageW(controls_.groupSizeCombo, CB_GETCURSEL, 0, 0));
        if (sel >= 0) {
            wchar_t buf[16]{};
            SendMessageW(controls_.groupSizeCombo, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(buf));
            if (buf[0]) base = _wtoi(buf);
        }
    }
    if (base < 2) base = 2;
    groupSizePref_ = base;

    const std::vector<int> pattern = BuildGroupPattern(N, base);
    if (pattern.empty()) { generatedGroups_.clear(); SyncGroupsOutput(); return; }
    const std::wstring exactBefore = ExactGroupPermutationText();
    if (exactBefore == L"0") {
        MessageBoxW(hwnd_,
            L"No valid grouping remains under the current Groups history rules.\n\n"
            L"Try Reset Shuffle Memory, change the group size, or relax one of the history checkboxes.",
            L"Unable to Shuffle Groups",
            MB_OK | MB_ICONWARNING);
        SetStatus(L"Groups shuffle is blocked by the current history rules");
        RefreshAutoAssignFooter();
        return;
    }

    // Check whether every possible student pair has already been grouped together.
    // When all C(N,2) pairs are in history, every future shuffle will repeat a pairing.
    if (false) {
        bool exhausted = true;
        for (int i = 0; i < N && exhausted; ++i) {
            for (int j = i + 1; j < N && exhausted; ++j) {
                if (!groupPairHistory_.count(GroupPairKey(state_.roster[i], state_.roster[j])))
                    exhausted = false;
            }
        }
        if (exhausted) {
            MessageBoxW(hwnd_,
                L"All unique group pairings have been used.\n\n"
                L"Every student has now been grouped with every other student at least once.\n\n"
                L"Click “Reset Shuffle Memory” to start generating fresh arrangements.",
                L"Shuffle Exhausted",
                MB_OK | MB_ICONINFORMATION);
            return;
        }
    }

    auto scoreGroups = [&](const std::vector<std::vector<std::wstring>>& groups) {
        size_t score = 0;
        for (const auto& grp : groups) {
            for (size_t i = 0; i < grp.size(); ++i) {
                for (size_t j = i + 1; j < grp.size(); ++j) {
                    const auto key = GroupPairKey(grp[i], grp[j]);
                    const auto it = groupPairHistory_.find(key);
                    if (it != groupPairHistory_.end()) score += 1u + static_cast<size_t>(it->second);
                }
            }
        }
        return score;
    };

    std::mt19937 rng(static_cast<unsigned>(GetTickCount64()));
    std::vector<std::vector<std::wstring>> bestGroups;
    size_t bestScore = std::numeric_limits<size_t>::max();

    for (int attempt = 0; attempt < 2000; ++attempt) {
        std::vector<std::wstring> shuffled = state_.roster;
        std::shuffle(shuffled.begin(), shuffled.end(), rng);

        std::vector<std::vector<std::wstring>> groups = PartitionRosterByPattern(shuffled, pattern);
        if (groups.size() != pattern.size() || !CandidateGroupsMeetConstraints(groups))
            continue;
        const size_t score = scoreGroups(groups);
        if (score <= bestScore) {
            bestScore = score;
            bestGroups = std::move(groups);
        }
    }

    if (bestGroups.empty()) {
        MessageBoxW(hwnd_,
            L"No valid grouping was found under the current Groups history rules.\n\n"
            L"Try Reset Shuffle Memory, change the group size, or relax one of the history checkboxes.",
            L"Unable to Shuffle Groups",
            MB_OK | MB_ICONWARNING);
        SetStatus(L"Groups shuffle is blocked by the current history rules");
        RefreshAutoAssignFooter();
        return;
    }

    generatedGroups_ = std::move(bestGroups);
    RecordGroupShuffleHistory(generatedGroups_);
    SyncGroupsOutput();
    RefreshAutoAssignFooter();
    SetStatus(L"Generated " + std::to_wstring(generatedGroups_.size()) +
              L" groups • remaining exact groupings: " + ExactGroupPermutationText());
}

void SeatingChartApp::SyncGroupsOutput() {
    if (!controls_.groupsOutputList) return;
    SendMessageW(controls_.groupsOutputList, LB_RESETCONTENT, 0, 0);
    if (generatedGroups_.empty()) {
        SendMessageW(controls_.groupsOutputList, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Click Shuffle Groups to generate."));
        return;
    }
    for (int i = 0; i < static_cast<int>(generatedGroups_.size()); ++i) {
        const auto& g = generatedGroups_[static_cast<size_t>(i)];
        std::wstring line = L"Group " + std::to_wstring(i + 1) + L":  ";
        for (size_t j = 0; j < g.size(); ++j) {
            if (j) line += L",  ";
            line += DisplayStudentName(g[j], state_.showLastNames);
        }
        SendMessageW(controls_.groupsOutputList, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(line.c_str()));
    }
}

void SeatingChartApp::ResetGroupShuffleMemory() {
    groupPairHistory_.clear();
    groupNumberHistory_.clear();
    groupPartnerSetHistory_.clear();
    groupExactHistory_.clear();
    RefreshAutoAssignFooter();
    SetStatus(L"Group shuffle memory reset — all students can pair again");
}

// RefreshGroupCombo — populates the "Groups of:" combobox with valid base sizes
// and updates the "or N" label showing the overflow group size.
void SeatingChartApp::RefreshGroupCombo() {
    if (!controls_.groupSizeCombo) return;
    const int N = static_cast<int>(state_.roster.size());

    // Preserve current selection before repopulating
    wchar_t curText[16]{};
    const int curSel = static_cast<int>(SendMessageW(controls_.groupSizeCombo, CB_GETCURSEL, 0, 0));
    if (curSel >= 0) SendMessageW(controls_.groupSizeCombo, CB_GETLBTEXT, curSel, reinterpret_cast<LPARAM>(curText));
    int preservedK = curText[0] ? _wtoi(curText) : groupSizePref_;

    SendMessageW(controls_.groupSizeCombo, CB_RESETCONTENT, 0, 0);
    if (N < 4) {
        // Need at least 4 students to form 2 groups of 2
        SendMessageW(controls_.groupSizeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"—"));
        SendMessageW(controls_.groupSizeCombo, CB_SETCURSEL, 0, 0);
        if (controls_.groupOrValLabel) SetWindowTextW(controls_.groupOrValLabel, L"");
        return;
    }

    // Offer all k where BuildGroupPattern gives at least 2 groups.
    // Max k = ceil(N/2) = (N+1)/2: ensures at least 2 groups.
    const int maxK = (N + 1) / 2;
    int selIdx = 0;
    for (int k = 2; k <= maxK; ++k) {
        const auto pat = BuildGroupPattern(N, k);
        if (static_cast<int>(pat.size()) < 2) continue;
        const std::wstring txt = std::to_wstring(k);
        const int idx = static_cast<int>(
            SendMessageW(controls_.groupSizeCombo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(txt.c_str())));
        if (k == preservedK) selIdx = idx;
    }
    if (SendMessageW(controls_.groupSizeCombo, CB_GETCOUNT, 0, 0) == 0) {
        SendMessageW(controls_.groupSizeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"2"));
    }
    SendMessageW(controls_.groupSizeCombo, CB_SETCURSEL, selIdx, 0);

    // Derive the selected k and update the "or N" label
    wchar_t buf[16]{};
    SendMessageW(controls_.groupSizeCombo, CB_GETLBTEXT, selIdx, reinterpret_cast<LPARAM>(buf));
    const int k = buf[0] ? _wtoi(buf) : 2;
    groupSizePref_ = k;

    const auto pat = BuildGroupPattern(N, k);
    if (controls_.groupOrValLabel) {
        if (pat.empty()) {
            SetWindowTextW(controls_.groupOrValLabel, L"—");
        } else {
            const int minSz = *std::min_element(pat.begin(), pat.end());
            const int maxSz = *std::max_element(pat.begin(), pat.end());
            if (minSz == maxSz)
                SetWindowTextW(controls_.groupOrValLabel, L"—");
            else
                SetWindowTextW(controls_.groupOrValLabel, std::to_wstring(maxSz).c_str());
        }
    }
}

void SeatingChartApp::RefreshGroupRuleToggleLabels() {
    if (controls_.groupKeepApartToggle) {
        SetWindowTextW(controls_.groupKeepApartToggle,
            sidebar_.GroupKeepApartCollapsed() ? L"Show Keep Apart Rules"
                                               : L"Hide Keep Apart Rules");
    }
    if (controls_.groupKeepTogetherToggle) {
        SetWindowTextW(controls_.groupKeepTogetherToggle,
            sidebar_.GroupKeepTogetherCollapsed() ? L"Show Keep Together Rules"
                                                  : L"Hide Keep Together Rules");
    }
}

bool SeatingChartApp::GroupAvoidSameNumberEnabled() const {
    return controls_.groupAvoidSameNumberCheck &&
           SendMessageW(controls_.groupAvoidSameNumberCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

bool SeatingChartApp::GroupAvoidSamePartnersEnabled() const {
    return controls_.groupAvoidSamePartnersCheck &&
           SendMessageW(controls_.groupAvoidSamePartnersCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

bool SeatingChartApp::GroupAvoidSameFullGroupEnabled() const {
    return controls_.groupAvoidSameFullGroupCheck &&
           SendMessageW(controls_.groupAvoidSameFullGroupCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

std::vector<int> SeatingChartApp::CurrentGroupPattern() const {
    const int N = static_cast<int>(state_.roster.size());
    if (N == 0) return {};

    int base = std::max(2, groupSizePref_);
    if (controls_.groupSizeCombo) {
        const int sel = static_cast<int>(SendMessageW(controls_.groupSizeCombo, CB_GETCURSEL, 0, 0));
        if (sel >= 0) {
            wchar_t buf[16]{};
            SendMessageW(controls_.groupSizeCombo, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(buf));
            if (buf[0]) base = std::max(2, _wtoi(buf));
        }
    }
    return BuildGroupPattern(N, base);
}

std::wstring SeatingChartApp::CanonicalGroupKey(const std::vector<std::wstring>& group) const {
    std::vector<std::wstring> names;
    names.reserve(group.size());
    for (const auto& name : group) names.push_back(CanonicalName(name));
    std::sort(names.begin(), names.end());
    std::wstring key;
    for (const auto& name : names) {
        if (!key.empty()) key += L"|";
        key += name;
    }
    return key;
}

std::wstring SeatingChartApp::BuildGroupsSummaryText(const std::wstring& exactText) const {
    const int studentCount = static_cast<int>(state_.roster.size());
    const auto pattern = CurrentGroupPattern();
    if (studentCount == 0) return L"Add students to the roster to generate groups.";
    if (pattern.empty()) return L"Need at least 4 students to form 2 or more groups.";

    size_t numberedLocks = 0;
    for (const auto& [name, groups] : groupNumberHistory_) {
        (void)name;
        numberedLocks += groups.size();
    }
    size_t partnerSets = 0;
    for (const auto& [name, histories] : groupPartnerSetHistory_) {
        (void)name;
        partnerSets += histories.size();
    }
    const size_t exactGroups = groupExactHistory_.size();

    std::wstring line1 = L"Pattern: " + FormatGroupPattern(pattern);
    std::wstring line2 = L"History: ";
    line2 += GroupAvoidSameNumberEnabled() ? L"No same group number" : L"Group number repeats allowed";
    line2 += L" • ";
    line2 += GroupAvoidSamePartnersEnabled() ? L"No 2+ repeated classmates" : L"Repeated classmates allowed";
    line2 += L" • ";
    line2 += GroupAvoidSameFullGroupEnabled() ? L"No exact full-group repeats" : L"Full-group repeats allowed";
    line2 += L" • Locks: " + std::to_wstring(numberedLocks);
    line2 += L" • Partner sets: " + std::to_wstring(partnerSets);
    line2 += L" • Prior groups: " + std::to_wstring(exactGroups);
    if (!state_.groupAffinities.empty())
        line2 += L"\r\nClusters enforced: " + std::to_wstring(state_.groupAffinities.size());

    if (exactText == L"0")
        line2 += L"\r\nCurrent rules leave no valid future grouping.";
    else if (exactText == L"Too many to count exactly")
        line2 += L"\r\nExact counting is unavailable for this roster size and rule mix.";

    return line1 + L"\r\n" + line2;
}

std::wstring SeatingChartApp::BuildGroupPermutationCacheKey() const {
    std::wstring key;
    key.reserve(4096);
    key += L"N:";
    key += std::to_wstring(state_.roster.size());
    key += L"|P:";
    for (const int sz : CurrentGroupPattern()) {
        key += std::to_wstring(sz);
        key += L",";
    }
    key += L"|O:";
    key += GroupAvoidSameNumberEnabled() ? L"1" : L"0";
    key += GroupAvoidSamePartnersEnabled() ? L"1" : L"0";
    key += GroupAvoidSameFullGroupEnabled() ? L"1" : L"0";
    key += L"|R:";
    for (const auto& name : state_.roster) {
        key += CanonicalName(name);
        key += L",";
    }
    key += L"|KA:";
    {
        std::vector<std::wstring> rows;
        rows.reserve(state_.restrictions.size());
        for (const auto& rule : state_.restrictions)
            rows.push_back(CanonicalName(rule.first) + L">" + CanonicalName(rule.second));
        std::sort(rows.begin(), rows.end());
        for (const auto& row : rows) key += row + L";";
    }
    key += L"|KT:";
    {
        std::vector<std::wstring> rows;
        rows.reserve(state_.affinities.size());
        for (const auto& rule : state_.affinities)
            rows.push_back(CanonicalName(rule.first) + L">" + CanonicalName(rule.second));
        std::sort(rows.begin(), rows.end());
        for (const auto& row : rows) key += row + L";";
    }
    key += L"|MT:";
    {
        std::vector<std::wstring> rows;
        rows.reserve(state_.mustTogether.size());
        for (const auto& rule : state_.mustTogether)
            rows.push_back(CanonicalName(rule.first) + L">" + CanonicalName(rule.second));
        std::sort(rows.begin(), rows.end());
        for (const auto& row : rows) key += row + L";";
    }
    key += L"|GN:";
    {
        std::vector<std::wstring> rows;
        rows.reserve(groupNumberHistory_.size());
        for (const auto& [name, groups] : groupNumberHistory_) {
            std::vector<int> sortedGroups(groups.begin(), groups.end());
            std::sort(sortedGroups.begin(), sortedGroups.end());
            std::wstring row = name + L":";
            for (const int idx : sortedGroups) {
                row += std::to_wstring(idx);
                row += L",";
            }
            rows.push_back(std::move(row));
        }
        std::sort(rows.begin(), rows.end());
        for (const auto& row : rows) key += row + L";";
    }
    key += L"|GP:";
    {
        std::vector<std::wstring> rows;
        rows.reserve(groupPartnerSetHistory_.size());
        for (const auto& [name, histories] : groupPartnerSetHistory_) {
            std::wstring row = name + L":";
            std::vector<std::wstring> historyRows;
            historyRows.reserve(histories.size());
            for (const auto& partners : histories) {
                std::vector<std::wstring> sortedPartners(partners.begin(), partners.end());
                std::sort(sortedPartners.begin(), sortedPartners.end());
                std::wstring historyRow = L"[";
                for (const auto& partner : sortedPartners) {
                    historyRow += partner;
                    historyRow += L",";
                }
                historyRow += L"]";
                historyRows.push_back(std::move(historyRow));
            }
            std::sort(historyRows.begin(), historyRows.end());
            for (const auto& historyRow : historyRows) row += historyRow;
            rows.push_back(std::move(row));
        }
        std::sort(rows.begin(), rows.end());
        for (const auto& row : rows) key += row + L";";
    }
    key += L"|GX:";
    {
        std::vector<std::wstring> rows(groupExactHistory_.begin(), groupExactHistory_.end());
        std::sort(rows.begin(), rows.end());
        for (const auto& row : rows) key += row + L";";
    }
    return key;
}

std::wstring SeatingChartApp::ExactGroupPermutationText() {
    const std::wstring key = BuildGroupPermutationCacheKey();
    if (key == groupPermutationCacheKey_) return groupPermutationCacheValue_;

    const auto pattern = CurrentGroupPattern();
    const int studentCount = static_cast<int>(state_.roster.size());
    if (studentCount == 0 || pattern.empty()) {
        groupPermutationCacheKey_ = key;
        groupPermutationCacheValue_ = L"0";
        return groupPermutationCacheValue_;
    }

    struct Dsu {
        std::vector<int> parent;
        explicit Dsu(int n) : parent(static_cast<size_t>(n)) {
            for (int i = 0; i < n; ++i) parent[static_cast<size_t>(i)] = i;
        }
        int Find(int x) {
            int& p = parent[static_cast<size_t>(x)];
            if (p != x) p = Find(p);
            return p;
        }
        void Union(int a, int b) {
            a = Find(a);
            b = Find(b);
            if (a != b) parent[static_cast<size_t>(b)] = a;
        }
    };

    std::unordered_map<std::wstring, int> indexByName;
    for (int i = 0; i < studentCount; ++i)
        indexByName[CanonicalName(state_.roster[static_cast<size_t>(i)])] = i;

    Dsu dsu(studentCount);
    const bool avoidSameNumber = GroupAvoidSameNumberEnabled();
    const bool avoidSamePartners = GroupAvoidSamePartnersEnabled();
    const bool avoidSameFullGroup = GroupAvoidSameFullGroupEnabled();
    auto unionRule = [&](const Restriction& rule) {
        const auto ia = indexByName.find(CanonicalName(rule.first));
        const auto ib = indexByName.find(CanonicalName(rule.second));
        if (ia != indexByName.end() && ib != indexByName.end())
            dsu.Union(ia->second, ib->second);
    };
    for (const auto& rule : state_.affinities) unionRule(rule);
    for (const auto& rule : state_.mustTogether) unionRule(rule);
    for (const auto& group : state_.groupAffinities) {
        int anchor = -1;
        for (const auto& name : group) {
            const auto it = indexByName.find(CanonicalName(name));
            if (it == indexByName.end()) continue;
            if (anchor < 0) anchor = it->second;
            else dsu.Union(anchor, it->second);
        }
    }

    std::unordered_map<int, std::vector<int>> groupsByRoot;
    for (int i = 0; i < studentCount; ++i)
        groupsByRoot[dsu.Find(i)].push_back(i);

    struct Component {
        uint64_t studentMask = 0;
        int size = 0;
    };
    std::vector<Component> components;
    std::vector<int> componentOfStudent(static_cast<size_t>(studentCount), -1);
    components.reserve(groupsByRoot.size());
    for (const auto& [root, members] : groupsByRoot) {
        (void)root;
        Component comp;
        comp.size = static_cast<int>(members.size());
        if (comp.size > *std::max_element(pattern.begin(), pattern.end())) {
            groupPermutationCacheKey_ = key;
            groupPermutationCacheValue_ = L"0";
            return groupPermutationCacheValue_;
        }
        for (const int idx : members) {
            if (avoidSamePartners || avoidSameFullGroup)
                comp.studentMask |= (uint64_t{1} << idx);
            componentOfStudent[static_cast<size_t>(idx)] = static_cast<int>(components.size());
        }
        components.push_back(comp);
    }

    const int componentCount = static_cast<int>(components.size());
    std::vector<uint64_t> conflictMask(static_cast<size_t>(componentCount), 0);
    for (const auto& rule : state_.restrictions) {
        const auto ia = indexByName.find(CanonicalName(rule.first));
        const auto ib = indexByName.find(CanonicalName(rule.second));
        if (ia == indexByName.end() || ib == indexByName.end()) continue;
        const int ca = componentOfStudent[static_cast<size_t>(ia->second)];
        const int cb = componentOfStudent[static_cast<size_t>(ib->second)];
        if (ca == cb) {
            groupPermutationCacheKey_ = key;
            groupPermutationCacheValue_ = L"0";
            return groupPermutationCacheValue_;
        }
        conflictMask[static_cast<size_t>(ca)] |= (uint64_t{1} << cb);
        conflictMask[static_cast<size_t>(cb)] |= (uint64_t{1} << ca);
    }

    if (!avoidSameNumber && !avoidSamePartners && !avoidSameFullGroup &&
        state_.restrictions.empty() && state_.affinities.empty() &&
        state_.mustTogether.empty() && state_.groupAffinities.empty()) {
        groupPermutationCacheKey_ = key;
        groupPermutationCacheValue_ = FormatExactInteger(
            OrderedGroupArrangementCountExact(studentCount, pattern));
        return groupPermutationCacheValue_;
    }
    if (componentCount >= 64 ||
        ((avoidSamePartners || avoidSameFullGroup) && studentCount >= 64)) {
        groupPermutationCacheKey_ = key;
        groupPermutationCacheValue_ = L"Too many to count exactly";
        return groupPermutationCacheValue_;
    }

    std::vector<std::vector<uint64_t>> priorPartnerMasks(static_cast<size_t>(studentCount));
    if (avoidSamePartners) {
        for (int i = 0; i < studentCount; ++i) {
            const auto canon = CanonicalName(state_.roster[static_cast<size_t>(i)]);
            const auto hist = groupPartnerSetHistory_.find(canon);
            if (hist == groupPartnerSetHistory_.end()) continue;
            for (const auto& priorPartners : hist->second) {
                uint64_t mask = 0;
                for (const auto& partner : priorPartners) {
                    const auto it = indexByName.find(partner);
                    if (it != indexByName.end()) mask |= (uint64_t{1} << it->second);
                }
                priorPartnerMasks[static_cast<size_t>(i)].push_back(mask);
            }
        }
    }

    std::vector<std::vector<bool>> forbiddenGroup(static_cast<size_t>(componentCount),
                                                  std::vector<bool>(pattern.size(), false));
    if (avoidSameNumber) {
        for (int i = 0; i < studentCount; ++i) {
            const auto canon = CanonicalName(state_.roster[static_cast<size_t>(i)]);
            const auto hist = groupNumberHistory_.find(canon);
            if (hist == groupNumberHistory_.end()) continue;
            const int comp = componentOfStudent[static_cast<size_t>(i)];
            for (const int gi : hist->second) {
                if (gi >= 0 && gi < static_cast<int>(pattern.size()))
                    forbiddenGroup[static_cast<size_t>(comp)][static_cast<size_t>(gi)] = true;
            }
        }
    }

    auto violatesPartnerHistory = [&](uint64_t selectedStudents) {
            if (!avoidSamePartners) return false;
            for (int i = 0; i < studentCount; ++i) {
                if ((selectedStudents & (uint64_t{1} << i)) == 0) continue;
                const uint64_t partners = selectedStudents & ~(uint64_t{1} << i);
                if (partners == 0) continue;
                for (const uint64_t priorMask : priorPartnerMasks[static_cast<size_t>(i)]) {
                    const uint64_t overlapMask = partners & priorMask;
                    if (Popcount64(overlapMask) >= 2) return true;
                }
            }
            return false;
        };

    struct MemoKey {
        int groupIndex = 0;
        uint64_t remainingMask = 0;
        bool operator==(const MemoKey& other) const {
            return groupIndex == other.groupIndex && remainingMask == other.remainingMask;
        }
    };
    struct MemoHasher {
        size_t operator()(const MemoKey& key) const {
            return (static_cast<size_t>(key.groupIndex) * 1315423911u) ^
                   static_cast<size_t>(key.remainingMask);
        }
    };

    std::unordered_map<MemoKey, BigUInt, MemoHasher> memo;
    std::function<BigUInt(int, uint64_t)> countGroups;
    countGroups = [&](int groupIndex, uint64_t remainingMask) -> BigUInt {
        if (groupIndex == static_cast<int>(pattern.size()))
            return remainingMask == 0 ? BigUInt(1) : BigUInt(0);

        const MemoKey memoKey{groupIndex, remainingMask};
        if (const auto it = memo.find(memoKey); it != memo.end())
            return it->second;

        std::vector<int> remainingComponents;
        remainingComponents.reserve(componentCount);
        for (int comp = 0; comp < componentCount; ++comp) {
            if ((remainingMask & (uint64_t{1} << comp)) != 0)
                remainingComponents.push_back(comp);
        }

        BigUInt total(0);
        const int targetSize = pattern[static_cast<size_t>(groupIndex)];
        std::function<void(size_t, int, uint64_t, uint64_t)> chooseSubset;
        chooseSubset = [&](size_t pos, int need, uint64_t chosenComps, uint64_t chosenStudents) {
            if (need == 0) {
                if (!violatesPartnerHistory(chosenStudents)) {
                    if (avoidSameFullGroup) {
                        std::vector<std::wstring> groupNames;
                        groupNames.reserve(static_cast<size_t>(targetSize));
                        for (int i = 0; i < studentCount; ++i) {
                            if ((chosenStudents & (uint64_t{1} << i)) != 0)
                                groupNames.push_back(state_.roster[static_cast<size_t>(i)]);
                        }
                        if (groupExactHistory_.count(CanonicalGroupKey(groupNames)) != 0)
                            return;
                    }
                    total += countGroups(groupIndex + 1, remainingMask & ~chosenComps);
                }
                return;
            }
            if (pos >= remainingComponents.size()) return;

            int remainingCapacity = 0;
            for (size_t i = pos; i < remainingComponents.size(); ++i)
                remainingCapacity += components[static_cast<size_t>(remainingComponents[i])].size;
            if (remainingCapacity < need) return;

            for (size_t i = pos; i < remainingComponents.size(); ++i) {
                const int comp = remainingComponents[i];
                const auto& component = components[static_cast<size_t>(comp)];
                if (component.size > need) continue;
                if (forbiddenGroup[static_cast<size_t>(comp)][static_cast<size_t>(groupIndex)]) continue;
                if ((chosenComps & conflictMask[static_cast<size_t>(comp)]) != 0) continue;

                const uint64_t nextChosenComps = chosenComps | (uint64_t{1} << comp);
                const uint64_t nextChosenStudents = chosenStudents | component.studentMask;
                if (violatesPartnerHistory(nextChosenStudents)) continue;
                chooseSubset(i + 1, need - component.size, nextChosenComps, nextChosenStudents);
            }
        };

        chooseSubset(0, targetSize, 0, 0);
        memo.emplace(memoKey, total);
        return total;
    };

    const uint64_t allComponentsMask =
        componentCount >= 64 ? ~uint64_t{0} : ((uint64_t{1} << componentCount) - 1);
    groupPermutationCacheKey_ = key;
    groupPermutationCacheValue_ = FormatExactInteger(countGroups(0, allComponentsMask));
    return groupPermutationCacheValue_;
}

bool SeatingChartApp::CandidateGroupsMeetConstraints(
    const std::vector<std::vector<std::wstring>>& groups) const {
    if (groups.empty()) return false;

    std::unordered_map<std::wstring, int> groupIndexByStudent;
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        for (const auto& name : groups[gi])
            groupIndexByStudent[CanonicalName(name)] = static_cast<int>(gi);
    }

    for (const auto& rule : state_.restrictions) {
        const auto a = CanonicalName(rule.first);
        const auto b = CanonicalName(rule.second);
        const auto ia = groupIndexByStudent.find(a);
        const auto ib = groupIndexByStudent.find(b);
        if (ia != groupIndexByStudent.end() && ib != groupIndexByStudent.end() && ia->second == ib->second)
            return false;
    }

    for (const auto& rule : state_.affinities) {
        const auto a = CanonicalName(rule.first);
        const auto b = CanonicalName(rule.second);
        const auto ia = groupIndexByStudent.find(a);
        const auto ib = groupIndexByStudent.find(b);
        if (ia != groupIndexByStudent.end() && ib != groupIndexByStudent.end() && ia->second != ib->second)
            return false;
    }

    for (const auto& rule : state_.mustTogether) {
        const auto a = CanonicalName(rule.first);
        const auto b = CanonicalName(rule.second);
        const auto ia = groupIndexByStudent.find(a);
        const auto ib = groupIndexByStudent.find(b);
        if (ia != groupIndexByStudent.end() && ib != groupIndexByStudent.end() && ia->second != ib->second)
            return false;
    }
    for (const auto& group : state_.groupAffinities) {
        int groupIndex = -1;
        for (const auto& name : group) {
            const auto it = groupIndexByStudent.find(CanonicalName(name));
            if (it == groupIndexByStudent.end()) continue;
            if (groupIndex < 0) groupIndex = it->second;
            else if (groupIndex != it->second) return false;
        }
    }

    const bool avoidSameNumber = GroupAvoidSameNumberEnabled();
    const bool avoidSamePartners = GroupAvoidSamePartnersEnabled();
    const bool avoidSameFullGroup = GroupAvoidSameFullGroupEnabled();
    if (!avoidSameNumber && !avoidSamePartners && !avoidSameFullGroup) return true;

    for (size_t gi = 0; gi < groups.size(); ++gi) {
        std::unordered_set<std::wstring> groupMembers;
        for (const auto& name : groups[gi])
            groupMembers.insert(CanonicalName(name));

        if (avoidSameFullGroup &&
            groupExactHistory_.count(CanonicalGroupKey(groups[gi])) != 0)
            return false;

        for (const auto& name : groups[gi]) {
            const auto canon = CanonicalName(name);

            if (avoidSameNumber) {
                const auto hist = groupNumberHistory_.find(canon);
                if (hist != groupNumberHistory_.end() &&
                    hist->second.count(static_cast<int>(gi)) != 0)
                    return false;
            }

            if (avoidSamePartners) {
                std::unordered_set<std::wstring> currentPartners = groupMembers;
                currentPartners.erase(canon);
                if (currentPartners.size() < 2) continue;

                const auto hist = groupPartnerSetHistory_.find(canon);
                if (hist == groupPartnerSetHistory_.end()) continue;
                for (const auto& priorPartners : hist->second) {
                    size_t overlap = 0;
                    for (const auto& partner : currentPartners) {
                        if (priorPartners.count(partner) != 0 && ++overlap >= 2)
                            return false;
                    }
                }
            }
        }
    }

    return true;
}

void SeatingChartApp::RecordGroupShuffleHistory(const std::vector<std::vector<std::wstring>>& groups) {
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        std::unordered_set<std::wstring> groupMembers;
        for (const auto& name : groups[gi])
            groupMembers.insert(CanonicalName(name));

        for (size_t i = 0; i < groups[gi].size(); ++i) {
            const auto canon = CanonicalName(groups[gi][i]);
            groupNumberHistory_[canon].insert(static_cast<int>(gi));

            std::unordered_set<std::wstring> partners = groupMembers;
            partners.erase(canon);
            groupPartnerSetHistory_[canon].push_back(std::move(partners));

            for (size_t j = i + 1; j < groups[gi].size(); ++j)
                ++groupPairHistory_[GroupPairKey(groups[gi][i], groups[gi][j])];
        }
        groupExactHistory_.insert(CanonicalGroupKey(groups[gi]));
    }
}

// ---------------------------------------------------------------------------
// Inline cell edit — floating EDIT overlay directly on the rosterView cells
// ---------------------------------------------------------------------------

void SeatingChartApp::BeginInlineCellEdit(int item, int subItem) {
    if (!controls_.rosterView || !controls_.sidebar) return;
    // Allow item == roster.size() only when adding new (temp row already inserted)
    if (item < 0 || subItem < 1 || subItem > 2) return;

    CancelInlineCellEdit(); // dismiss any existing edit first

    // Get the cell rect in rosterView client coords, then map to sidebar coords
    RECT cellRect{};
    ListView_GetSubItemRect(controls_.rosterView, item, subItem, LVIR_BOUNDS, &cellRect);
    MapWindowPoints(controls_.rosterView, controls_.sidebar,
                    reinterpret_cast<POINT*>(&cellRect), 2);

    // Pre-fill with current value
    std::wstring val;
    if (item < static_cast<int>(state_.roster.size())) {
        const auto& name = state_.roster[static_cast<size_t>(item)];
        size_t sp = name.find(L' ');
        if (subItem == 1) val = (sp != std::wstring::npos) ? name.substr(0, sp) : name;
        else              val = (sp != std::wstring::npos) ? name.substr(sp + 1) : L"";
    }
    // For new rows with pendingFirst already set, keep it
    if (cellEdit_.isNew && subItem == 2 && !cellEdit_.pendingFirst.empty())
        val = L""; // last name starts empty

    cellEdit_.item    = item;
    cellEdit_.subItem = subItem;
    // isNew / pendingFirst are set by caller before calling this

    // Create the floating EDIT as a child of the sidebar (same parent as rosterView)
    const int h = std::max(Scale(18), static_cast<int>(cellRect.bottom - cellRect.top));
    cellEdit_.wnd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", val.c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        cellRect.left, cellRect.top,
        cellRect.right - cellRect.left, h,
        controls_.sidebar, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (cellEdit_.wnd) {
        SendMessageW(cellEdit_.wnd, WM_SETFONT,
                     reinterpret_cast<WPARAM>(renderer_.UiFont()), TRUE);
        SetWindowSubclass(cellEdit_.wnd, CellEditSubclassProc,
                          kCellEditSubclassId, reinterpret_cast<DWORD_PTR>(this));
        SetFocus(cellEdit_.wnd);
        SendMessageW(cellEdit_.wnd, EM_SETSEL, 0, -1); // select all
    }
}

// Commit the active cell edit and open the first-name cell of the next row.
// Called by the Enter key handler in CellEditSubclassProc.
void SeatingChartApp::AdvanceToNextStudent() {
    const int currentItem = cellEdit_.item;
    CommitInlineCellEdit(false);
    // After commit, state_.roster may have grown (if a new student was added).
    // nextItem is always currentItem + 1 — it may be an existing student or the ghost row.
    const int nextItem  = currentItem + 1;
    const int ghostIdx  = static_cast<int>(state_.roster.size());
    if (nextItem <= ghostIdx) {
        // The ghost row must be flagged as a "new student" edit; otherwise
        // subsequent Enter presses take the existing-row path and do nothing.
        cellEdit_.isNew = (nextItem == ghostIdx);
        BeginInlineCellEdit(nextItem, 1);
    }
}

// advance=true  → Tab key: after first-name, open last-name editor for same row.
// advance=false → Enter key or focus-loss: commit immediately, no second editor.
void SeatingChartApp::CommitInlineCellEdit(bool advance) {
    if (!cellEdit_.wnd) return; // guard against re-entry

    HWND wnd = cellEdit_.wnd;
    cellEdit_.wnd = nullptr; // clear FIRST to block re-entry from WM_KILLFOCUS

    wchar_t buf[256]{};
    GetWindowTextW(wnd, buf, 256);
    const std::wstring val = TrimCopy(buf);

    const int  item    = cellEdit_.item;
    const int  sub     = cellEdit_.subItem;
    const bool isNew   = cellEdit_.isNew;
    const std::wstring pending = cellEdit_.pendingFirst;
    cellEdit_ = {};    // fully clear state before any SyncRosterView call

    DestroyWindow(wnd);

    // -----------------------------------------------------------------------
    // Adding a brand-new student
    // -----------------------------------------------------------------------
    if (isNew) {
        if (sub == 1) {
            if (val.empty()) {
                // Nothing typed — ghost row stays, nothing added
                SyncRosterView(state_, controls_);
                return;
            }

            if (advance) {
                // Tab: save first name visually and open last-name editor
                if (controls_.rosterView)
                    ListView_SetItemText(controls_.rosterView, item,
                                         1, const_cast<wchar_t*>(val.c_str()));
                cellEdit_.isNew        = true;
                cellEdit_.pendingFirst = val;
                BeginInlineCellEdit(item, 2);
                return;
            }
            // Enter / focus-loss: commit with first name only
            AppState::Transaction tx(state_);
            tx->roster.push_back(val);
            tx.Commit();
            SyncRosterView(state_, controls_);
            RefreshRosterList(state_, controls_);
            RefreshGroupConfigList();
            SetStatus(L"Added " + val);
            ScheduleSave();
            return;
        }

        // sub == 2: completing last-name step (Tab from first-name landed here)
        const std::wstring full = val.empty() ? pending : pending + L" " + val;
        if (full.empty()) { SyncRosterView(state_, controls_); return; }
        AppState::Transaction tx(state_);
        tx->roster.push_back(full);
        tx.Commit();
        SyncRosterView(state_, controls_);
        RefreshRosterList(state_, controls_);
        RefreshGroupConfigList();
        SetStatus(L"Added " + full);
        ScheduleSave();
        return;
    }

    // -----------------------------------------------------------------------
    // Editing an existing student
    // -----------------------------------------------------------------------
    if (item < 0 || item >= static_cast<int>(state_.roster.size())) return;

    const auto& oldName = state_.roster[static_cast<size_t>(item)];
    size_t sp = oldName.find(L' ');
    std::wstring first = (sp != std::wstring::npos) ? oldName.substr(0, sp) : oldName;
    std::wstring last  = (sp != std::wstring::npos) ? oldName.substr(sp + 1) : L"";
    if (sub == 1) first = val;
    else          last  = val;

    const std::wstring full = first.empty() ? last :
                              (last.empty()  ? first : first + L" " + last);
    if (!full.empty() && full != oldName) {
        AppState::Transaction tx(state_);
        tx->roster[static_cast<size_t>(item)] = full;
        tx.Commit();
        SyncRosterView(state_, controls_);
        RefreshRosterList(state_, controls_);
        RefreshGroupConfigList();
        ScheduleSave();
    }
    // Tab from First Name → move to Last Name for the same row
    if (advance && sub == 1) BeginInlineCellEdit(item, 2);
}

void SeatingChartApp::CancelInlineCellEdit() {
    if (!cellEdit_.wnd) return;
    HWND wnd = cellEdit_.wnd;
    const bool wasNew = cellEdit_.isNew;
    cellEdit_ = {};
    DestroyWindow(wnd);
    // For new adds, just resync — the ghost row is part of the list already,
    // so a full resync restores everything cleanly with no orphan rows.
    if (wasNew)
        SyncRosterView(state_, controls_);
}

// PromptRulePairDropdown — shows a dialog with two comboboxes (populated from roster)
// instead of two free-text fields.
bool SeatingChartApp::PromptRulePairDropdown(const std::wstring& title,
                                             std::wstring& nameA, std::wstring& nameB) {
    if (state_.roster.empty()) {
        MessageBoxW(hwnd_, L"Add students to the roster first.", title.c_str(),
                    MB_OK | MB_ICONINFORMATION);
        return false;
    }

    struct Dlg {
        HWND parent;
        HWND cbA, cbB;
        bool ok;
        std::wstring selA, selB;
        const std::vector<std::wstring>* roster;
    } d{ hwnd_, {}, {}, false, {}, {}, &state_.roster };

    const wchar_t* cls = L"SeatingChartRulePairDropDlg";
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = [](HWND hw, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
            auto* s = reinterpret_cast<Dlg*>(GetWindowLongPtrW(hw, GWLP_USERDATA));
            if (msg == WM_NCCREATE) {
                s = reinterpret_cast<Dlg*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
                SetWindowLongPtrW(hw, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
            }
            switch (msg) {
            case WM_CREATE: {
                auto mkLbl = [&](const wchar_t* t, int y) {
                    CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE,
                        Scale(12), y, Scale(80), Scale(20), hw,
                        nullptr, GetModuleHandleW(nullptr), nullptr);
                };
                mkLbl(L"Student A:", Scale(14));
                mkLbl(L"Student B:", Scale(48));
                s->cbA = CreateWindowExW(0, L"COMBOBOX", nullptr,
                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                    Scale(96), Scale(12), Scale(170), Scale(200), hw,
                    nullptr, GetModuleHandleW(nullptr), nullptr);
                s->cbB = CreateWindowExW(0, L"COMBOBOX", nullptr,
                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                    Scale(96), Scale(46), Scale(170), Scale(200), hw,
                    nullptr, GetModuleHandleW(nullptr), nullptr);
                // Populate both from roster
                if (s->roster) {
                    for (const auto& nm : *s->roster) {
                        SendMessageW(s->cbA, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(nm.c_str()));
                        SendMessageW(s->cbB, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(nm.c_str()));
                    }
                }
                SendMessageW(s->cbA, CB_SETCURSEL, 0, 0);
                SendMessageW(s->cbB, CB_SETCURSEL, s->roster && s->roster->size() > 1 ? 1 : 0, 0);
                CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                    Scale(36), Scale(82), Scale(76), Scale(28), hw,
                    reinterpret_cast<HMENU>(IDOK), GetModuleHandleW(nullptr), nullptr);
                CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE,
                    Scale(120), Scale(82), Scale(76), Scale(28), hw,
                    reinterpret_cast<HMENU>(IDCANCEL), GetModuleHandleW(nullptr), nullptr);
                return 0;
            }
            case WM_COMMAND:
                if (LOWORD(wp) == IDOK) {
                    wchar_t buf[256]{};
                    const int ia = static_cast<int>(SendMessageW(s->cbA, CB_GETCURSEL, 0, 0));
                    const int ib = static_cast<int>(SendMessageW(s->cbB, CB_GETCURSEL, 0, 0));
                    SendMessageW(s->cbA, CB_GETLBTEXT, ia, reinterpret_cast<LPARAM>(buf));
                    s->selA = buf;
                    SendMessageW(s->cbB, CB_GETLBTEXT, ib, reinterpret_cast<LPARAM>(buf));
                    s->selB = buf;
                    if (s->selA == s->selB) {
                        MessageBoxW(hw, L"Select two different students.", L"Rule",
                                    MB_OK | MB_ICONWARNING);
                        return 0;
                    }
                    s->ok = true;
                    DestroyWindow(hw);
                    return 0;
                }
                if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hw); return 0; }
                break;
            case WM_CLOSE: DestroyWindow(hw); return 0;
            }
            return DefWindowProcW(hw, msg, wp, lp);
        };
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = cls;
        RegisterClassW(&wc);
        reg = true;
    }
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, cls, title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, Scale(290), Scale(150),
        hwnd_, nullptr, GetModuleHandleW(nullptr), &d);
    if (!dlg) return false;
    RECT pr{}, dr{};
    GetWindowRect(hwnd_, &pr); GetWindowRect(dlg, &dr);
    SetWindowPos(dlg, HWND_TOP,
        pr.left + ((pr.right - pr.left) - (dr.right - dr.left)) / 2,
        pr.top  + ((pr.bottom - pr.top) - (dr.bottom - dr.top)) / 2,
        0, 0, SWP_NOSIZE);
    EnableWindow(hwnd_, FALSE);
    ShowWindow(dlg, SW_SHOW);
    MSG m{};
    while (IsWindow(dlg) && GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m); DispatchMessageW(&m);
    }
    EnableWindow(hwnd_, TRUE); SetActiveWindow(hwnd_);
    if (!d.ok) return false;
    nameA = d.selA; nameB = d.selB;
    return true;
}

// ---------------------------------------------------------------------------
// RecalculateLayout — does NOT mutate layout item bounds on window resize
// ---------------------------------------------------------------------------

void SeatingChartApp::RecalculateLayout() {
    RECT rc{}; GetClientRect(hwnd_, &rc);
    layout_.client = rc;
    // Lay out within the ACTUAL client rect so the sidebar always sits flush at
    // the right edge (don't inflate to MinWindowWidth — that pushed it off-screen).
    const int cw = std::max(1, static_cast<int>(rc.right  - rc.left));
    const int ch = std::max(1, static_cast<int>(rc.bottom - rc.top));
    const int minPanel = Scale(kSidebarMinWidth);
    const int maxPanel = Scale(kSidebarMaxWidth);
    if (sidebarWidth_ <= 0) sidebarWidth_ = PanelWidth();
    sidebarWidth_ = std::clamp(sidebarWidth_, minPanel, std::min(maxPanel, std::max(minPanel, cw - Margin())));
    const int panelW  = sidebarWidth_;
    const int stripW  = classListBox_ ? Scale(kClassStripW) : 0;
    layout_.panel = { cw - stripW - panelW, 0, cw - stripW, ch };
    layout_.chart = { Margin(), HeaderHeight(),
                      std::max(Margin() + Scale(40), cw - stripW - panelW - Margin()),
                      std::max(HeaderHeight() + Scale(40), ch - Margin()) };

    layoutTx_ = ComputeLayoutViewTransform(layout_.chart, state_.roomW, state_.roomH);
    SyncLayoutInspectorWithSelection(state_, controls_);

    if (controls_.sidebar) {
        MoveWindow(controls_.sidebar,
                   layout_.panel.left, layout_.panel.top,
                   layout_.panel.right - layout_.panel.left,
                   layout_.panel.bottom - layout_.panel.top, TRUE);
        sidebar_.Recalculate(controls_.sidebar, state_, controls_, renderer_);
    }

    // Class strip
    if (classListBox_) {
        const int btnH = Scale(28);
        MoveWindow(classListBox_, cw - stripW, 0,         stripW, ch - btnH, TRUE);
        MoveWindow(addClassBtn_,  cw - stripW, ch - btnH, stripW, btnH,     TRUE);
    }

    LayoutFloatingTools();
    InvalidateChart();
}

// ---------------------------------------------------------------------------
// Message handlers
// ---------------------------------------------------------------------------

LRESULT SeatingChartApp::OnCreate(HWND hwnd) {
    INITCOMMONCONTROLSEX icc{sizeof(icc),
        ICC_STANDARD_CLASSES | ICC_UPDOWN_CLASS | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);

    hwnd_ = hwnd;
    dpi_  = g_dpi = [hwnd]() -> UINT {
        HDC dc = GetDC(hwnd);
        UINT d = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96u;
        if (dc) ReleaseDC(hwnd, dc);
        return d ? d : 96u;
    }();

    renderer_.ApplyThemeFromSystem();
    renderer_.RebuildFonts(dpi_);
    sidebarWidth_ = PanelWidth();
    BuildMenuBar();

    CreateAllUIControls(hwnd, controls_);
    if (controls_.sidebar)
        SetWindowLongPtrW(controls_.sidebar, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    if (controls_.rosterList)
        SetWindowSubclass(controls_.rosterList, RosterListSubclassProc,
                          kRosterListSubclassId, reinterpret_cast<DWORD_PTR>(this));

    // Class strip (narrow column on the far right of the window)
    classListBox_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kClassStripId)),
        GetModuleHandleW(nullptr), nullptr);
    addClassBtn_ = CreateWindowExW(0, L"BUTTON", L"+",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kAddClassBtnId)),
        GetModuleHandleW(nullptr), nullptr);

    ApplyFontsToControls(controls_, renderer_);
    ApplyThemeToListViews(); // set bg/text colours so selection is visible from the start
    RefreshGroupRuleToggleLabels();
    if (classListBox_) SendMessageW(classListBox_, WM_SETFONT,
                                    reinterpret_cast<WPARAM>(renderer_.UiFont()), TRUE);
    if (addClassBtn_)  SendMessageW(addClassBtn_,  WM_SETFONT,
                                    reinterpret_cast<WPARAM>(renderer_.UiFont()), TRUE);

    state_.Init();
    bool loaded = LoadState(&state_);
    if (loaded) state_.ClearUndoHistory();
    // Auto-migrate: if legacy state.txt still exists, silently write JSON and delete it.
    {
        const auto legacyPath = GetLegacyStateFilePath();
        if (!legacyPath.empty() &&
            GetFileAttributesW(legacyPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            SaveStateNow(&state_, false); // ensure JSON is up-to-date
            DeleteFileW(legacyPath.c_str()); // remove old format
        }
    }
    SyncAllEditsFromState();
    SyncRosterView(state_, controls_);
    SyncRulesLists(state_, controls_);
    RefreshSelectionFlags();
    UpdateButtonState(state_, controls_, false);
    SetStatus(L"Ready");
    InitClassList();
    SyncClassListBox();

    // Fit the window fully inside the monitor work area so every edge is
    // reachable for resizing. If it opens taller/wider than the screen, the
    // off-screen edges can't be grabbed (feels like it "only resizes one way").
    if (HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)) {
        MONITORINFO mi{ sizeof(mi) };
        if (GetMonitorInfoW(mon, &mi)) {
            RECT wr{}; GetWindowRect(hwnd, &wr);
            const int workW = mi.rcWork.right  - mi.rcWork.left;
            const int workH = mi.rcWork.bottom - mi.rcWork.top;
            const int margin = Scale(24);
            const int w = std::min(static_cast<int>(wr.right  - wr.left), workW - margin);
            const int h = std::min(static_cast<int>(wr.bottom - wr.top),  workH - margin);
            const int x = mi.rcWork.left + (workW - w) / 2;
            const int y = mi.rcWork.top  + (workH - h) / 2;
            SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    RecalculateLayout();
    return 0;
}

LRESULT SeatingChartApp::OnSize() { RecalculateLayout(); return 0; }

LRESULT SeatingChartApp::OnTimer(UINT_PTR id) {
    if (id == kAutoSaveTimerId) { KillTimer(hwnd_, kAutoSaveTimerId); SaveStateNow(&state_, false); }
    return 0;
}

// Apply app theme colours to all ListViews so selection is clearly visible
// regardless of the Windows accent colour or dark-mode setting.
void SeatingChartApp::ApplyThemeToListViews() {
    const COLORREF bg  = renderer_.Theme().window;  // dark in dark mode, white in light
    const COLORREF fg  = renderer_.Theme().text;
    // Selection highlight — use the app's accent colour so it's always legible
    const COLORREF sel = renderer_.Theme().accent;

    auto applyLv = [&](HWND lv) {
        if (!lv) return;
        ListView_SetBkColor   (lv, bg);
        ListView_SetTextColor (lv, fg);
        ListView_SetTextBkColor(lv, bg);
        // Tint the highlight: Windows uses the system colour by default, which can
        // be invisible against dark backgrounds.  Setting it explicitly to the app
        // accent makes selected rows pop in both light and dark mode.
        // LVM_SETSELECTEDCOLUMN is not what we want; instead use the hot-track
        // brush trick: paint header with accent for selection via custom draw, or
        // simply keep the system colour and rely on LVS_SHOWSELALWAYS.
        // For now set the bg/text; the system highlight will still show correctly
        // because LVS_EX_FULLROWSELECT + LVS_SHOWSELALWAYS ensures it paints.
        (void)sel;
        RedrawWindow(lv, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE);
    };
    applyLv(controls_.rosterView);
    applyLv(controls_.keepApartList);
    applyLv(controls_.keepTogetherList);
    applyLv(controls_.deskTagList);
}

LRESULT SeatingChartApp::OnThemeChange() {
    renderer_.ApplyThemeFromSystem();
    renderer_.RebuildFonts(dpi_);
    ApplyFontsToControls(controls_, renderer_);
    ApplyThemeToListViews();
    UpdateButtonState(state_, controls_, aaRunning_);
    InvalidateChart();
    if (controls_.sidebar)
        RedrawWindow(controls_.sidebar, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    return 0;
}

LRESULT SeatingChartApp::OnDpiChanged(UINT newDpi, const RECT* suggested) {
    dpi_ = g_dpi = newDpi;
    state_.saveDpi = newDpi;
    // Layout items are now in DPI-independent room-local coordinates; no scaling needed.
    renderer_.RebuildFonts(dpi_);
    ApplyFontsToControls(controls_, renderer_);
    ApplyThemeToListViews();
    if (suggested)
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    RecalculateLayout();
    return 0;
}

LRESULT SeatingChartApp::OnKeyDown(WPARAM vk, bool ctrl, bool shift) {
    if (ctrl) {
        if (vk == 'S') { SaveStateNow(&state_, true); SetStatus(state_.status); return 0; }
        if (vk == 'C') {
            if (state_.chartMode == ChartMode::Layout && !state_.selectedLayoutItems.empty()) {
                editor_.CopySelected();
            } else {
                SetStatus(renderer_.CopyChartToClipboard(hwnd_, state_, layout_.chart) ? L"Chart copied to clipboard" : L"Could not copy chart");
            }
            return 0;
        }
        if (vk == 'X' && state_.chartMode == ChartMode::Layout && !state_.selectedLayoutItems.empty()) {
            editor_.CutSelected(); return 0;
        }
        if (vk == 'V' && state_.chartMode == ChartMode::Layout) {
            editor_.PasteClipboard(); return 0;
        }
        if (vk == 'E') { SetStatus(renderer_.ExportChartToFile(hwnd_, state_, layout_.chart) ? L"Chart exported" : L"Export cancelled"); return 0; }
        if (vk == 'P') { SetStatus(PrintChart(hwnd_, state_, renderer_) ? L"Chart printed" : L"Print cancelled or failed"); return 0; }
        if (!shift && vk == 'Z') { PerformUndo(); return 0; }
        if (vk == 'Y' || (shift && vk == 'Z')) { PerformRedo(); return 0; }
        if (vk == 'A' && state_.chartMode == ChartMode::Layout) { selection_.SelectAll(); return 0; }
        if (vk == 'D' && state_.chartMode == ChartMode::Layout) { editor_.DuplicateSelected(); return 0; }
    }

    // Layout-mode shortcuts
    if (state_.chartMode == ChartMode::Layout && !ctrl) {
        if (vk == 'R') { editor_.Rotate(shift ? -90 : 90); return 0; }
        if (vk == 'F') { editor_.Flip(); return 0; }
        if (vk == 'L') { editor_.ToggleLock(); return 0; }

        // Arrow-key nudge in room units (1 or 10 with Shift)
        if (!state_.selectedLayoutItems.empty()) {
            const int step = shift ? 10 : 1;
            int dx = 0, dy = 0;
            if (vk == VK_LEFT)  dx = -step;
            if (vk == VK_RIGHT) dx =  step;
            if (vk == VK_UP)    dy = -step;
            if (vk == VK_DOWN)  dy =  step;
            if (dx || dy) { editor_.NudgeSelected(dx, dy); return 0; }
        }
    }

    if (vk == VK_ESCAPE) {
        if (draggingFront_) {
            draggingFront_ = false;
            ReleaseCapture();
            state_.frontEdge = frontDragOriginalEdge_;
            InvalidateChart();
            return 0;
        }
        if (rosterDragPrimed_ || rosterDragging_) {
            EndRosterDrag({}, false);
            SetStatus(L"Student drag cancelled");
            return 0;
        }
        if (editor_.IsEditing()) { editor_.CancelEdit(true); SetStatus(L"Layout edit cancelled"); return 0; }
        if (rubberBandSelecting_) {
            rubberBandSelecting_ = false; ReleaseCapture(); InvalidateChart(); return 0;
        }
        if (state_.selectedLayoutSeat) {
            state_.selectedLayoutSeat = std::nullopt; InvalidateChart(); return 0;
        }
        if (state_.chartMode == ChartMode::Layout && !state_.selectedLayoutItems.empty()) {
            selection_.ClearSelection();
            SyncLayoutInspectorWithSelection(state_, controls_);
            UpdateButtonState(state_, controls_, aaRunning_);
            InvalidateChart(); return 0;
        }
    }
    if (vk == VK_DELETE || vk == VK_BACK) {   // Backspace mirrors Delete
        // Delete key while roster view has focus → remove selected students
        if (controls_.rosterView && GetFocus() == controls_.rosterView &&
            ListView_GetSelectedCount(controls_.rosterView) > 0) {
            DeleteSelectedRosterStudents();
            return 0;
        }
        // Assign tool: Delete clears the focused seat's occupant.
        if (state_.chartMode == ChartMode::Seats && state_.selectedLayoutSeat) {
            const auto seat = *state_.selectedLayoutSeat;
            if (seat.first >= 0 && seat.first < static_cast<int>(state_.layoutItems.size())) {
                auto& occs = state_.layoutItems[static_cast<size_t>(seat.first)].occupants;
                if (seat.second >= 0 && seat.second < static_cast<int>(occs.size()) &&
                    !occs[static_cast<size_t>(seat.second)].empty()) {
                    AppState::Transaction tx(state_);
                    tx->layoutItems[static_cast<size_t>(seat.first)]
                        .occupants[static_cast<size_t>(seat.second)].clear();
                    tx.Commit();
                    SetStatus(L"Seat cleared"); InvalidateChart(); ScheduleSave();
                }
            }
            return 0;
        }
        // Arrange tool: Delete removes the selected furniture.
        if (state_.chartMode == ChartMode::Layout && !state_.selectedLayoutItems.empty()) {
            editor_.DeleteSelected(); return 0;
        }
    }
    return DefWindowProcW(hwnd_, WM_KEYDOWN, vk, 0);
}

LRESULT SeatingChartApp::OnCommand(int id, int notif) {
    const bool active = IsCommandActivation(notif);
    auto alignOrHint = [&](AlignMode mode) {
        if (!active) return;
        if (state_.chartMode != ChartMode::Layout) {
            SetStatus(L"Switch to Arrange mode to align furniture");
            return;
        }
        if (state_.selectedLayoutItems.size() < 2) {
            SetStatus(L"Select 2 or more desks to align");
            return;
        }
        editor_.Align(mode);
    };
    switch (id) {
    case kImportRosterId:         if (notif==BN_CLICKED) ImportRosterFromEdit(); break;
    case kLoadRosterId:           if (notif==BN_CLICKED) { if (PromptAndLoadRosterFile()) SetStatus(L"Roster loaded"); } break;
    case kSaveNowId:              if (active) { SaveStateNow(&state_,true); SetStatus(state_.status); } break;
    case kSeatModeId:             if (active) SetChartMode(ChartMode::Seats); break;
    case kLayoutModeId:           if (active) SetChartMode(ChartMode::Layout); break;
    case kCaptureChartId:         if (active) SetStatus(renderer_.CopyChartToClipboard(hwnd_,state_,layout_.chart) ? L"Chart copied" : L"Could not copy chart"); break;
    case kExportChartId:          if (active) SetStatus(renderer_.ExportChartToFile(hwnd_,state_,layout_.chart) ? L"Chart exported" : L"Export cancelled"); break;
    case kPrintChartId:           if (active) SetStatus(PrintChart(hwnd_, state_, renderer_) ? L"Chart printed" : L"Print cancelled or failed"); break;
    case kExportCsvId:            if (active) { ExportSeatingCsv(); break; }
    case kSeatingReportId:        if (active) { ShowSeatingReport(); break; }
    case kExportHtmlId:           if (active) { ExportHtml(); break; }
    case kAutoAssignId:           if (notif==BN_CLICKED) StartAutoAssign(); break;
    case kQuickFillSeatsId:       if (notif==BN_CLICKED) QuickFillSeats(); break;
    case kAssignSelectedRosterId: if (notif==BN_CLICKED) AssignRosterSelectionToSeat(); break;
    case kBulkTagId:              if (notif==BN_CLICKED) BulkTagSelectedRoster(); break;
    case kClearAllSeatsId:        if (notif==BN_CLICKED) ClearAllSeats(); break;
    case kApplyRulesId:           if (notif==BN_CLICKED) {
                                      const auto desk = GetWindowTextStr(controls_.restrictionEdit);
                                      const auto group = GetWindowTextStr(controls_.groupRulesEdit);
                                      ApplyRestrictions(SplitRestrictionInput(desk),
                                                        SplitAffinityInput(desk),
                                                        SplitTogetherInput(desk),
                                                        SplitGroupInput(group));
                                  } break;
    case kGroupRulesApplyId:      if (notif==BN_CLICKED) {
                                      ApplyGroupRules(SplitGroupInput(GetWindowTextStr(controls_.groupRulesEdit)));
                                  } break;
    case kGroupResetId:           if (notif==BN_CLICKED) ResetGroupShuffleMemory(); break;
    case kGroupKeepApartToggleId:
        if (notif == BN_CLICKED) {
            sidebar_.SetGroupKeepApartCollapsed(!sidebar_.GroupKeepApartCollapsed());
            RefreshGroupRuleToggleLabels();
            sidebar_.Recalculate(controls_.sidebar, state_, controls_, renderer_);
        }
        break;
    case kGroupKeepTogetherToggleId:
        if (notif == BN_CLICKED) {
            sidebar_.SetGroupKeepTogetherCollapsed(!sidebar_.GroupKeepTogetherCollapsed());
            RefreshGroupRuleToggleLabels();
            sidebar_.Recalculate(controls_.sidebar, state_, controls_, renderer_);
        }
        break;
    case kGroupAvoidSameNumberId:
    case kGroupAvoidSamePartnersId:
    case kGroupAvoidSameFullGroupId:
        if (notif == BN_CLICKED) RefreshAutoAssignFooter();
        break;
    case kShowLastNamesId:        if (notif==BN_CLICKED) ToggleShowLastNames(); break;
    case kAddSmartboardId:        if (notif==BN_CLICKED) editor_.AddItem(LayoutItemType::Smartboard); break;
    case kAddTrapezoidId:         if (notif==BN_CLICKED) editor_.AddItem(LayoutItemType::TrapezoidDesk); break;
    case kAddDeskId:              if (notif==BN_CLICKED) editor_.AddItem(LayoutItemType::RectangleDesk); break;
    case kAddTableId:             if (notif==BN_CLICKED) editor_.AddItem(LayoutItemType::Table4); break;
    case kAddBigTableId:          if (notif==BN_CLICKED) editor_.AddItem(LayoutItemType::BigTable); break;
    case kAddBlockId:             if (notif==BN_CLICKED) editor_.AddBlock(); break;
    case kAddTrapPairId:          if (notif==BN_CLICKED) editor_.AddTrapPair(); break;
    case kAddTrapPodId:           if (notif==BN_CLICKED) editor_.AddTrapPod(); break;
    case kDeleteLayoutItemId:     if (notif==BN_CLICKED) editor_.DeleteSelected(); break;
    case kMergeSelectedId:        if (notif==BN_CLICKED) SetStatus(L"Merge is disabled for now"); break;
    case kCopyLayoutItemId:       if (notif==BN_CLICKED) editor_.CopySelected(); break;
    case kCutLayoutItemId:        if (notif==BN_CLICKED) editor_.CutSelected(); break;
    case kPasteLayoutItemId:      if (notif==BN_CLICKED) editor_.PasteClipboard(); break;
    case kGroupLayoutItemsId:     if (notif==BN_CLICKED) editor_.GroupSelected(); break;
    case kUngroupLayoutItemsId:   if (notif==BN_CLICKED) editor_.UngroupSelected(); break;
    case kPresetRowsId:           if (active) { ApplyRowsPreset(); break; }
    case kPresetUId:              if (active) { ApplyUPreset(); break; }
    case kPresetHorseshoeId:      if (active) { ApplyHorseshoePreset(); break; }
    case kToggleVisibleId:        if (notif==BN_CLICKED) { editor_.ToggleVisibility(); break; }
    case kApplyLayoutItemId:      if (notif==BN_CLICKED) ApplyLayoutInspector(); break;
    case kDuplicateLayoutItemId:  if (notif==BN_CLICKED) editor_.DuplicateSelected(); break;
    case kSendLayoutBackId:       if (notif==BN_CLICKED) editor_.SendToBack(); break;
    case kBringLayoutFrontId:     if (notif==BN_CLICKED) editor_.BringToFront(); break;
    case kRotateCWId:             if (notif==BN_CLICKED) editor_.Rotate(+90); break;
    case kRotateCCWId:            if (notif==BN_CLICKED) editor_.Rotate(-90); break;
    case kFlipHId:                if (notif==BN_CLICKED) editor_.Flip(); break;
    case kLockItemId:             if (notif==BN_CLICKED) editor_.ToggleLock(); break;
    case kSelectAllLayoutId:      if (notif==BN_CLICKED) selection_.SelectAll(); break;
    case kAlignLeftId:            alignOrHint(AlignMode::Left); break;
    case kAlignRightId:           alignOrHint(AlignMode::Right); break;
    case kAlignTopId:             alignOrHint(AlignMode::Top); break;
    case kAlignBottomId:          alignOrHint(AlignMode::Bottom); break;
    case kAlignCenterHId:         alignOrHint(AlignMode::CenterH); break;
    case kAlignCenterVId:         alignOrHint(AlignMode::CenterV); break;
    case kDistributeHId:          alignOrHint(AlignMode::DistributeH); break;
    case kDistributeVId:          alignOrHint(AlignMode::DistributeV); break;
    case kApplyRoomSizeId:        if (active) PromptAndApplyRoomSize(); break;
    case kFrontEdgeId:            if (notif==BN_CLICKED) editor_.CycleFrontEdge(); break;
    case kSaveTemplateId:
        if (active) { if (editor_.SaveTemplate()) SetStatus(L"Template saved"); }
        break;
    case kLoadTemplateId:
        if (active) editor_.ApplyTemplate();
        break;
    case kShowAlignmentToolsId:
        if (active) ShowAlignmentToolsWindow();
        break;
    case kShowObjectInspectorId:
        if (active) ShowObjectInspectorWindow();
        break;
    case kRosterListId:
        if (notif==LBN_SELCHANGE) UpdateButtonState(state_, controls_, aaRunning_);
        else if (notif==LBN_DBLCLK) AssignRosterSelectionToSeat();
        break;
    case kRosterFilterId:
        if (notif == EN_CHANGE) {
            RefreshFilteredRosterList();
            UpdateButtonState(state_, controls_, aaRunning_);
        }
        break;
    case kRosterViewId:
        // NM_CLICK / NM_DBLCLK handled in WM_NOTIFY (HandleSidebarMessage) instead
        break;
    case kAddStudentId:
        if (active && controls_.rosterView) {
            // Ghost row already exists at the bottom — just scroll to it and start editing
            CancelInlineCellEdit();
            const int ghostIdx = static_cast<int>(state_.roster.size());
            ListView_EnsureVisible(controls_.rosterView, ghostIdx, FALSE);
            cellEdit_.isNew = true;
            BeginInlineCellEdit(ghostIdx, 1);
        }
        break;
    case kSaveStudentEditId:   break; // no longer used (cell edit commits on Enter/blur)
    case kInlineFirstEditId:   break;
    case kInlineLastEditId:    break;
    case kRemoveStudentId:
        if (active && controls_.rosterView) {
            int sel = ListView_GetNextItem(controls_.rosterView, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < static_cast<int>(state_.roster.size())) {
                const std::wstring removed = state_.roster[static_cast<size_t>(sel)];
                AppState::Transaction tx(state_);
                tx->roster.erase(tx->roster.begin() + sel);
                for (auto& item : tx->layoutItems)
                    for (auto& occ : item.occupants)
                        if (CanonicalName(occ) == CanonicalName(removed)) occ.clear();
                tx.Commit();
                SyncRosterView(state_, controls_);
                RefreshRosterList(state_, controls_);
                RefreshGroupConfigList();
                InvalidateChart();
                SetStatus(L"Removed " + removed);
                ScheduleSave();
            }
        }
        break;
    case kAddKeepApartId:
        if (active) {
            std::wstring a, b;
            if (PromptRulePairDropdown(L"Add Keep Apart Rule", a, b)) {
                Restriction r = NormalizeRestriction({a, b});
                if (!CanonicalName(r.first).empty() && !CanonicalName(r.second).empty()) {
                    AppState::Transaction tx(state_);
                    const auto it = std::find_if(tx->restrictions.begin(), tx->restrictions.end(),
                        [&](const Restriction& existing) { return RestrictionEquals(existing, r); });
                    if (it == tx->restrictions.end()) tx->restrictions.push_back(r);
                    tx.Commit();
                    SyncRulesLists(state_, controls_);
                    SyncRestrictionEditFromRules(state_, controls_);
                    RefreshAutoAssignFooter();
                    ScheduleSave();
                }
            }
        }
        break;
    case kRemKeepApartId:
        if (active && controls_.keepApartList) {
            int sel = ListView_GetNextItem(controls_.keepApartList, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < static_cast<int>(state_.restrictions.size())) {
                AppState::Transaction tx(state_);
                tx->restrictions.erase(tx->restrictions.begin() + sel);
                tx.Commit();
                SyncRulesLists(state_, controls_);
                SyncRestrictionEditFromRules(state_, controls_);
                RefreshAutoAssignFooter();
                ScheduleSave();
            }
        }
        break;
    case kAddKeepTogetherId:
        if (active) {
            std::wstring a, b;
            if (PromptRulePairDropdown(L"Add Keep Together Rule", a, b)) {
                Restriction r = NormalizeRestriction({a, b});
                if (!CanonicalName(r.first).empty() && !CanonicalName(r.second).empty()) {
                    AppState::Transaction tx(state_);
                    const auto it = std::find_if(tx->affinities.begin(), tx->affinities.end(),
                        [&](const Restriction& existing) { return RestrictionEquals(existing, r); });
                    if (it == tx->affinities.end()) tx->affinities.push_back(r);
                    tx.Commit();
                    SyncRulesLists(state_, controls_);
                    SyncRestrictionEditFromRules(state_, controls_);
                    RefreshAutoAssignFooter();
                    ScheduleSave();
                }
            }
        }
        break;
    case kRemKeepTogetherId:
        if (active && controls_.keepTogetherList) {
            int sel = ListView_GetNextItem(controls_.keepTogetherList, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < static_cast<int>(state_.affinities.size())) {
                AppState::Transaction tx(state_);
                tx->affinities.erase(tx->affinities.begin() + sel);
                tx.Commit();
                SyncRulesLists(state_, controls_);
                SyncRestrictionEditFromRules(state_, controls_);
                RefreshAutoAssignFooter();
                ScheduleSave();
            }
        }
        break;
    case kAddDeskTagRuleId:
        if (active) {
            std::wstring student, tag;
            if (PromptRulePair(L"Add Desk Tag Rule", student, tag)) {
                student = TrimCopy(student);
                tag = TrimCopy(tag);
                if (!student.empty() && !tag.empty()) {
                    AppState::Transaction tx(state_);
                    auto& tags = tx->StudentRecord(student).forbiddenDesks;
                    if (std::find(tags.begin(), tags.end(), tag) == tags.end())
                        tags.push_back(tag);
                    tx.Commit();
                    SyncRulesLists(state_, controls_);
                    SetStatus(L"Added desk tag rule for " + student);
                    ScheduleSave();
                }
            }
        }
        break;
    case kRemDeskTagRuleId:
        if (active && controls_.deskTagList) {
            int sel = ListView_GetNextItem(controls_.deskTagList, -1, LVNI_SELECTED);
            if (sel >= 0) {
                int row = 0;
                AppState::Transaction tx(state_);
                bool removed = false;
                for (const auto& name : tx->roster) {
                    auto itInfo = tx->studentInfo.find(CanonicalName(name));
                    if (itInfo == tx->studentInfo.end()) continue;
                    auto& tags = itInfo->second.forbiddenDesks;
                    for (auto it = tags.begin(); it != tags.end(); ++it, ++row) {
                        if (row == sel) {
                            tags.erase(it);
                            removed = true;
                            break;
                        }
                    }
                    if (removed) break;
                }
                if (removed) {
                    tx.Commit();
                    SyncRulesLists(state_, controls_);
                    ScheduleSave();
                }
            }
        }
        break;
    case kGroupConfigListId:
        if (notif == LBN_DBLCLK) ShuffleGroups();
        break;
    case kGroupSizeEditId:
        if (notif == EN_CHANGE && controls_.groupSizeEdit) {
            int v = GetDlgItemInt(controls_.sidebar, kGroupSizeEditId, nullptr, FALSE);
            groupSizePref_ = std::clamp(v, 1, 99);
            RefreshGroupConfigList();
        }
        break;
    case kShuffleGroupsId:
        if (active) { ShuffleGroups(); InvalidateChart(); }
        break;
    case kGroupSizeComboId:
        if (notif == CBN_SELCHANGE && controls_.groupSizeCombo) {
            const int idx = static_cast<int>(SendMessageW(controls_.groupSizeCombo, CB_GETCURSEL, 0, 0));
            if (idx >= 0) {
                wchar_t buf[16]{};
                SendMessageW(controls_.groupSizeCombo, CB_GETLBTEXT, idx, reinterpret_cast<LPARAM>(buf));
                const int k = _wtoi(buf);
                if (k >= 2) { groupSizePref_ = k; }
                RefreshGroupCombo(); // update the "or N" label
                RefreshAutoAssignFooter();
            }
        }
        break;
    case kClassStripId:
        if (notif == LBN_SELCHANGE && classListBox_) {
            const int sel = static_cast<int>(SendMessageW(classListBox_, LB_GETCURSEL, 0, 0));
            SwitchToClass(sel);
        }
        break;
    case kAddClassBtnId:
        if (active) NewClass();
        break;
    }
    return 0;
}

LRESULT SeatingChartApp::OnLButtonDown(POINT pt, bool shift) {
    SetFocus(hwnd_);
    if (HitSidebarSplitter(pt)) {
        resizingSidebar_ = true;
        SetCapture(hwnd_);
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return 0;
    }
    // Front-edge indicator drag — takes priority over item/seat actions
    if (HitTestFrontIndicator(pt)) {
        frontDragOriginalEdge_ = state_.frontEdge;
        draggingFront_ = true;
        SetCapture(hwnd_);
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return 0;
    }
    if (state_.chartMode == ChartMode::Seats) {
        // Assign tool: click a furniture seat to place a roster name.
        if (const auto seat = selection_.HitTestSeatSlot(pt)) {
            POINT anchor = pt; ClientToScreen(hwnd_, &anchor);
            AssignLayoutSeatViaMenu(*seat, anchor);
        } else if (state_.selectedLayoutSeat) {
            state_.selectedLayoutSeat = std::nullopt;
            InvalidateChart();
        }
    } else {
        // Rotation handle takes priority, then resize handles, then the item.
        const bool         rotate = selection_.HitTestRotateHandle(pt);
        const ResizeHandle rh     = rotate ? ResizeHandle::None
                                           : selection_.HitTestHandle(pt);
        const int hit = (rotate || rh != ResizeHandle::None)
                        ? selection_.Primary()
                        : selection_.HitTestItem(pt);

        if (hit >= 0) {
            if (shift && !rotate && rh == ResizeHandle::None) {
                // Shift+click toggles the item in/out of the selection set.
                selection_.ToggleInSelection(hit);
                SetLayoutSelectionHint();
            } else {
                selection_.SetSingleSelection(hit);
                SyncLayoutInspectorWithSelection(state_, controls_);
                UpdateButtonState(state_, controls_, aaRunning_);
                InvalidateChart();
                if      (rotate)                  editor_.BeginRotate(pt);
                else if (rh != ResizeHandle::None) editor_.BeginResize(pt, rh);
                else {
                    editor_.BeginDrag(pt);
                    SetLayoutSelectionHint();
                }
            }
        } else {
            if (!state_.selectedLayoutItems.empty() && !shift) {
                selection_.ClearSelection();
                SyncLayoutInspectorWithSelection(state_, controls_);
                UpdateButtonState(state_, controls_, aaRunning_);
                InvalidateChart();
            }
            if (PtInRectEx(layout_.chart, pt)) {
                rubberBandSelecting_ = true;
                rubberBandStart_ = rubberBandEnd_ = pt;
                SetCapture(hwnd_);
            }
        }
    }
    return 0;
}

LRESULT SeatingChartApp::OnLButtonDblClk(POINT pt) {
    if (state_.chartMode == ChartMode::Layout) {
        // Double-clicking a furniture seat assigns/clears its occupant.
        if (const auto seat = selection_.HitTestSeatSlot(pt)) {
            POINT anchor = pt; ClientToScreen(hwnd_, &anchor);
            AssignLayoutSeatViaMenu(*seat, anchor);
            return 0;
        }
        const int hit = selection_.HitTestItem(pt);
        if (hit >= 0) {
            selection_.SetSingleSelection(hit);
            SyncLayoutInspectorWithSelection(state_, controls_);
            UpdateButtonState(state_, controls_, aaRunning_);
            SetLayoutSelectionHint();
            InvalidateChart();
        }
    }
    return 0;
}

LRESULT SeatingChartApp::OnMouseMove(POINT pt, WPARAM buttons) {
    if (draggingFront_ && (buttons & MK_LBUTTON)) {
        // Live-preview: move the front bar to the nearest edge as the user drags
        if (PtInRectEx(layout_.chart, pt)) {
            const RoomEdge hovered = NearestRoomEdge(pt);
            if (hovered != state_.frontEdge) {
                state_.frontEdge = hovered;
                InvalidateChart();
            }
        }
        return 0;
    }
    if (resizingSidebar_ && (buttons & MK_LBUTTON)) {
        ResizeSidebarLive(pt);
    } else if (editor_.IsDragging() && (buttons & MK_LBUTTON)) editor_.UpdateDrag(pt);
    else if (editor_.IsResizing() && (buttons & MK_LBUTTON))
        editor_.UpdateResize(pt, (buttons & MK_SHIFT) != 0);
    else if (editor_.IsRotating() && (buttons & MK_LBUTTON))
        editor_.UpdateRotate(pt, (buttons & MK_SHIFT) != 0);
    else if (rubberBandSelecting_ && (buttons & MK_LBUTTON)) {
        rubberBandEnd_ = pt; InvalidateChart();
    } else UpdateHover(pt);
    return 0;
}

LRESULT SeatingChartApp::OnMouseLeave() {
    trackingMouse_ = false;
    if (hoverItem_ >= 0) { InvalidateChart(); hoverItem_ = -1; }
    return 0;
}

// Group colour presets (0x00RRGGBB). 0 = none.
static const struct { const wchar_t* name; uint32_t rgb; } kGroupColors[] = {
    { L"None",   0x000000 }, { L"Red",    0xE2574B }, { L"Orange", 0xE8923C },
    { L"Yellow", 0xEFC94C }, { L"Green",  0x6FBF73 }, { L"Teal",   0x4FB0C6 },
    { L"Blue",   0x4A78D6 }, { L"Purple", 0x9B6FD0 }, { L"Pink",   0xE07CB0 },
};
// Preset attribute tags (toggleable).
static const wchar_t* const kPresetTags[] = {
    L"Front row", L"Quiet zone", L"Keep apart", L"Behavior",
    L"IEP", L"ELL", L"Allergy", L"Gifted",
};

void SeatingChartApp::StudentContextMenu(LayoutSeatRef seat, POINT screenAnchor) {
    const int it = seat.first, sl = seat.second;
    if (it < 0 || it >= static_cast<int>(state_.layoutItems.size())) return;
    LayoutItem& item = state_.layoutItems[static_cast<size_t>(it)];
    if (sl < 0 || sl >= static_cast<int>(item.occupants.size())) return;
    const std::wstring name = item.occupants[static_cast<size_t>(sl)];
    if (name.empty()) {                       // empty seat → fall back to assign
        AssignLayoutSeatViaMenu(seat, screenAnchor);
        return;
    }
    state_.selectedLayoutSeat = seat;
    InvalidateChart();

    const StudentInfo* info = state_.FindStudent(name);
    const uint32_t curColor = info ? info->color : 0u;
    auto hasTag = [&](const std::wstring& t) {
        return info && std::find(info->tags.begin(), info->tags.end(), t) != info->tags.end();
    };

    constexpr UINT kColorBase = 60000, kTagBase = 60100, kRemove = 60200;

    HMENU colorMenu = CreatePopupMenu();
    for (int c = 0; c < static_cast<int>(std::size(kGroupColors)); ++c)
        AppendMenuW(colorMenu, MF_STRING | (curColor == kGroupColors[c].rgb ? MF_CHECKED : 0u),
                    kColorBase + c, kGroupColors[c].name);

    HMENU tagMenu = CreatePopupMenu();
    for (int t = 0; t < static_cast<int>(std::size(kPresetTags)); ++t)
        AppendMenuW(tagMenu, MF_STRING | (hasTag(kPresetTags[t]) ? MF_CHECKED : 0u),
                    kTagBase + t, kPresetTags[t]);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, name.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(colorMenu), L"Colour / group");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(tagMenu),   L"Tags");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kRemove, (L"Remove " + name).c_str());

    const int id = static_cast<int>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenAnchor.x, screenAnchor.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);   // destroys submenus too
    if (id <= 0) return;

    if (id >= static_cast<int>(kColorBase) &&
        id < static_cast<int>(kColorBase + std::size(kGroupColors))) {
        StudentInfo& rec = state_.StudentRecord(name);
        rec.color = kGroupColors[id - kColorBase].rgb;   // None == 0 clears it
        state_.dirty = true;
        SetStatus(L"Set colour for " + name);
    } else if (id >= static_cast<int>(kTagBase) &&
               id < static_cast<int>(kTagBase + std::size(kPresetTags))) {
        const std::wstring tag = kPresetTags[id - kTagBase];
        StudentInfo& rec = state_.StudentRecord(name);
        const auto pos = std::find(rec.tags.begin(), rec.tags.end(), tag);
        if (pos == rec.tags.end()) { rec.tags.push_back(tag); SetStatus(name + L": +" + tag); }
        else                       { rec.tags.erase(pos);     SetStatus(name + L": -" + tag); }
        state_.dirty = true;
    } else if (id == static_cast<int>(kRemove)) {
        state_.ClearStudentFromSeat(seat);
        SetStatus(L"Seat cleared");
    }
    InvalidateChart();
    ScheduleSave();
}

LRESULT SeatingChartApp::OnContextMenu(HWND source, POINT screenPt) {
    // Keyboard-invoked menus arrive as (-1,-1); fall back to the cursor.
    if (screenPt.x == -1 && screenPt.y == -1) GetCursorPos(&screenPt);

    if (source == classListBox_ && classListBox_) {
        POINT cp = screenPt;
        ScreenToClient(classListBox_, &cp);
        const DWORD hit = static_cast<DWORD>(SendMessageW(
            classListBox_, LB_ITEMFROMPOINT, 0, MAKELPARAM(cp.x, cp.y)));
        if (HIWORD(hit) != 0) return 0;
        const int sel = static_cast<int>(LOWORD(hit));
        if (sel >= 0 && sel < static_cast<int>(classList_.size())) {
            SendMessageW(classListBox_, LB_SETCURSEL, sel, 0);
            HMENU menu = CreatePopupMenu();
            if (!menu) return 0;
            AppendMenuW(menu, MF_STRING, 70001, L"Rename Class");
            AppendMenuW(menu, MF_STRING, 70002, L"New Class");
            const int id = static_cast<int>(TrackPopupMenu(
                menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
                screenPt.x, screenPt.y, 0, hwnd_, nullptr));
            DestroyMenu(menu);
            if (id == 70001) {
                std::wstring name = classList_[static_cast<size_t>(sel)].name;
                if (PromptSingleText(L"Rename Class", L"Class name:", name)) {
                    RenameClass(sel, name);
                }
            } else if (id == 70002) {
                NewClass();
            }
        }
        return 0;
    }

    // Assign tool: right-clicking a seat edits that student's colour/tags.
    if (state_.chartMode == ChartMode::Seats) {
        POINT cp = screenPt; ScreenToClient(hwnd_, &cp);
        if (const auto seat = selection_.HitTestSeatSlot(cp)) {
            StudentContextMenu(*seat, screenPt);
            return 0;
        }
        return DefWindowProcW(hwnd_, WM_CONTEXTMENU, 0, MAKELPARAM(screenPt.x, screenPt.y));
    }

    // Right-click on the front-edge indicator — works in any mode
    {
        POINT cp = screenPt; ScreenToClient(hwnd_, &cp);
        if (HitTestFrontIndicator(cp)) {
            static const struct { RoomEdge e; const wchar_t* label; } kEdges[] = {
                { RoomEdge::Top,    L"Front: Top"    },
                { RoomEdge::Bottom, L"Front: Bottom" },
                { RoomEdge::Left,   L"Front: Left"   },
                { RoomEdge::Right,  L"Front: Right"  },
            };
            HMENU menu = CreatePopupMenu();
            if (!menu) return 0;
            for (int i = 0; i < 4; ++i)
                AppendMenuW(menu, MF_STRING | (state_.frontEdge == kEdges[i].e ? MF_CHECKED : 0u),
                            static_cast<UINT_PTR>(i + 1), kEdges[i].label);
            const int id = static_cast<int>(TrackPopupMenu(
                menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
                screenPt.x, screenPt.y, 0, hwnd_, nullptr));
            DestroyMenu(menu);
            if (id >= 1 && id <= 4) {
                const RoomEdge newEdge = kEdges[id - 1].e;
                if (newEdge != state_.frontEdge) CommitFrontEdge(newEdge);
            }
            return 0;
        }
    }

    if (state_.chartMode != ChartMode::Layout)
        return DefWindowProcW(hwnd_, WM_CONTEXTMENU, 0, MAKELPARAM(screenPt.x, screenPt.y));

    POINT clientPt = screenPt;
    ScreenToClient(hwnd_, &clientPt);
    if (!PtInRectEx(layout_.chart, clientPt)) return 0;

    // Right-clicking an item that is not part of the current selection selects
    // it first; right-clicking empty canvas clears the selection and shows no menu.
    const int hit = selection_.HitTestItem(clientPt);
    if (hit < 0) {
        if (!state_.selectedLayoutItems.empty()) {
            selection_.ClearSelection();
            SyncLayoutInspectorWithSelection(state_, controls_);
            UpdateButtonState(state_, controls_, aaRunning_);
            InvalidateChart();
        }
        HMENU menu = CreatePopupMenu();
        if (!menu) return 0;
        AppendMenuW(menu, MF_STRING, kPasteLayoutItemId, L"Paste");
        const int id = static_cast<int>(TrackPopupMenu(
            menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
            screenPt.x, screenPt.y, 0, hwnd_, nullptr));
        DestroyMenu(menu);
        if (id > 0) SendMessageW(hwnd_, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0);
        return 0;
    }
    if (!selection_.IsSelected(hit)) {
        selection_.SetSingleSelection(hit);
        SyncLayoutInspectorWithSelection(state_, controls_);
        UpdateButtonState(state_, controls_, aaRunning_);
        InvalidateChart();
    }
    if (state_.selectedLayoutItems.empty()) return 0;
    SetLayoutSelectionHint();

    const bool multi  = state_.selectedLayoutItems.size() > 1;
    const bool locked = state_.selectedLayoutItem &&
        state_.layoutItems[static_cast<size_t>(*state_.selectedLayoutItem)].locked;

    HMENU menu = CreatePopupMenu();
    if (!menu) return 0;
    auto add = [&](UINT id, const wchar_t* text, bool enabled = true) {
        AppendMenuW(menu, MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED), id, text);
    };
    const UINT sep = MF_SEPARATOR;

    add(kCopyLayoutItemId,      L"Copy", true);
    add(kCutLayoutItemId,       L"Cut", !locked);
    add(kPasteLayoutItemId,     L"Paste", true);
    AppendMenuW(menu, sep, 0, nullptr);
    add(kDuplicateLayoutItemId, L"Duplicate", !locked);
    add(kDeleteLayoutItemId,    L"Delete");
    add(kLockItemId,            locked ? L"Unlock" : L"Lock");
    if (multi) add(kGroupLayoutItemsId, L"Group");
    add(kUngroupLayoutItemsId, L"Ungroup");
    AppendMenuW(menu, sep, 0, nullptr);
    add(kRotateCWId,  L"Rotate 90\xB0 Clockwise",        !locked);
    add(kRotateCCWId, L"Rotate 90\xB0 Counterclockwise", !locked);
    add(kFlipHId,     L"Flip Horizontal",                !locked);
    AppendMenuW(menu, sep, 0, nullptr);
    add(kBringLayoutFrontId, L"Bring to Front");
    add(kSendLayoutBackId,   L"Send to Back");
    if (multi) {
        AppendMenuW(menu, sep, 0, nullptr);
        add(kAlignLeftId,    L"Align Left");
        add(kAlignRightId,   L"Align Right");
        add(kAlignTopId,     L"Align Top");
        add(kAlignBottomId,  L"Align Bottom");
        add(kAlignCenterHId, L"Center Horizontally");
        add(kAlignCenterVId, L"Center Vertically");
        add(kDistributeHId,  L"Distribute Horizontally");
        add(kDistributeVId,  L"Distribute Vertically");
    }

    const int id = static_cast<int>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenPt.x, screenPt.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    if (id > 0) SendMessageW(hwnd_, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0);
    return 0;
}

LRESULT SeatingChartApp::OnSetCursor(LPARAM lParam) {
    if (LOWORD(lParam) == HTCLIENT) {
        POINT pt{}; GetCursorPos(&pt); ScreenToClient(hwnd_, &pt);
        if (HitSidebarSplitter(pt) || resizingSidebar_) {
            SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
            return TRUE;
        }
        if (draggingFront_ || HitTestFrontIndicator(pt)) {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
        if (state_.chartMode == ChartMode::Layout) {
            if (selection_.HitTestRotateHandle(pt))
                { SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE; }
            const ResizeHandle rh = selection_.HitTestHandle(pt);
            if (rh != ResizeHandle::None) {
                // Pick a double-arrow cursor matching the handle's *rotated*
                // outward direction so it stays correct for rotated items.
                int rot = 0;
                if (state_.selectedLayoutItem &&
                    *state_.selectedLayoutItem < static_cast<int>(state_.layoutItems.size()))
                    rot = state_.layoutItems[static_cast<size_t>(*state_.selectedLayoutItem)].rotation;
                double base = 0.0; // outward angle (deg), screen y-down
                switch (rh) {
                case ResizeHandle::TopLeft:     base = 225.0; break;
                case ResizeHandle::BottomRight: base =  45.0; break;
                case ResizeHandle::TopRight:    base = 315.0; break;
                case ResizeHandle::BottomLeft:  base = 135.0; break;
                default: break;
                }
                double a = base + rot;       // rotation is clockwise on screen
                a = std::fmod(a, 180.0); if (a < 0) a += 180.0;
                const int bucket = static_cast<int>(std::lround(a / 45.0)) % 4;
                LPCWSTR cur = IDC_SIZEWE;    // 0°  → horizontal
                if      (bucket == 1) cur = IDC_SIZENWSE;  // 45°
                else if (bucket == 2) cur = IDC_SIZENS;    // 90°
                else if (bucket == 3) cur = IDC_SIZENESW;  // 135°
                SetCursor(LoadCursor(nullptr, cur));
                return TRUE;
            }
            const int hit = selection_.HitTestItem(pt);
            if (hit >= 0) {
                const bool locked = state_.layoutItems[static_cast<size_t>(hit)].locked;
                SetCursor(LoadCursor(nullptr, locked ? IDC_NO : IDC_SIZEALL));
                return TRUE;
            }
            if (rubberBandSelecting_)
                { SetCursor(LoadCursor(nullptr, IDC_CROSS)); return TRUE; }
        }
        if (state_.chartMode == ChartMode::Seats &&
            selection_.HitTestSeatSlot(pt).has_value())
            { SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE; }
    }
    // Pass the real window handle as wParam so DefWindowProc shows the standard
    // resize cursors (↔ on side edges, diagonal on corners) over the frame.
    return DefWindowProcW(hwnd_, WM_SETCURSOR, reinterpret_cast<WPARAM>(hwnd_), lParam);
}

LRESULT SeatingChartApp::OnPaint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    if (sidebar_.ActiveTab() == 3) {
        groupsZoom_ = renderer_.ClampGroupsZoomToFit(
            hwnd_, layout_.chart, generatedGroups_, state_.showLastNames, groupsZoom_);
        renderer_.PaintGroupsCanvasBuffered(hwnd_, hdc, layout_.chart,
                                            generatedGroups_, state_.showLastNames,
                                            groupsZoom_);
    } else {
        RECT rb{};
        if (rubberBandSelecting_) {
            rb = {
                std::min(rubberBandStart_.x, rubberBandEnd_.x),
                std::min(rubberBandStart_.y, rubberBandEnd_.y),
                std::max(rubberBandStart_.x, rubberBandEnd_.x),
                std::max(rubberBandStart_.y, rubberBandEnd_.y)
            };
        }
        renderer_.PaintWindowBuffered(hwnd_, hdc, state_, layout_.chart,
                                      {hoverItem_}, rb, dragPreview_);
    }
    EndPaint(hwnd_, &ps);
    return 0;
}

LRESULT SeatingChartApp::OnDestroy() {
    if (alignmentToolsWindow_) {
        DestroyWindow(alignmentToolsWindow_);
        alignmentToolsWindow_ = nullptr;
    }
    if (objectInspectorWindow_) {
        DestroyWindow(objectInspectorWindow_);
        objectInspectorWindow_ = nullptr;
    }
    if (aaRunning_ && aaThread_) {
        aaCancel_.store(true, std::memory_order_relaxed);
        WaitForSingleObject(aaThread_, 5000);
        CloseHandle(aaThread_); aaThread_ = nullptr;
    }
    if (state_.dirty) SaveStateNow(&state_, false);
    return 0;
}

// ---------------------------------------------------------------------------
// HandleSidebarMessage
// ---------------------------------------------------------------------------

LRESULT SeatingChartApp::HandleSidebarMessage(HWND sidebar, UINT msg,
                                               WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        sidebar_.Recalculate(sidebar, state_, controls_, renderer_);
        return 0;
    case WM_VSCROLL:
        sidebar_.HandleVScroll(sidebar, wParam, state_, controls_, renderer_);
        return 0;
    case WM_MOUSEWHEEL:
        sidebar_.HandleMouseWheel(sidebar, GET_WHEEL_DELTA_WPARAM(wParam),
                                   state_, controls_, renderer_);
        return 0;
    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<NMHDR*>(lParam);
        if (hdr->idFrom == kTabControlId && hdr->code == TCN_SELCHANGE) {
            const int prevTab = sidebar_.ActiveTab();
            const int tab = TabCtrl_GetCurSel(controls_.tabControl);
            sidebar_.SetActiveTab(tab);
            // Tab 2 (Arrange) → Layout mode; tabs 0/1 (Roster/Rules) → Assign mode
            const ChartMode newMode = (tab == 2) ? ChartMode::Layout : ChartMode::Seats;
            if (state_.chartMode != newMode) SetChartMode(newMode);
            else {
                sidebar_.Recalculate(controls_.sidebar, state_, controls_, renderer_);
                if (tab == 3 || prevTab == 3) InvalidateChart();
            }
            RefreshAutoAssignFooter();
        } else if (hdr->idFrom == kRosterViewId &&
                   (hdr->code == NM_CLICK || hdr->code == NM_DBLCLK)) {
            auto* nm = reinterpret_cast<NMITEMACTIVATE*>(lParam);
            const int ghostIdx = static_cast<int>(state_.roster.size());

            if (nm->iItem == ghostIdx && nm->iSubItem >= 1) {
                // Clicked the ghost "add" row — start a new student immediately
                // (single click is enough; no double-click needed for an empty row)
                CancelInlineCellEdit();
                cellEdit_.isNew = true;
                BeginInlineCellEdit(ghostIdx, 1);
            } else if (hdr->code == NM_DBLCLK &&
                       nm->iItem >= 0 &&
                       nm->iItem < ghostIdx &&
                       nm->iSubItem >= 1) {
                // Double-click an existing name cell → edit it
                BeginInlineCellEdit(nm->iItem, nm->iSubItem);
            }
            // Single click on an existing row → just selects (native highlight)
        } else if (hdr->idFrom == kRosterViewId && hdr->code == NM_RCLICK) {
            // Right-click on the roster list → context menu
            auto* nm = reinterpret_cast<NMITEMACTIVATE*>(lParam);

            // Count how many rows are selected
            int selCount = ListView_GetSelectedCount(controls_.rosterView);

            // If the click landed on an unselected row, select only that row
            if (nm->iItem >= 0 && nm->iItem < static_cast<int>(state_.roster.size())) {
                if (!(ListView_GetItemState(controls_.rosterView, nm->iItem, LVIS_SELECTED) & LVIS_SELECTED)) {
                    // Clear previous selection and select just this row
                    for (int i = ListView_GetItemCount(controls_.rosterView) - 1; i >= 0; --i)
                        ListView_SetItemState(controls_.rosterView, i, 0, LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_SetItemState(controls_.rosterView, nm->iItem,
                                          LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    selCount = 1;
                } else {
                    selCount = ListView_GetSelectedCount(controls_.rosterView);
                }
            }

            constexpr UINT kIdDelete    = 7001;
            constexpr UINT kIdAddNew    = 7002;

            HMENU menu = CreatePopupMenu();
            if (selCount > 0) {
                std::wstring label = selCount == 1
                    ? L"Delete Student"
                    : (L"Delete " + std::to_wstring(selCount) + L" Students");
                AppendMenuW(menu, MF_STRING, kIdDelete, label.c_str());
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            }
            AppendMenuW(menu, MF_STRING, kIdAddNew, L"Add New Student");

            POINT screenPt{}; GetCursorPos(&screenPt);
            const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                            screenPt.x, screenPt.y, 0, hwnd_, nullptr);
            DestroyMenu(menu);

            if (cmd == kIdDelete && selCount > 0) {
                DeleteSelectedRosterStudents();
            } else if (cmd == kIdAddNew) {
                // Trigger inline add (same as the old "+Add" button)
                SendMessageW(hwnd_, WM_COMMAND,
                             MAKEWPARAM(kAddStudentId, BN_CLICKED), 0);
            }
        }
        return 0;
    }
    case WM_COMMAND:
        return SendMessageW(hwnd_, WM_COMMAND, wParam, lParam);
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, renderer_.TextColor());
        SetBkColor(hdc, renderer_.PanelColor());
        SetBkMode(hdc, OPAQUE);
        return reinterpret_cast<LRESULT>(renderer_.PanelBrush());
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, renderer_.TextColor());
        SetBkColor(hdc, renderer_.WindowColor());
        SetBkMode(hdc, OPAQUE);
        return reinterpret_cast<LRESULT>(renderer_.InputBrush());
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(sidebar, &ps);
        renderer_.PaintInfoPanel(hdc, sidebar, layout_,
                                  sidebar_.ScrollOffset(), sidebar_.SectionDividers(),
                                  sidebar_.TotalHeaderH(), sidebar_.StatusH());
        EndPaint(sidebar, &ps);
        return 0;
    }
    default: return DefWindowProcW(sidebar, msg, wParam, lParam);
    }
}

// ---------------------------------------------------------------------------
// Main dispatch
// ---------------------------------------------------------------------------

LRESULT SeatingChartApp::HandleMessage(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:        return OnCreate(hwnd);
    case WM_SIZE:          return OnSize();
    case WM_GETMINMAXINFO: {
        auto* mm = reinterpret_cast<MINMAXINFO*>(lParam);
        int minW = MinWindowWidth(), minH = MinWindowHeight();
        // Never demand more than the monitor's work area, or the window can't be
        // dragged smaller (feels "not resizable") on small / high-DPI displays.
        if (HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)) {
            MONITORINFO mi{ sizeof(mi) };
            if (GetMonitorInfoW(mon, &mi)) {
                minW = std::min(minW, static_cast<int>(mi.rcWork.right  - mi.rcWork.left));
                minH = std::min(minH, static_cast<int>(mi.rcWork.bottom - mi.rcWork.top));
            }
        }
        mm->ptMinTrackSize = { minW, minH };
        return 0;
    }
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:  return OnThemeChange();
    case WM_DPICHANGED:    return OnDpiChanged(HIWORD(wParam), reinterpret_cast<RECT*>(lParam));
    case WM_ERASEBKGND:    return 1;
    case WM_TIMER:         return OnTimer(wParam);
    case WM_APP_AUTOASSIGN_DONE:     return OnAutoAssignDone(reinterpret_cast<AutoAssignResult*>(lParam));
    case WM_APP_AUTOASSIGN_PROGRESS: return OnAutoAssignProgress(static_cast<size_t>(wParam));
    case WM_APP_LAYOUT_FLOATING_TOOLS:
        LayoutFloatingTools();
        return 0;
    case WM_APP_INSPECTOR_SPIN:
        OnInspectorSpin(static_cast<int>(wParam), static_cast<int>(lParam));
        return 0;
    case WM_KEYDOWN:       return OnKeyDown(wParam,
                               (GetKeyState(VK_CONTROL)&0x8000)!=0,
                               (GetKeyState(VK_SHIFT)&0x8000)!=0);
    case WM_COMMAND:       return OnCommand(LOWORD(wParam), HIWORD(wParam));
    case WM_LBUTTONDOWN:   return OnLButtonDown(
                               {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)},
                               (wParam & MK_SHIFT) != 0);
    case WM_LBUTTONDBLCLK: return OnLButtonDblClk({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
    case WM_MOUSEMOVE:     return OnMouseMove({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, wParam);
    case WM_MOUSELEAVE:    return OnMouseLeave();
    case WM_LBUTTONUP:
        if (draggingFront_) {
            draggingFront_ = false;
            ReleaseCapture();
            if (state_.frontEdge != frontDragOriginalEdge_)
                CommitFrontEdge(state_.frontEdge);
            else
                InvalidateChart();
        } else if (resizingSidebar_) {
            resizingSidebar_ = false;
            ReleaseCapture();
            RecalculateLayout();
        } else if (rubberBandSelecting_) {
            ReleaseCapture();
            EndRubberBand({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        } else if (editor_.IsRotating()) {
            editor_.EndRotate();
        } else {
            editor_.EndDrag();
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (draggingFront_) {
            // Cancelled — restore original edge
            draggingFront_ = false;
            state_.frontEdge = frontDragOriginalEdge_;
            InvalidateChart();
        }
        resizingSidebar_ = false;
        if (rubberBandSelecting_) { rubberBandSelecting_ = false; InvalidateChart(); }
        editor_.CancelEdit(/*doRelease=*/false);
        return 0;
    case WM_CONTEXTMENU:   return OnContextMenu(reinterpret_cast<HWND>(wParam),
                               {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
    case WM_SETCURSOR:     return OnSetCursor(lParam);
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, renderer_.TextColor());
        SetBkColor(hdc, renderer_.PanelColor());
        SetBkMode(hdc, OPAQUE);
        return reinterpret_cast<LRESULT>(renderer_.PanelBrush());
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, renderer_.TextColor());
        SetBkColor(hdc, renderer_.WindowColor());
        SetBkMode(hdc, OPAQUE);
        return reinterpret_cast<LRESULT>(renderer_.InputBrush());
    }
    case WM_MOUSEWHEEL:
        if (sidebar_.ActiveTab() == 3 && (LOWORD(wParam) & MK_CONTROL)) {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const float desiredZoom = std::clamp(groupsZoom_ + (delta > 0 ? 0.1f : -0.1f), 0.5f, 5.0f);
            if (desiredZoom > groupsZoom_) {
                groupsZoom_ = renderer_.ClampGroupsZoomToFit(
                    hwnd_, layout_.chart, generatedGroups_, state_.showLastNames, desiredZoom);
            } else {
                groupsZoom_ = desiredZoom;
            }
            InvalidateChart();
            return 0;
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_PAINT:   return OnPaint();
    case WM_DESTROY: OnDestroy(); PostQuitMessage(0); return 0;
    default:         return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
