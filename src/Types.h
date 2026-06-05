#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <optional>

// Shared types used across the app.
// Extracted to break circular dependencies and keep headers lightweight.

enum class ChartMode { Seats, Layout };
enum class LayoutItemType { Smartboard, TrapezoidDesk, RectangleDesk, Table4, BigTable, TrapPair, TrapPod, LDesk, UDesk };
enum class ResizeHandle { None, TopLeft, TopRight, BottomLeft, BottomRight };

// Which edge of the room is "the front" (where the board/teacher is). Used to
// rank seats for front-row rules and to draw the FRONT banner. Different
// layouts orient differently, so this is a per-chart property.
enum class RoomEdge { Top, Bottom, Left, Right };

struct LayoutItem {
    LayoutItemType type{};
    RECT           bounds{};
    bool           selected  = false;
    std::wstring   label;
    int            rotation  = 0;
    bool           locked    = false;
    bool           flipped   = false;
    int            capacity  = 0;
    std::vector<std::wstring> occupants;
    bool           visible   = true; // for layer visibility toggle
    int            groupId   = 0;    // 0 = ungrouped; matching ids move/select together
};

using LayoutSeatRef = std::pair<int, int>;
