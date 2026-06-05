#include "Templates.h"
#include "FileIO.h"
#include "Utils.h"
#include <nlohmann/json.hpp>
#include <windows.h>
#include <commdlg.h>
#include <algorithm>

using json = nlohmann::json;

static std::wstring TemplateDir() {
    auto base = GetAppDataStateDir();
    return base.empty() ? L"" : base + L"\\templates";
}

static std::wstring TemplatePath(const std::wstring& name) {
    auto dir = TemplateDir();
    return dir.empty() ? L"" : dir + L"\\" + name + L".json";
}

// ---------------------------------------------------------------------------

std::vector<std::wstring> ListTemplates() {
    const auto dir = TemplateDir();
    if (dir.empty()) return {};
    const std::wstring pattern = dir + L"\\*.json";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::vector<std::wstring> names;
    do {
        std::wstring fname(fd.cFileName);
        if (fname.size() > 5 && fname.substr(fname.size() - 5) == L".json")
            names.push_back(fname.substr(0, fname.size() - 5));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end());
    return names;
}

// Template JSON format version 2: includes rotation, locked, flipped,
// capacity, roomW, roomH.  Bounds are room-local logical coordinates.
bool SaveTemplate(const Template& tmpl) {
    const auto dir = TemplateDir();
    if (dir.empty()) return false;
    if (!EnsureDirectoryExists(dir)) return false;

    json j;
    // v4: the rows×cols grid is retired; templates no longer store "grid".
    // Older templates that carry it still load fine (the field is ignored).
    j["version"] = 4;
    j["name"]    = WideToUtf8(tmpl.name);
    j["room_w"]  = tmpl.roomW;
    j["room_h"]  = tmpl.roomH;
    j["layout_items"] = json::array();
    for (const auto& item : tmpl.layoutItems) {
        json ji;
        ji["type"]     = WideToUtf8(std::wstring(LayoutTypeName(item.type)));
        ji["label"]    = WideToUtf8(item.label);
        ji["rotation"] = item.rotation;
        ji["locked"]   = item.locked;
        ji["flipped"]  = item.flipped;
        ji["capacity"] = item.capacity;
        ji["bounds"]   = json::array({ static_cast<int>(item.bounds.left),
                                       static_cast<int>(item.bounds.top),
                                       static_cast<int>(item.bounds.right),
                                       static_cast<int>(item.bounds.bottom) });
        j["layout_items"].push_back(ji);
    }
    const std::string text = j.dump(2);
    const std::vector<unsigned char> bytes(text.begin(), text.end());
    return WriteAllBytesAtomic(TemplatePath(tmpl.name), bytes);
}

bool LoadTemplate(const std::wstring& name, Template* tmpl) {
    std::vector<unsigned char> bytes;
    if (!ReadAllBytes(TemplatePath(name), &bytes) || bytes.empty()) return false;
    try {
        const auto j = json::parse(std::string(
            reinterpret_cast<const char*>(bytes.data()), bytes.size()));

        const int version = j.value("version", 1);

        tmpl->name = Utf8ToWide(j.value("name", std::string{}));
        if (tmpl->name.empty()) tmpl->name = name;
        // "grid" (rows/cols) is ignored if present — the seat grid is retired.
        tmpl->roomW = j.value("room_w", 0);
        tmpl->roomH = j.value("room_h", 0);
        if (tmpl->roomW < 0) tmpl->roomW = 0;
        if (tmpl->roomH < 0) tmpl->roomH = 0;

        tmpl->layoutItems.clear();
        for (const auto& li : j.value("layout_items", json::array())) {
            LayoutItem item;
            item.type     = LayoutTypeFromName(Utf8ToWide(li.value("type", std::string{"RectangleDesk"})));
            item.label    = Utf8ToWide(li.value("label", std::string{}));
            if (item.label.empty()) item.label = std::wstring(LayoutTypeName(item.type));
            item.rotation = ((li.value("rotation", 0) % 360) + 360) % 360;
            item.locked   = li.value("locked",   false);
            item.flipped  = li.value("flipped",  false);
            item.capacity = li.value("capacity", 0);

            const auto& b = li.value("bounds", json::array({0, 0, 120, 60}));
            if (b.size() == 4)
                item.bounds = { b[0].get<int>(), b[1].get<int>(),
                                b[2].get<int>(), b[3].get<int>() };
            item.bounds = NormalizeRect(item.bounds);

            // v1 templates stored screen-space coords — migrate.
            if (version < 2) {
                constexpr int kOriginX = 28;
                constexpr int kOriginY = 64;
                item.bounds.left  -= kOriginX; item.bounds.right  -= kOriginX;
                item.bounds.top   -= kOriginY; item.bounds.bottom -= kOriginY;
                item.bounds = NormalizeRect(item.bounds);
            }

            // <v3: rotation swapped width/height for 90/270; un-swap so the
            // footprint is stored unrotated (rotation is display-only now).
            if (version < 3 && (item.rotation == 90 || item.rotation == 270)) {
                const LONG cx = (item.bounds.left + item.bounds.right) / 2;
                const LONG cy = (item.bounds.top  + item.bounds.bottom) / 2;
                const LONG w  = item.bounds.right  - item.bounds.left;
                const LONG h  = item.bounds.bottom - item.bounds.top;
                item.bounds = { cx - h/2, cy - w/2, cx + h/2, cy + w/2 };
            }

            tmpl->layoutItems.push_back(std::move(item));
        }
        return true;
    } catch (...) { return false; }
}

