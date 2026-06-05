#pragma once
#include "AppState.h"
#include <string>
#include <vector>

// A template captures the classroom furniture layout (and room size) without
// roster or seat assignments. Stored as JSON in:
//   %APPDATA%\SeatingChartApp\templates\<name>.json

struct Template {
    std::wstring            name;
    int                     roomW = 0; // logical room units (0 = default)
    int                     roomH = 0;
    std::vector<LayoutItem> layoutItems;
};

// Returns available template names (no extension).
[[nodiscard]] std::vector<std::wstring> ListTemplates();

// Saves the current furniture layout as a named template.
// Returns false on I/O error.
[[nodiscard]] bool SaveTemplate(const Template& tmpl);

// Loads a template by name. Returns false if not found or invalid.
[[nodiscard]] bool LoadTemplate(const std::wstring& name, Template* tmpl);

// Deletes a template file. Returns false if not found.
[[nodiscard]] bool DeleteTemplate(const std::wstring& name);

// Convenience: extract layout from AppState into a Template struct.
[[nodiscard]] Template TemplateFromState(const AppState& state,
                                          const std::wstring& name);

// Apply a template to state (replaces furniture layout + room size; leaves the
// roster and any existing seat assignments untouched).
void ApplyTemplate(const Template& tmpl, AppState* state);

// Prompt user for a name and save current layout as a template.
// Returns the name chosen, or empty string if cancelled.
[[nodiscard]] std::wstring PromptSaveTemplate(HWND owner, const AppState& state);

// Prompt user to pick a template and apply it.
// Returns false if cancelled.
[[nodiscard]] bool PromptLoadTemplate(HWND owner, AppState* state);
