param(
    [string]$Compiler = "gcc",
    [string]$Output = "bin/kmeans.exe",
    [switch]$NoOpenMp
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$msysUcrtBin = "C:\msys64\ucrt64\bin"
$msysUsrBin = "C:\msys64\usr\bin"

if (-not (Test-Path $msysUcrtBin)) {
    throw "No se encontro MSYS2 UCRT64 en '$msysUcrtBin'."
}
if (-not (Test-Path $msysUsrBin)) {
    throw "No se encontro MSYS2 usr/bin en '$msysUsrBin'."
}

$gccPath = Join-Path $msysUcrtBin "$Compiler.exe"
if (-not (Test-Path $gccPath)) {
    throw "No se encontro el compilador '$Compiler' en '$msysUcrtBin'."
}

$oldPath = $env:PATH
$oldMsystem = $env:MSYSTEM
$oldChere = $env:CHERE_INVOKING

try {
    $env:MSYSTEM = "UCRT64"
    $env:CHERE_INVOKING = "1"
    $env:PATH = "$msysUcrtBin;$msysUsrBin;$oldPath"

    New-Item -ItemType Directory -Force -Path "bin" | Out-Null
    New-Item -ItemType Directory -Force -Path "build" | Out-Null

    $sources = @(
        "src/main.c",
        "src/csv_io.c",
        "src/time_utils.c",
        "src/kmeans_common.c",
        "src/kmeans_serial.c",
        "src/kmeans_omp.c"
    )

    $cflags = @(
        "-Iinclude",
        "-O3",
        "-march=native",
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Wshadow",
        "-Wconversion",
        "-pedantic"
    )

    if (-not $NoOpenMp) {
        $cflags += "-fopenmp"
    }

    $args = @()
    $args += $cflags
    $args += $sources
    $args += "-o"
    $args += $Output
    $args += "-lm"

    Write-Host "Building $Output with $Compiler ..."
    & $Compiler @args
    if ($LASTEXITCODE -ne 0) {
        throw "La compilacion fallo con codigo $LASTEXITCODE."
    }

    Write-Host "Build complete: $Output"
}
finally {
    $env:PATH = $oldPath
    if ($null -ne $oldMsystem) {
        $env:MSYSTEM = $oldMsystem
    }
    else {
        Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
    }

    if ($null -ne $oldChere) {
        $env:CHERE_INVOKING = $oldChere
    }
    else {
        Remove-Item Env:CHERE_INVOKING -ErrorAction SilentlyContinue
    }
}