bool DeleteTemplate(const std::wstring& name) {
    const auto path = TemplatePath(name);
    if (path.empty()) return false;
    return DeleteFileW(path.c_str()) != 0;
}

Template TemplateFromState(const AppState& state, const std::wstring& name) {
    return { name, state.roomW, state.roomH, state.layoutItems };
}

void ApplyTemplate(const Template& tmpl, AppState* state) {
    state->layoutItems = tmpl.layoutItems;
    state->roomW = tmpl.roomW;
    state->roomH = tmpl.roomH;
    state->selectedLayoutItem = std::nullopt;
    state->selectedLayoutItems.clear();
    for (auto& item : state->layoutItems) item.selected = false;
}

// ---------------------------------------------------------------------------
// Dialog helpers
// ---------------------------------------------------------------------------

std::wstring PromptSaveTemplate(HWND owner, const AppState& state) {
    wchar_t buf[256]{};
    if (MessageBoxW(owner,
        L"Save current classroom layout as a template.\n\n"
        L"Click OK then choose a file name.",
        L"Save Template", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
        return {};

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = static_cast<DWORD>(std::size(buf));
    ofn.lpstrTitle  = L"Template name (without extension)";
    ofn.lpstrFilter = L"JSON Template\0*.json\0\0";
    ofn.lpstrDefExt = L"json";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    const auto dir = TemplateDir();
    if (!dir.empty()) { EnsureDirectoryExists(dir); ofn.lpstrInitialDir = dir.c_str(); }
    if (!GetSaveFileNameW(&ofn)) return {};

    std::wstring path(buf);
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring fname = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    if (fname.size() > 5 && fname.substr(fname.size() - 5) == L".json")
        fname = fname.substr(0, fname.size() - 5);

    const auto tmpl = TemplateFromState(state, fname);
    if (!SaveTemplate(tmpl)) {
        MessageBoxW(owner, L"Failed to save template.", L"Error", MB_ICONERROR | MB_OK);
        return {};
    }
    return fname;
}

bool PromptLoadTemplate(HWND owner, AppState* state) {
    const auto names = ListTemplates();
    if (names.empty()) {
        MessageBoxW(owner,
            L"No templates found.\n\nSave a template first using Save Template.",
            L"Load Template", MB_ICONINFORMATION | MB_OK);
        return false;
    }

    const auto dir = TemplateDir();
    wchar_t buf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"Select a classroom template";
    ofn.lpstrFilter = L"JSON Template\0*.json\0\0";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!dir.empty()) ofn.lpstrInitialDir = dir.c_str();
    if (!GetOpenFileNameW(&ofn)) return false;

    std::wstring path(buf);
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring fname = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    if (fname.size() > 5 && fname.substr(fname.size()-5) == L".json")
        fname = fname.substr(0, fname.size()-5);

    Template tmpl;
    if (!LoadTemplate(fname, &tmpl)) {
        MessageBoxW(owner, L"Failed to load template.", L"Error", MB_ICONERROR | MB_OK);
        return false;
    }
    ApplyTemplate(tmpl, state);
    return true;
}
