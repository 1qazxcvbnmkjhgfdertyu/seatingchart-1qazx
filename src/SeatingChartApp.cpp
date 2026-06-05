#include "SeatingChartApp.h"
#include "CrashLog.h"
#include "FileIO.h"
#include "Print.h"
#include "Templates.h"
#include "Utils.h"
#include <algorithm>
#include <commctrl.h>
#include <cmath>
#include <random>
#include <commdlg.h>
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

void SeatingChartApp::SetStatus(const std::wstring& text) {
    state_.status = text;
    UpdateSidebarText(state_, controls_);
    for (HWND h : {controls_.summaryLabel, controls_.statusLabel})
        if (h) RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE);
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
    SyncRosterEditFromRoster(state_, controls_);
    const auto rp = GetRosterFilePath();
    if (!rp.empty()) {
        std::wstring text;
        for (const auto& n : state_.roster) { text += n; text += L"\r\n"; }
        WriteTextFileUtf8Atomic(rp, text);
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
    if (!groups.empty()) tx->groupAffinities = std::move(groups);
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
        for (const auto& g : state_.groupAffinities) {
            if (!g.empty()) {
                text += L"Group: ";
                for (size_t i=0; i<g.size(); ++i) {
                    if (i>0) text += L" ";
                    text += g[i];
                }
                text += L"\r\n";
            }
        }
        WriteTextFileUtf8Atomic(rp, text);
    }
    SetStatus(std::to_wstring(state_.restrictions.size()) + L" keep-apart, "
              + std::to_wstring(state_.affinities.size()) + L" sit-near, "
              + std::to_wstring(state_.mustTogether.size()) + L" must-together rules");
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
    UpdateButtonState(state_, controls_, aaRunning_);
    SetStatus(L"Assigning seats…");
    UpdateWindow(controls_.sidebar);

    if (!BeginAutoAssign(hwnd_, state_, aaCancel_, aaThread_)) {
        aaRunning_ = false;
        SetStatus(L"Could not start auto-assign");
        UpdateButtonState(state_, controls_, aaRunning_);
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
    UpdateButtonState(state_, controls_, aaRunning_);
    return 0;
}

LRESULT SeatingChartApp::OnAutoAssignProgress(size_t steps) {
    const size_t limit = state_.autoAssignSearchLimit > 0 ? state_.autoAssignSearchLimit : 500000;
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
    SyncRosterEditFromRoster(state_, controls_);
    SyncRestrictionEditFromRules(state_, controls_);
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
    const int newWidth = std::clamp(cw - static_cast<int>(pt.x),
                                    Scale(kSidebarMinWidth), Scale(kSidebarMaxWidth));
    if (newWidth == sidebarWidth_) return;

    sidebarWidth_ = newWidth;
    layout_.client = rc;
    layout_.panel = { cw - sidebarWidth_, 0, cw, ch };
    layout_.chart = { Margin(), HeaderHeight(),
                      std::max(Margin() + Scale(40), cw - sidebarWidth_ - Margin()),
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
                auto makeLabel = [&](const wchar_t* text, int x, int y) {
                    CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                        x, y, 80, 22, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
                };
                makeLabel(L"Width", 12, 16);
                makeLabel(L"Height", 12, 50);
                s->wEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                    82, 14, 110, 24, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
                s->hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                    82, 48, 110, 24, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
                SetWindowTextW(s->wEdit, std::to_wstring(s->width).c_str());
                SetWindowTextW(s->hEdit, std::to_wstring(s->height).c_str());
                CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                    42, 88, 72, 28, hwnd, reinterpret_cast<HMENU>(IDOK), GetModuleHandleW(nullptr), nullptr);
                CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE,
                    122, 88, 72, 28, hwnd, reinterpret_cast<HMENU>(IDCANCEL), GetModuleHandleW(nullptr), nullptr);
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
        Scale(230), Scale(160), hwnd_, nullptr, GetModuleHandleW(nullptr), &ps);
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
    const int panelW = sidebarWidth_;
    layout_.panel = { cw - panelW, 0, cw, ch };
    layout_.chart = { Margin(), HeaderHeight(),
                      std::max(Margin() + Scale(40), cw - panelW - Margin()),
                      std::max(HeaderHeight() + Scale(40), ch - Margin()) };

    // Recompute viewport transform — items are in stable room-local coords
    // and never moved just because the window was resized.
    layoutTx_ = ComputeLayoutViewTransform(layout_.chart, state_.roomW, state_.roomH);

    SyncLayoutInspectorWithSelection(state_, controls_);

    if (controls_.sidebar) {
        MoveWindow(controls_.sidebar,
                   layout_.panel.left, layout_.panel.top,
                   layout_.panel.right - layout_.panel.left,
                   layout_.panel.bottom - layout_.panel.top, TRUE);
        sidebar_.Recalculate(controls_.sidebar, state_, controls_, renderer_);
    }
    LayoutFloatingTools();
    InvalidateChart();
}

