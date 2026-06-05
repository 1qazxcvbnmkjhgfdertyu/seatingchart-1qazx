# Seating Chart App

Native Win32 C++ classroom seating chart tool. It uses Win32 controls and GDI drawing, stores state in AppData as JSON, and supports light/dark Windows theme colors.

## Features

- Configurable grid up to 12 rows × 10 columns (default 8 × 6).
- Paste or load a class roster; duplicate names are rejected after case-folding and whitespace normalization.
- Manually assign or clear a selected seat.
- Smart auto-assign with backtracking solver that respects adjacency restrictions, must-together pairs, "Behavior" tag auto keep-apart, and preserves fixed seats. Includes post-SA refinement for soft affinities. Stale results (state changed while search ran) are automatically discarded.
- Pre-solve feasibility warnings dialog with specific issues listed.
- Affinity satisfaction % shown after auto-assign (e.g. "87% affinities met").
- Quick-fill now does random shuffle of unassigned students for variety.
- Richer roster input: "Name [tag1, tag2]" syntax auto-populates student tags on import.
- Export current seating to CSV (Name, Desk, Seat, Tags, Notes).
- Roster filter/search box (filters list by name or tag).
- "must together" rules (A == B syntax in rules box; enforced on same furniture item).
- Weighted affinities support in parser (A + B @5) and satisfaction scoring.
- Group affinities / clusters ("Group: Alice Bob Charlie" in rules; students kept on same furniture item).
- Configurable auto-assign search limit (persisted in state.json as auto_assign_limit).
- Post-solve stats include steps used.
- Built-in layout preset button (e.g. "Preset: Rows") for quick classroom setups.
- Per-student forbidden desks skeleton (forbiddenDesks in student info, enforced in solver for whitelisting).
- Expanded pre-solve conflict detection (must-together vs restrictions, group vs keep-apart).
- Classroom layout mode: add/move/resize/delete smartboards, trapezoid desks, rectangle desks, and 4-person tables. Every committed layout edit is undoable.
- Classroom templates: save and reload named layout presets (grid size + furniture, without roster or assignments).
- Autosave to `%APPDATA%\SeatingChartApp\state.json` (JSON, version 2 format).
- Reads legacy `state.txt` (SCAT1 text format) on first run, then migrates to JSON.
- Capture the chart to the Windows clipboard (Ctrl+C when focus is not in an edit control).
- Export chart as a BMP file (Ctrl+E).
- Print chart (Ctrl+P).
- Full undo/redo (up to 50 levels) for all seat, roster, restrictions, layout item, and grid changes.
- Per-monitor DPI awareness: layout item bounds are scaled on load if saved at a different DPI.

## Build Dependencies

