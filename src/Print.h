#pragma once
#include "AppState.h"
#include <windows.h>

class Renderer;

// Shows the Windows print dialog and prints the seating chart.
// Returns false if the user cancels or a print error occurs.
[[nodiscard]] bool PrintChart(HWND owner, const AppState& state, const Renderer& renderer);
