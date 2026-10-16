# SPDX-License-Identifier: GPL-2.0-or-later
$ErrorActionPreference = 'Stop'
$code = 1
try {
    $process = Start-Process -FilePath (Join-Path $PSScriptRoot 'drmario64-vs.exe') -WorkingDirectory $PSScriptRoot -PassThru -Wait
    $code = $process.ExitCode
} catch { $code = 1 }
if ($code -ne 0 -and $code -ne 2 -and $env:DM_NO_REPORT -ne '1') {
    $version = (Get-Content (Join-Path $PSScriptRoot 'VERSION') -ErrorAction SilentlyContinue)
    # Fire and forget; no application logs or personal identifiers are uploaded.
    Start-Process curl.exe -WindowStyle Hidden -ArgumentList @('-fsS', '--max-time', '8', '-d', "`"drmario64-vs $version Windows exit code $code`"", 'https://ntfy.sh/drmario64-vs-faults-9c41d7a2e05b4f18') -ErrorAction SilentlyContinue
}
exit $code
