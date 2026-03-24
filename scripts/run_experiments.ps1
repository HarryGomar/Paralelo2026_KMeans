param(
    [int]$K = 5,
    [int]$MaxIters = 300,
    [double]$Tol = 1e-4,
    [int]$Runs = 10,
    [string]$ResultsCsv = "results/experiments.csv",
    [int[]]$Dims,
    [int[]]$Ns,
    [int[]]$Threads,
    [switch]$SkipBuild,
    [switch]$SkipPlots
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

function Require-Command([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "No se encontro '$Name' en PATH."
    }
}

function Write-SystemInfo([string]$Path, [string]$CompilerCmd, [string]$PythonCmd) {
    $cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
    $os = Get-CimInstance Win32_OperatingSystem
    $compilerVersion = (& $CompilerCmd --version)[0]
    $pythonVersion = (& $PythonCmd --version)

    @(
        "date: $(Get-Date -Format o)"
        "computer_name: $env:COMPUTERNAME"
        "cwd: $root"
        "cpu_name: $($cpu.Name)"
        "physical_cores: $($cpu.NumberOfCores)"
        "logical_processors: $($cpu.NumberOfLogicalProcessors)"
        "max_clock_mhz: $($cpu.MaxClockSpeed)"
        "os_caption: $($os.Caption)"
        "os_version: $($os.Version)"
        "os_build: $($os.BuildNumber)"
        "os_architecture: $($os.OSArchitecture)"
        "compiler: $compilerVersion"
        "python: $pythonVersion"
    ) | Set-Content -Path $Path
}

function Validate-ResultsCsv([string]$Path, [int]$ExpectedRuns, [int]$ExpectedRows) {
    $rows = Import-Csv $Path
    if (-not $rows -or $rows.Count -eq 0) {
        throw "ERROR: $Path no contiene filas."
    }

    if ($rows.Count -ne $ExpectedRows) {
        throw "ERROR: $Path tiene $($rows.Count) filas, se esperaban $ExpectedRows."
    }

    $badGroups = $rows |
        Group-Object dim, N, mode, threads |
        Where-Object { $_.Count -ne $ExpectedRuns }

    if ($badGroups) {
        $messages = $badGroups | ForEach-Object {
            "ERROR: configuracion [$($_.Name)] tiene $($_.Count) filas, se esperaban $ExpectedRuns."
        }
        throw ($messages -join [Environment]::NewLine)
    }
}

Require-Command python
$pythonCmd = (Get-Command python).Source
$compilerCmd = Join-Path $msysUcrtBin "gcc.exe"

if (-not $Dims) {
    $Dims = @(2, 3)
}

if (-not $Ns) {
    $Ns = @(100000, 200000, 300000, 400000, 600000, 800000, 1000000)
}

$logical = (Get-CimInstance Win32_Processor | Measure-Object -Property NumberOfLogicalProcessors -Sum).Sum
$half = [Math]::Max(1, [int]($logical / 2))

if (-not $Threads) {
    $threadCandidates = @(1, $half, $logical, ($logical * 2))
    $Threads = @($threadCandidates | Select-Object -Unique)
}

New-Item -ItemType Directory -Force -Path "data" | Out-Null
New-Item -ItemType Directory -Force -Path "results" | Out-Null

$oldPath = $env:PATH
$oldMsystem = $env:MSYSTEM
$oldChere = $env:CHERE_INVOKING

try {
    $env:MSYSTEM = "UCRT64"
    $env:CHERE_INVOKING = "1"
    $env:PATH = "$msysUcrtBin;$msysUsrBin;$oldPath"

    if (-not $SkipBuild) {
        & powershell -ExecutionPolicy Bypass -File "scripts/build_windows.ps1"
        if ($LASTEXITCODE -ne 0) {
            throw "La compilacion fallo con codigo $LASTEXITCODE."
        }
    }

    $exePath = Join-Path $root "bin/kmeans.exe"
    if (-not (Test-Path $exePath)) {
        throw "No se encontro el ejecutable en '$exePath'."
    }

    if (Test-Path $ResultsCsv) {
        Remove-Item $ResultsCsv -Force
    }

    $expectedRows = $Dims.Count * $Ns.Count * $Runs * (1 + $Threads.Count)

    Write-Host "Capturing HW/SW info..."
    Write-SystemInfo -Path "results/system_info.txt" -CompilerCmd $compilerCmd -PythonCmd $pythonCmd

    Write-Host "Running experiments:"
    Write-Host "  K=$K MAX_ITERS=$MaxIters TOL=$Tol RUNS=$Runs"
    Write-Host "  LogicalProcessors=$logical THREADS=$($Threads -join ' ')"
    Write-Host "  Expected rows=$expectedRows"

    foreach ($dim in $Dims) {
        foreach ($n in $Ns) {
            $dataPath = "data/synth_${dim}d_N${n}.csv"
            if (-not (Test-Path $dataPath)) {
                Write-Host "Generating $dataPath ..."
                & $pythonCmd "scripts/generate_synthetic.py" --dim $dim --n $n --k $K --seed 123 --out $dataPath
                if ($LASTEXITCODE -ne 0) {
                    throw "Fallo la generacion de $dataPath."
                }
            }

            foreach ($run in 0..($Runs - 1)) {
                $seed = 42 + $run

                & $exePath --input $dataPath --dim $dim --k $K --mode serial `
                    --max-iters $MaxIters --tol $Tol --seed $seed `
                    --log-csv $ResultsCsv --run-idx $run | Out-Null
                if ($LASTEXITCODE -ne 0) {
                    throw "Fallo la corrida serial para dim=$dim N=$n run=$run."
                }

                foreach ($threadCount in $Threads) {
                    & $exePath --input $dataPath --dim $dim --k $K --mode omp --threads $threadCount `
                        --max-iters $MaxIters --tol $Tol --seed $seed `
                        --log-csv $ResultsCsv --run-idx $run | Out-Null
                    if ($LASTEXITCODE -ne 0) {
                        throw "Fallo la corrida omp para dim=$dim N=$n run=$run threads=$threadCount."
                    }
                }
            }
        }
    }

    Validate-ResultsCsv -Path $ResultsCsv -ExpectedRuns $Runs -ExpectedRows $expectedRows

    $rep2d = "data/synth_2d_N100000.csv"
    $rep3d = "data/synth_3d_N100000.csv"
    $repThreads = ($Threads | Measure-Object -Maximum).Maximum

    Write-Host "Generating representative cluster outputs..."
    & $exePath --input $rep2d --dim 2 --k $K --mode omp --threads $repThreads `
        --max-iters $MaxIters --tol $Tol --seed 42 `
        --out "results/clusters_2d.csv" --centroids "results/centroids_2d.csv" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Fallo la generacion de outputs representativos 2D."
    }

    & $exePath --input $rep3d --dim 3 --k $K --mode omp --threads $repThreads `
        --max-iters $MaxIters --tol $Tol --seed 42 `
        --out "results/clusters_3d.csv" --centroids "results/centroids_3d.csv" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Fallo la generacion de outputs representativos 3D."
    }

    if (-not $SkipPlots) {
        Write-Host "Generating speedup plots..."
        & $pythonCmd "scripts/plot_speedup.py" --input $ResultsCsv --outdir "results"
        if ($LASTEXITCODE -ne 0) {
            throw "Fallo la generacion de speedup.csv o graficas."
        }
    }

    Write-Host "Validated $expectedRows rows in $ResultsCsv"
    Write-Host "Done: $ResultsCsv"
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
