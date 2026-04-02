# ============================================================
# NeuronOS — Windows Build Script (PowerShell)
#
# Prerequisites:
#   1. Visual Studio 2022 (Community) with C++ workload, OR
#      LLVM/Clang + Ninja (recommended, matches CI)
#   2. CMake 3.14+ (winget install Kitware.CMake)
#   3. Git (for submodule checkout)
#
# Usage:
#   .\scripts\build-windows.ps1
#   .\scripts\build-windows.ps1 -Compiler msvc
#   .\scripts\build-windows.ps1 -Compiler clang -GPU vulkan
#
# Output: build\bin\neuronos.exe
# ============================================================

param(
    [ValidateSet("clang", "msvc")]
    [string]$Compiler = "msvc",

    [ValidateSet("none", "vulkan", "cuda")]
    [string]$GPU = "none",

    [switch]$Clean,
    [switch]$Test,
    [switch]$Release
)

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "================================================" -ForegroundColor Cyan
Write-Host "  NeuronOS Windows Build" -ForegroundColor Cyan
Write-Host "  Compiler: $Compiler | GPU: $GPU" -ForegroundColor Cyan
Write-Host "================================================" -ForegroundColor Cyan
Write-Host ""

# ── Check prerequisites ──
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    Write-Host "ERROR: cmake not found. Install with: winget install Kitware.CMake" -ForegroundColor Red
    exit 1
}

$git = Get-Command git -ErrorAction SilentlyContinue
if (-not $git) {
    Write-Host "ERROR: git not found." -ForegroundColor Red
    exit 1
}

# ── Navigate to repo root ──
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (Test-Path "$PSScriptRoot\..\CMakeLists.txt") {
    $repoRoot = Split-Path -Parent $PSScriptRoot
}
Push-Location $repoRoot

# ── Initialize submodules ──
Write-Host "[1/5] Checking submodules..." -ForegroundColor Yellow
if (-not (Test-Path "3rdparty\llama.cpp\CMakeLists.txt")) {
    Write-Host "  Initializing llama.cpp submodule..."
    git submodule update --init --recursive 3rdparty/llama.cpp
}

# ── Clean if requested ──
if ($Clean -and (Test-Path "build")) {
    Write-Host "[2/5] Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force "build"
}

# ── Configure ──
Write-Host "[2/5] Configuring CMake..." -ForegroundColor Yellow

$buildType = if ($Release) { "Release" } else { "RelWithDebInfo" }
$cmakeArgs = @(
    "-B", "build",
    "-DCMAKE_BUILD_TYPE=$buildType",
    "-DBITNET_X86_TL2=OFF",
    "-DBUILD_SHARED_LIBS=OFF"
)

# GPU options
if ($GPU -eq "vulkan") {
    $cmakeArgs += "-DNEURONOS_VULKAN=ON"
} else {
    $cmakeArgs += "-DNEURONOS_VULKAN=OFF"
}

if ($GPU -eq "cuda") {
    $cmakeArgs += "-DNEURONOS_CUDA=ON"
} else {
    $cmakeArgs += "-DNEURONOS_CUDA=OFF"
    $cmakeArgs += "-DGGML_CUDA=OFF"
}

# Compiler
if ($Compiler -eq "clang") {
    $ninja = Get-Command ninja -ErrorAction SilentlyContinue
    if (-not $ninja) {
        Write-Host "  Ninja not found, installing..." -ForegroundColor DarkYellow
        winget install Ninja-build.Ninja --silent 2>$null
    }
    $cmakeArgs += @("-G", "Ninja", "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++")
} else {
    # MSVC: use default generator (Visual Studio)
    $cmakeArgs += @("-DCMAKE_C_COMPILER=cl", "-DCMAKE_CXX_COMPILER=cl")
}

cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: CMake configuration failed" -ForegroundColor Red
    Pop-Location
    exit 1
}

# ── Build ──
Write-Host "[3/5] Building..." -ForegroundColor Yellow
$jobs = $env:NUMBER_OF_PROCESSORS
if (-not $jobs) { $jobs = 4 }
cmake --build build --config $buildType -j $jobs
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build failed" -ForegroundColor Red
    Pop-Location
    exit 1
}

# ── Verify binary ──
Write-Host "[4/5] Verifying..." -ForegroundColor Yellow
$binary = "build\bin\neuronos.exe"
if (-not (Test-Path $binary)) {
    $binary = "build\bin\$buildType\neuronos.exe"
}
if (-not (Test-Path $binary)) {
    Write-Host "ERROR: Binary not found" -ForegroundColor Red
    Pop-Location
    exit 1
}

$size = (Get-Item $binary).Length / 1MB
Write-Host "  Binary: $binary ($([math]::Round($size, 1)) MB)" -ForegroundColor Green

# ── Test ──
if ($Test) {
    Write-Host "[5/5] Running tests..." -ForegroundColor Yellow

    $testBins = @("test_hal", "test_memory", "test_json")
    foreach ($t in $testBins) {
        $testPath = "build\bin\$t.exe"
        if (-not (Test-Path $testPath)) {
            $testPath = "build\bin\$buildType\$t.exe"
        }
        if (Test-Path $testPath) {
            Write-Host "  Running $t..."
            & $testPath
            if ($LASTEXITCODE -ne 0) {
                Write-Host "  FAILED: $t" -ForegroundColor Red
            } else {
                Write-Host "  PASSED: $t" -ForegroundColor Green
            }
        }
    }
} else {
    Write-Host "[5/5] Skipping tests (use -Test to run)" -ForegroundColor DarkYellow
}

# ── Done ──
Write-Host ""
Write-Host "================================================" -ForegroundColor Green
Write-Host "  Build successful!" -ForegroundColor Green
Write-Host "  Binary: $binary" -ForegroundColor Green
Write-Host "" -ForegroundColor Green
Write-Host "  Quick start:" -ForegroundColor Green
Write-Host "    .\$binary hwinfo          # Check hardware" -ForegroundColor White
Write-Host "    .\$binary model recommend  # Find best model" -ForegroundColor White
Write-Host "    .\$binary                  # Start agent" -ForegroundColor White
Write-Host "    .\$binary serve            # Start server" -ForegroundColor White
Write-Host "================================================" -ForegroundColor Green

Pop-Location
