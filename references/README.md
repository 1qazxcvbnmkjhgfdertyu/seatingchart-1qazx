# Seating Chart / Classroom App GitHub References

Downloaded/cloned on 2026-04 (approx) for reference.

**Location:** `Documents/seating-chart-app/references/` (inside your main app tree for easy reference while developing)

All are full `git clone` copies (with `.git` folders preserved; you can `git pull` inside any if you want updates). 
Note: On Windows (case-insensitive FS), some similarly-named repos were given owner-prefixed directory names to avoid conflicts.

---

## TEACHER-FOCUSED SEATING CHART APPS

| Original Repo | Local Dir | GitHub URL |
|---------------|-----------|------------|
| Classy (FayeKeegan) | `Classy/` | https://github.com/FayeKeegan/Classy |
| seating-randomizer (hannahmcdowell) | `seating-randomizer/` | https://github.com/hannahmcdowell/seating-randomizer |
| SeatingChartGenerator (vanjac) | `SeatingChartGenerator/` | https://github.com/vanjac/SeatingChartGenerator |

---

## SEATING ALGORITHM / OPTIMIZATION LOGIC

| Original Repo | Local Dir | GitHub URL |
|---------------|-----------|------------|
| seatingchart (opelr) | `opelr-seatingchart/` | https://github.com/opelr/seatingchart |
| seating-chart (sjtryba) | `sjtryba-seating-chart/` | https://github.com/sjtryba/seating-chart |
| seatingChart (ztblick) | `ztblick-seatingChart/` | https://github.com/ztblick/seatingChart |
| seating-planner (spookylukey) | `seating-planner/` | https://github.com/spookylukey/seating-planner |

---

## DRAG-AND-DROP UI COMPONENTS

| Original Repo | Local Dir | GitHub URL |
|---------------|-----------|------------|
| react-seatmap-creator (cenksari) | `react-seatmap-creator/` | https://github.com/cenksari/react-seatmap-creator |
| seatmap (Ibrahimrahhal) | `seatmap/` | https://github.com/ibrahimrahhal/seatmap |
| Seating-Chart-by-drag-and-drop-of-image (mumahsan) | `Seating-Chart-by-drag-and-drop-of-image/` | https://github.com/mumahsan/Seating-Chart-by-drag-and-drop-of-image |

---

## Notes

- **Classy**: Ruby on Rails app (older, ~2015), includes models for classrooms, students, seating charts, etc. Has screenshots and wireframes in `docs/`.
- **SeatingChartGenerator**: Processing (Java) based, with GUI and printer components.
- **Seating-Chart-by-drag-and-drop-of-image**: Older ASP.NET / C# project with image-based drag-drop.
- **react-seatmap-creator** and **seatmap**: Modern React/TSX drag-and-drop seat map UIs.
- **seating-planner**: Python with simulated annealing solver for optimization.
- **opelr/seatingchart**, **sjtryba**, **ztblick**: Various Python seating logic/algorithms.
- **seating-randomizer**: Fullstack JS (React + Node) classroom/seating app.

You can explore each folder directly. Many have README.md with more details.

There's also an existing `Documents/seating-chart-app/` and zips in your Documents — these GitHub clones are additional reference material.

Enjoy building your seating chart app! If you need help analyzing any of these (e.g. extracting key algorithms or UI patterns), just let me know which one(s).