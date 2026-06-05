#pragma once

#include <string>
#include <windows.h>

void WriteAppLog(const std::wstring& message);
LONG WINAPI SeatingChartUnhandledExceptionFilter(EXCEPTION_POINTERS* info);
