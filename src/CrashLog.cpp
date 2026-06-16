#include "CrashLog.h"

#include <cstdio>
#include <ctime>
#include <string>
#include <DbgHelp.h>

namespace {

std::wstring LogPath() {
    wchar_t base[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(base) : L".";
    dir += L"\\SeatingChartApp";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\crash.log";
}

std::wstring DumpDir() {
    wchar_t base[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(base) : L".";
    return dir + L"\\SeatingChartApp";
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

// Write a minidump alongside the crash log.
void WriteMiniDump(EXCEPTION_POINTERS* info) {
    // Build a timestamped filename so each crash produces its own dump.
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    wchar_t fname[64]{};
    wcsftime(fname, std::size(fname), L"crash_%Y%m%d_%H%M%S.dmp", &tm);
    std::wstring path = DumpDir() + L"\\" + fname;

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId          = GetCurrentThreadId();
    mei.ExceptionPointers = info;
    mei.ClientPointers    = FALSE;

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                      hFile,
                      static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs |
                                                  MiniDumpWithHandleData |
                                                  MiniDumpWithThreadInfo),
                      info ? &mei : nullptr,
                      nullptr, nullptr);
    CloseHandle(hFile);
    AppendLine(std::wstring(L"Minidump written: ") + fname);
}

// Capture a symbolic stack trace from the faulting thread's context.
void WriteStackTrace(EXCEPTION_POINTERS* info) {
    if (!info || !info->ContextRecord) {
        AppendLine(L"  [no context for stack trace]");
        return;
    }

    HANDLE hProc   = GetCurrentProcess();
    HANDLE hThread = GetCurrentThread();

    // Initialise DbgHelp — search path includes the exe directory for the PDB.
    // SymInitialize takes a narrow ANSI path.
    char exePath[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    // Strip the filename to get the directory.
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *lastSlash = '\0';

    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(hProc, exePath, TRUE);

    STACKFRAME64 frame{};
    CONTEXT ctx = *info->ContextRecord;

#ifdef _M_X64
    constexpr DWORD machineType    = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset            = ctx.Rip;
    frame.AddrFrame.Offset         = ctx.Rbp;
    frame.AddrStack.Offset         = ctx.Rsp;
#elif defined(_M_ARM64)
    constexpr DWORD machineType    = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset            = ctx.Pc;
    frame.AddrFrame.Offset         = ctx.Fp;
    frame.AddrStack.Offset         = ctx.Sp;
#else
    constexpr DWORD machineType    = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset            = ctx.Eip;
    frame.AddrFrame.Offset         = ctx.Ebp;
    frame.AddrStack.Offset         = ctx.Esp;
#endif
    frame.AddrPC.Mode    = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

    AppendLine(L"  Stack trace:");
    for (int depth = 0; depth < 32; ++depth) {
        if (!StackWalk64(machineType, hProc, hThread, &frame,
                         &ctx, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        if (frame.AddrPC.Offset == 0) break;

        // Try to resolve the symbol name.
        constexpr DWORD kNameLen = 256;
        char symBuf[sizeof(SYMBOL_INFO) + kNameLen]{};
        auto* sym        = reinterpret_cast<SYMBOL_INFO*>(symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen   = kNameLen;
        DWORD64 displacement = 0;
        const bool haveSym = SymFromAddr(hProc, frame.AddrPC.Offset,
                                         &displacement, sym) != 0;

        // Also try to get the source file and line.
        IMAGEHLP_LINE64 line{sizeof(IMAGEHLP_LINE64)};
        DWORD lineDisp = 0;
        const bool haveLine = SymGetLineFromAddr64(hProc, frame.AddrPC.Offset,
                                                   &lineDisp, &line) != 0;

        // Get the module name.
        IMAGEHLP_MODULE64 mod{sizeof(IMAGEHLP_MODULE64)};
        const bool haveMod = SymGetModuleInfo64(hProc, frame.AddrPC.Offset, &mod) != 0;

        wchar_t entry[512]{};
        if (haveSym && haveLine) {
            swprintf_s(entry, L"    #%d %hs!%hs+0x%llx  (%hs:%lu)",
                       depth,
                       haveMod ? mod.ModuleName : "?",
                       sym->Name,
                       static_cast<unsigned long long>(displacement),
                       line.FileName, line.LineNumber);
        } else if (haveSym) {
            swprintf_s(entry, L"    #%d %hs!%hs+0x%llx",
                       depth,
                       haveMod ? mod.ModuleName : "?",
                       sym->Name,
                       static_cast<unsigned long long>(displacement));
        } else {
            swprintf_s(entry, L"    #%d %hs!0x%016llx",
                       depth,
                       haveMod ? mod.ModuleName : "?",
                       static_cast<unsigned long long>(frame.AddrPC.Offset));
        }
        AppendLine(entry);
    }

    SymCleanup(hProc);
}

} // namespace

void WriteAppLog(const std::wstring& message) {
    AppendLine(message);
}

LONG WINAPI SeatingChartUnhandledExceptionFilter(EXCEPTION_POINTERS* info) {
    DWORD code    = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    void* address = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr;

    wchar_t buf[256]{};
    swprintf_s(buf, L"CRASH exception=0x%08lX address=%p", code, address);
    AppendLine(buf);

    // Capture the full stack trace so we can pinpoint the call site.
    WriteStackTrace(info);

    // Write a minidump that a debugger can open later.
    WriteMiniDump(info);

    MessageBoxW(nullptr,
                (std::wstring(L"Seating Chart crashed.\n\nA crash log and minidump were written to:\n") + DumpDir()).c_str(),
                L"Seating Chart Crash", MB_OK | MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}
