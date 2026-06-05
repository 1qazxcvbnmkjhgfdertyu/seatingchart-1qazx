#include "CrashLog.h"

#include <cstdio>
#include <ctime>
#include <string>

namespace {

std::wstring LogPath() {
    wchar_t base[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(base) : L".";
    dir += L"\\SeatingChartApp";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\crash.log";
}

std::wstring Timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    wchar_t buf[64]{};
    wcsftime(buf, std::size(buf), L"%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

void AppendLine(const std::wstring& line) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, LogPath().c_str(), L"a, ccs=UTF-8") != 0 || !f) return;
    fwprintf(f, L"[%ls] %ls\r\n", Timestamp().c_str(), line.c_str());
    fclose(f);
}

} // namespace

void WriteAppLog(const std::wstring& message) {
    AppendLine(message);
}

LONG WINAPI SeatingChartUnhandledExceptionFilter(EXCEPTION_POINTERS* info) {
    DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    void* address = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr;

    wchar_t buf[256]{};
    swprintf_s(buf, L"CRASH exception=0x%08lX address=%p", code, address);
    AppendLine(buf);

    MessageBoxW(nullptr,
                (std::wstring(L"Seating Chart crashed.\n\nA crash log was written to:\n") + LogPath()).c_str(),
                L"Seating Chart Crash", MB_OK | MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}
