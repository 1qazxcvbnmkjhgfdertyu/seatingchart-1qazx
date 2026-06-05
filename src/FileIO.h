#pragma once
#include "AppState.h"
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Raw I/O
// ---------------------------------------------------------------------------
[[nodiscard]] bool ReadAllBytes(const std::wstring& path,
                                 std::vector<unsigned char>* bytes);
[[nodiscard]] bool WriteAllBytesAtomic(const std::wstring& path,
                                        const std::vector<unsigned char>& bytes);
[[nodiscard]] bool ReadTextFileUtf8(const std::wstring& path, std::wstring* out);
[[nodiscard]] bool WriteTextFileUtf8Atomic(const std::wstring& path,
                                            const std::wstring& text);

std::wstring Utf8ToWide(const std::string& text);
std::string  WideToUtf8(const std::wstring& text);

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------
[[nodiscard]] std::wstring GetAppDataStateDir();
[[nodiscard]] std::wstring GetStateFilePath();   // state.json (new format)
[[nodiscard]] std::wstring GetLegacyStateFilePath(); // state.txt (SCAT1)
[[nodiscard]] std::wstring GetRosterFilePath();
[[nodiscard]] std::wstring GetRestrictionsFilePath();
[[nodiscard]] bool EnsureDirectoryExists(const std::wstring& dir);

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

// JSON (current format — order-independent, forward-compatible)
[[nodiscard]] std::string  BuildStateJson(const AppState& state);
[[nodiscard]] bool LoadStateFromJson(const std::string& json, AppState* state);

// SCAT1 (legacy text format — read-only, for migrating old saves)
[[nodiscard]] bool ValidateStateText(const std::wstring& text);
[[nodiscard]] bool LoadStateFromText(const std::wstring& text, AppState* state);

// ---------------------------------------------------------------------------
// High-level load / save
// ---------------------------------------------------------------------------
// Tries JSON first, falls back to SCAT1 if state.json absent.
[[nodiscard]] bool LoadState(AppState* state);

// showSuccessStatus: sets state->status = L"Saved" on success.
void SaveStateNow(AppState* state, bool showSuccessStatus = false);
void ScheduleAutoSave(AppState* state, HWND hwnd);