| Dependency | Source |
|---|---|
| [nlohmann/json](https://github.com/nlohmann/json) single-header | Vendored at `vendor/nlohmann/json.hpp` — no internet required at configure time |
| [doctest](https://github.com/doctest/doctest) v2.4.11 | Downloaded once by CMake FetchContent (test target only) |

Required Windows libraries (linked automatically by CMake): `user32`, `gdi32`, `comctl32`, `comdlg32`, `advapi32`, `winspool`.

## Building

### MSVC (Visual Studio 2022)

Requirements: Windows 10+, Visual Studio 2022 with *Desktop development with C++*, CMake 3.24+.

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release
```

Executable: `build-msvc\Release\seating_chart_app.exe`

### ARM64 Windows (Clang / MSYS2 clangarm64)

```powershell
cmake -S . -B build-arm64 -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER=C:/msys64/clangarm64/bin/clang.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/clangarm64/bin/clang++.exe `
  -DCMAKE_MAKE_PROGRAM=C:/msys64/clangarm64/bin/mingw32-make.exe
cmake --build build-arm64
```

Executable: `build-arm64\seating_chart_app.exe`

### x64 Windows (MinGW / MSYS2 mingw64)

```powershell
cmake -S . -B build-x64 -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/mingw32-make.exe
cmake --build build-x64
```

Executable: `build-x64\seating_chart_app.exe`

### Run unit tests

```powershell
cmake -S . -B build-tests
cmake --build build-tests
build-tests\seating_chart_tests.exe
```

## Usage

### Roster

Paste one student per line into the roster input box, then click **Import Roster**. You can also click **Load from File** to open a `.txt`, `.csv`, or `.tsv` file. Duplicate names (after trimming, case folding, and whitespace normalization) are rejected.

### Restrictions & Together Rules

Enter rules in the box (one per line) then **Apply Rules**:
- Keep-apart: `Student A | Student B` or `A != B` (or `A <> B`, `A , B`). Optional radius: `A | B @150`.
- Sit-near (soft, for auto-assign refinement): `A + B` or `A & B`.
- **New: Must sit together** (hard, e.g. on same table/furniture): `A == B` or `A = B`.

Roster import now supports richer names with tags: `Alice [Front row, Behavior]` or `Bob[Quiet zone]`. Tags like "Behavior" auto-apply keep-apart rules in Smart Auto-Assign (no two on same furniture item). "Front row" still forces front band.

**Quick Fill** now shuffles unassigned students for random-but-varied seating (more like the reference randomizers).

### Manual assignment

Click a seat, type a name in the seat edit box, then click **Assign**. You can also select a name in the roster list and click **Assign Selected**, or double-click a roster name. Assigning a student who is already seated moves them to the selected seat.

### Auto-assign

**Smart Auto-Assign** fills empty seats with unassigned roster students while treating existing occupied seats as fixed. If fixed seats already violate a restriction, or no valid layout is found within the search limit, the current chart is left unchanged. If the chart changes while the search is running, the stale result is discarded.

### Layout mode

Switch to **Layout** to arrange classroom furniture. Click an item to select it; drag to move, or drag a corner handle to resize. Press **Escape** to cancel a drag/resize in progress (bounds are restored). Press **Delete** or click **Delete Item** to remove the selected item. All move/resize/add/delete operations are undoable.

Use **Save Template** / **Load Template** to store and recall named room layouts (grid size + furniture only; roster and assignments are not saved in templates).

### Export and print

| Action | Keyboard | Button |
|---|---|---|
| Copy chart to clipboard | Ctrl+C (when focus is not in a text box) | Capture Chart |
| Export chart as BMP | Ctrl+E | Export Chart |
| Print chart | Ctrl+P | Print Chart |
| Save state now | Ctrl+S | Save Now |

### Keyboard shortcuts

| Key | Action |
|---|---|
| `Ctrl+S` | Save now |
| `Ctrl+Z` | Undo |
| `Ctrl+Y` / `Ctrl+Shift+Z` | Redo |
| `Ctrl+D` | Duplicate selected layout item(s) |
| `Ctrl+C` | Copy chart (when focus is not in a text box) |
| `Ctrl+E` | Export chart as BMP |
| `Ctrl+P` | Print chart |
| `Delete` | Clear selected seat / delete selected layout item |
| `Escape` | Cancel active layout drag/resize; or clear layout selection |
| Arrow keys | Navigate seats in Seats mode |

Shortcuts are active even when a sidebar control has focus, except where noted (Ctrl+C, Ctrl+Z, Delete preserve normal text-editing behavior inside edit controls).

## Save format

State is saved as UTF-8 JSON at `%APPDATA%\SeatingChartApp\state.json` (current version 8). A backup copy (`state.json.bak`) is kept of the last valid save. The JSON schema includes:

- `version` (2), `save_dpi` — layout bounds are scaled on load if DPI differs.
- `grid` — `rows`, `cols`.
- `mode` — `"Seats"` or `"Layout"`.
- `roster` — array of name strings.
- `seats` — array of occupant strings (exactly `rows × cols` entries).
- `restrictions` — array of `{first, second}` pairs.
- `must_together` — array of must-sit-together pairs.
- `group_affinities` — array of student name arrays for clustering.
- `auto_assign_limit` — search step limit for solver.
- `group_affinities` — array of arrays for student clusters.
- `layout_items` — array of `{type, label, bounds}` objects.

Unknown `mode` values and unknown `type` values are rejected rather than silently defaulted.

Legacy SCAT1 (`state.txt`) files are read-only for migration. If no JSON save exists, the app reads the SCAT1 file and immediately saves it as JSON.

**Deprecation Plan (formal):** SCAT1 support is read-only. On load of legacy file, a warning is shown and a MessageBox prompts migration. Legacy file presence triggers warning in status. Planned removal in v9 (2027). Users should migrate by re-saving. No new features will support SCAT1. See code in FileIO.cpp:LoadState and SeatingChartApp.cpp for implementation.

## References & Inspiration

A collection of related open-source seating chart projects (teacher apps, solvers/algorithms, and drag-and-drop UIs) has been moved into `references/` inside this repo for convenient browsing while developing.

See [references/README.md](references/README.md) for the full list and original GitHub links.

These were analyzed for ideas. See the analysis and proposed improvements in the development conversation / plan. Key areas with transferable concepts:
- Better CSV/Excel roster import with per-student attributes and "allowed desks" (vanjac/SeatingChartGenerator + sjtryba + ztblick).
- Simulated annealing / connection-matrix scoring for soft "sit near friends" or group optimization (spookylukey/seating-planner).
- Per-student constraints, proximity rules, fill-order randomization (vanjac).
- Richer multi-class / section / multiple-chart models and drag-from-list UX (FayeKeegan/Classy, hannahmcdowell/seating-randomizer).
- Advanced interactive seat map editors (cenksari/react-seatmap-creator, ibrahimrahhal/seatmap).

The native C++ solver (src/AutoAssign.*) and layout system (src/LayoutEditor.*) are already fairly advanced; the references provide concrete extensions rather than wholesale replacement.
