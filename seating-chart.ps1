$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $projectRoot 'build-arm64'
$exe = Join-Path $buildDir 'seating_chart_app.exe'
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'
$cCompiler = 'C:/msys64/clangarm64/bin/clang.exe'
$cxxCompiler = 'C:/msys64/clangarm64/bin/clang++.exe'
$makeProgram = 'C:/msys64/clangarm64/bin/mingw32-make.exe'

if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

if (Test-Path $exe) {
    $running = Get-Process seating_chart_app -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and ([IO.Path]::GetFullPath($_.Path) -ieq [IO.Path]::GetFullPath($exe)) }
    if ($running) {
        Write-Host 'seating-chart: app is already running; skipped rebuild because Windows locks the executable.'
        Write-Host 'seating-chart: close the app and run this command again to rebuild latest changes.'
        Start-Process -FilePath $exe -WorkingDirectory $buildDir
        exit 0
    }
}

# Configure only if needed
if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    & $cmake -S $projectRoot -B $buildDir -G 'MinGW Makefiles' `
        -DCMAKE_C_COMPILER=$cCompiler `
        -DCMAKE_CXX_COMPILER=$cxxCompiler `
        -DCMAKE_MAKE_PROGRAM=$makeProgram
}

# Always build (incremental) so you always get the latest changes
& $cmake --build $buildDir

Start-Process -FilePath $exe -WorkingDirectory $buildDir
