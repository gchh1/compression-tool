# Copy google/brotli into third_party/brotli for offline CMake (no GitHub FetchContent).
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dst = Join-Path $root "third_party\brotli"
$sources = @(
    (Join-Path $root "build\_deps\google_brotli-src"),
    (Join-Path $root "build_py\_deps\google_brotli-src")
)
$src = $sources | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $src) {
    Write-Error "No cached brotli under build/_deps. Run cmake once with network, or clone v1.1.0 into third_party/brotli."
}
if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
Copy-Item -Recurse -Force $src $dst
Write-Host "Vendored libbrotli: $dst"
