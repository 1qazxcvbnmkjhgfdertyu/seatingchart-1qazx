#include "Print.h"
#include "Renderer.h"
#include "Utils.h"
#include <commdlg.h>
#include <winspool.h>

bool PrintChart(HWND owner, const AppState& state, const Renderer& renderer) {
    PRINTDLGW pd{};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner   = owner;
    pd.Flags       = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;

    if (!PrintDlgW(&pd)) return false;

    HDC printerDC = pd.hDC;
    if (!printerDC) {
        if (pd.hDevMode)  GlobalFree(pd.hDevMode);
        if (pd.hDevNames) GlobalFree(pd.hDevNames);
        return false;
    }

    const int pageW = GetDeviceCaps(printerDC, HORZRES);
    const int pageH = GetDeviceCaps(printerDC, VERTRES);
    const int marginX = pageW / 20;
    const int marginY = pageH / 20;
    const int drawW   = pageW - marginX * 2;
    const int drawH   = pageH - marginY * 2;

    // The chart is always the furniture room now; use its logical dimensions
    // to establish the print aspect ratio.
    const int srcW = EffectiveRoomW(state.roomW);
    const int srcH = EffectiveRoomH(state.roomH);

    const double scaleX = drawW / static_cast<double>(srcW);
    const double scaleY = drawH / static_cast<double>(srcH);
    const double scale  = std::min(scaleX, scaleY);
    const int cw = static_cast<int>(srcW * scale);
    const int ch = static_cast<int>(srcH * scale);
    const int cx = marginX + (drawW - cw) / 2;
    const int cy = marginY + (drawH - ch) / 2;
    const RECT chartRect{ cx, cy, cx + cw, cy + ch };

    bool ok = false;
    DOCINFOW di{sizeof(di), L"Seating Chart"};
    if (StartDocW(printerDC, &di) <= 0) goto cleanup;
    if (StartPage(printerDC)      <= 0) goto abort;

    renderer.PaintChart(printerDC, state, chartRect, {});

    EndPage(printerDC);
    EndDoc(printerDC);
    ok = true;
    goto cleanup;

abort:
    AbortDoc(printerDC);

cleanup:
    DeleteDC(printerDC);
    if (pd.hDevMode)  GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);
    return ok;
}