// ---------------------------------------------------------------------------
// Message handlers
// ---------------------------------------------------------------------------

LRESULT SeatingChartApp::OnCreate(HWND hwnd) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_UPDOWN_CLASS};
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

    ApplyFontsToControls(controls_, renderer_);

    state_.Init();
    bool loaded = LoadState(&state_);
    if (loaded) state_.ClearUndoHistory();
    // SCAT1 deprecation warning
    const auto legacyPath = GetLegacyStateFilePath();
    const auto jsonPath = GetStateFilePath();
    if (!jsonPath.empty() && !legacyPath.empty()) {
        // If legacy exists and we might have used it (simple check: no JSON or old)
        // For plan: always warn if legacy file still present
        if (GetFileAttributesW(legacyPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            SetStatus(L"WARNING: Legacy SCAT1 state.txt detected. Re-save to migrate. Support will be removed in v9+.");
            // One time message box
            MessageBoxW(hwnd_, L"Legacy SCAT1 format detected.\n\nPlease use File > Save Now to migrate to JSON.\n\nSCAT1 support will be removed in a future release (planned v9).", L"SCAT1 Deprecation", MB_OK | MB_ICONWARNING);
        }
    }
    SyncAllEditsFromState();
    RefreshSelectionFlags();
    UpdateButtonState(state_, controls_, false);
    SetStatus(L"Ready");

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

LRESULT SeatingChartApp::OnThemeChange() {
    renderer_.ApplyThemeFromSystem();
    renderer_.RebuildFonts(dpi_);
    ApplyFontsToControls(controls_, renderer_);
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
    case kApplyRulesId:           if (notif==BN_CLICKED) { const auto t = GetWindowTextStr(controls_.restrictionEdit); ApplyRestrictions(SplitRestrictionInput(t), SplitAffinityInput(t), SplitTogetherInput(t), SplitGroupInput(t)); } break;
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

LRESULT SeatingChartApp::OnContextMenu(POINT screenPt) {
    // Keyboard-invoked menus arrive as (-1,-1); fall back to the cursor.
    if (screenPt.x == -1 && screenPt.y == -1) GetCursorPos(&screenPt);

    // Assign tool: right-clicking a seat edits that student's colour/tags.
    if (state_.chartMode == ChartMode::Seats) {
        POINT cp = screenPt; ScreenToClient(hwnd_, &cp);
        if (const auto seat = selection_.HitTestSeatSlot(cp)) {
            StudentContextMenu(*seat, screenPt);
            return 0;
        }
        return DefWindowProcW(hwnd_, WM_CONTEXTMENU, 0, MAKELPARAM(screenPt.x, screenPt.y));
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
    RECT rb{};
    if (rubberBandSelecting_) {
        rb = {
            std::min(rubberBandStart_.x, rubberBandEnd_.x),
            std::min(rubberBandStart_.y, rubberBandEnd_.y),
            std::max(rubberBandStart_.x, rubberBandEnd_.x),
            std::max(rubberBandStart_.y, rubberBandEnd_.y)
        };
    }
    renderer_.PaintWindowBuffered(hwnd_, hdc, state_, layout_.chart, {hoverItem_}, rb, dragPreview_);
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
                                  sidebar_.ScrollOffset(), sidebar_.SectionDividers());
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
        if (resizingSidebar_) {
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
        resizingSidebar_ = false;
        if (rubberBandSelecting_) { rubberBandSelecting_ = false; InvalidateChart(); }
        editor_.CancelEdit(/*doRelease=*/false);
        return 0;
    case WM_CONTEXTMENU:   return OnContextMenu(
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
    case WM_PAINT:   return OnPaint();
    case WM_DESTROY: OnDestroy(); PostQuitMessage(0); return 0;
    default:         return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
