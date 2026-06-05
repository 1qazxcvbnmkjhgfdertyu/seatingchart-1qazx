#pragma once
#include "AppState.h"
#include "Controls.h"
#include "Renderer.h"
#include <vector>

// ---------------------------------------------------------------------------
// SidebarManager
//
// Owns all sidebar scroll state and section-divider tracking.
// RecalculateSidebarLayout is split into focused sub-functions.
// ---------------------------------------------------------------------------
class SidebarManager {
public:
    SidebarManager()  = default;
    ~SidebarManager() = default;

    SidebarManager(const SidebarManager&)            = delete;
    SidebarManager& operator=(const SidebarManager&) = delete;

    // --- Main entry point: recompute positions and defer-move all controls ---
    void Recalculate(HWND sidebar, const AppState& state,
                     const ControlHandles& c, const Renderer& renderer);

    // --- Scroll ---
    void ScrollTo(HWND sidebar, int newPos);
    void HandleVScroll(HWND sidebar, WPARAM wParam,
                       const AppState& state, const ControlHandles& c,
                       const Renderer& renderer);
    void HandleMouseWheel(HWND sidebar, int wheelDelta,
                          const AppState& state, const ControlHandles& c,
                          const Renderer& renderer);

    // --- Accessors (for Renderer::PaintInfoPanel) ---
    const std::vector<int>& SectionDividers() const { return sectionDividers_; }
    int ScrollOffset() const { return scroll_; }
    int HeaderH()     const { return headerH_; }
    int StatusH()     const { return statusH_; }

private:
    int scroll_   = 0;
    int contentH_ = 0;
    int viewH_    = 0;
    int pad_ = 16;
    int gap_ = 8;
    int lineH_ = 18;
    int labelH_ = 22;
    int buttonH_ = 30;
    int editH_ = 24;
    int sectionGap_ = 16;
    int headerH_ = 104;
    int statusH_ = 58;
    int scrollLine_ = 36;
    std::vector<int> sectionDividers_;

    // Sub-functions called by Recalculate
    void RebuildMetrics(HWND sidebar, const Renderer& renderer);
    int  LayoutCommonStrip   (HDWP& dwp, const ControlHandles& c,
                               int px, int pw, int base, int scrollUsed);
    int  LayoutSeatsModePanel(HDWP& dwp, const ControlHandles& c,
                               int px, int pw, int base, int scrollUsed);
    int  LayoutLayoutModePanel(HDWP& dwp, const ControlHandles& c,
                                int px, int pw, int base, int scrollUsed);
    void UpdateControlVisibility(const ControlHandles& c, ChartMode mode);
    void UpdateScrollBar(HWND sidebar, int contentH, int viewH);
    void ClampScroll(HWND sidebar);

    // Defer for scrolled controls — clamps to scrollable band so controls can't
    // paint over the fixed header or footer.  DeferFixed skips the clamp.
    void Defer     (HDWP& dwp, HWND child, int x, int y, int w, int h);
    void DeferFixed(HDWP& dwp, HWND child, int x, int y, int w, int h);
};
