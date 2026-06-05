cmake_minimum_required(VERSION 3.24)
project(SeatingChartApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Vendored single-header nlohmann/json (vendor/nlohmann/json.hpp).
# No internet required at configure time — the header lives in the repo.
# ---------------------------------------------------------------------------
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)

# ---------------------------------------------------------------------------
# doctest — downloaded once at configure time (tests only).
# ---------------------------------------------------------------------------
include(CTest)

if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    TRUE
    )
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(doctest)
endif()

# ---------------------------------------------------------------------------
# Shared compile definitions
# ---------------------------------------------------------------------------
set(COMMON_DEFS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# ---------------------------------------------------------------------------
# Main application
# ---------------------------------------------------------------------------
add_executable(seating_chart_app WIN32
    src/main.cpp
    src/SeatingChartApp.cpp
    src/LayoutEditor.cpp
    src/SelectionManager.cpp
    src/AppState.cpp
    src/AutoAssign.cpp
    src/Controls.cpp
    src/FileIO.cpp
    src/CrashLog.cpp
    src/Renderer.cpp
    src/SidebarManager.cpp
    src/Templates.cpp
    src/Print.cpp
    src/Undo.cpp
)

target_compile_definitions(seating_chart_app PRIVATE ${COMMON_DEFS})
target_include_directories(seating_chart_app PRIVATE src)
target_link_libraries(seating_chart_app PRIVATE
    user32 gdi32 comctl32 comdlg32 advapi32 winspool
    nlohmann_json
)

# Copy bundled fonts next to the executable after each build
add_custom_command(TARGET seating_chart_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:seating_chart_app>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/fonts/Baloo2.ttf"
            "$<TARGET_FILE_DIR:seating_chart_app>/fonts/Baloo2.ttf"
    COMMENT "Copying bundled fonts to output directory"
)

if(MINGW)
    # -municode: wWinMain entry point.
    # -static*: bake the C++ runtime (libc++/libunwind/pthread) into the exe so
    #   it launches without libc++.dll etc. on PATH — a self-contained binary.
    target_link_options(seating_chart_app PRIVATE
        -municode -static -static-libgcc -static-libstdc++)
endif()

# ---------------------------------------------------------------------------
# Unit tests (console executable)
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
    add_executable(seating_chart_tests
        tests/test_main.cpp
        tests/test_AppState.cpp
        tests/test_FileIO.cpp
        tests/test_AutoAssign.cpp
        src/AppState.cpp
        src/FileIO.cpp
        src/AutoAssign.cpp
        src/Undo.cpp
    )

    target_compile_definitions(seating_chart_tests PRIVATE
        ${COMMON_DEFS}
        SEATING_CHART_HEADLESS
    )
    target_include_directories(seating_chart_tests PRIVATE src)
    target_link_libraries(seating_chart_tests PRIVATE
        user32 gdi32 advapi32
        nlohmann_json
        doctest::doctest
    )

    if(MINGW)
        target_link_options(seating_chart_tests PRIVATE
            -static -static-libgcc -static-libstdc++)
    endif()

    add_test(NAME seating_chart_tests COMMAND seating_chart_tests)
endif()
