param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $ProjectRoot

try {
    $EnvFile = Join-Path $ProjectRoot ".env"
    if (Test-Path $EnvFile) {
        foreach ($Entry in Get-Content $EnvFile) {
            $Line = $Entry.Trim()
            if (-not $Line -or $Line.StartsWith("#")) { continue }
            if ($Line -match '^\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$') {
                $Name = $Matches[1]
                $Value = $Matches[2].Trim()
                if ($Value.Length -ge 2 -and
                    (($Value.StartsWith('"') -and $Value.EndsWith('"')) -or
                     ($Value.StartsWith("'") -and $Value.EndsWith("'")))) {
                    $Value = $Value.Substring(1, $Value.Length - 2)
                }
                [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
            }
        }
    }

    cmake -S . -B build -A x64
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }
    cmake --build build --config $Configuration
    if ($LASTEXITCODE -ne 0) { throw "UniLost failed to build." }

    $Server = Join-Path $ProjectRoot "build\$Configuration\unilost_server.exe"
    if (-not (Test-Path $Server)) {
        $Server = Join-Path $ProjectRoot "build\unilost_server.exe"
    }
    if (-not (Test-Path $Server)) { throw "The UniLost server executable was not found." }
    & $Server
}
finally {
    Pop-Location
}
