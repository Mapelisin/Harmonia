# Checks an installer end to end: installs it quietly for this user into a temporary folder,
# checks the files, the Start menu, the uninstall entry and the .harmonia file type, runs the
# installed Harmonia's self-test (without a sound card), then uninstalls it and checks that all
# of it is gone. Exit code: the number of failed checks.
#
#   tools\test-installer.ps1 -Installer build\installer\Harmonia-Setup-<version>.exe
#
# It refuses to run where Harmonia is installed already: the test install would replace it.
param([Parameter(Mandatory = $true)] [string] $Installer)

$ErrorActionPreference = 'Stop'
$failed = 0

function Check([bool] $ok, [string] $what)
{
    if ($ok) { Write-Host "[PASS] $what" } else { Write-Host "[FAIL] $what"; $script:failed++ }
}

function Get-Default([string] $key)
{
    $item = Get-Item -LiteralPath $key -ErrorAction SilentlyContinue
    if ($null -eq $item) { return $null }
    return $item.GetValue('')
}

$Installer = (Resolve-Path $Installer).Path
$script = Join-Path $PSScriptRoot '..\installer\Harmonia.iss'
$appId = [regex]::Match((Get-Content -Raw $script), 'AppId=\{\{([0-9A-F-]+)\}').Groups[1].Value
if (-not $appId) { throw "No AppId in $script" }

$uninstallKey = "Software\Microsoft\Windows\CurrentVersion\Uninstall\{$appId}_is1"
foreach ($root in 'HKCU:', 'HKLM:')
{
    if (Test-Path "$root\$uninstallKey") { throw "Harmonia is installed on this computer ($root): the test would replace it" }
}

$work = Join-Path ([IO.Path]::GetTempPath()) 'HarmoniaInstallTest'
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
$dir = Join-Path $work 'Harmonia'
New-Item -ItemType Directory -Force $work | Out-Null

$exe = Join-Path $dir 'Harmonia.exe'
$shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'Harmonia.lnk'
$desktop = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Harmonia.lnk'
$classes = 'HKCU:\Software\Classes'

# Only for this user (no administrator), the file type yes, the desktop icon no.
$install = Start-Process -FilePath $Installer -Wait -PassThru -ArgumentList @(
    '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER',
    "/DIR=`"$dir`"", '/TASKS="associate"', "/LOG=`"$work\install.log`"")
Check ($install.ExitCode -eq 0) "The installer finished (exit code $($install.ExitCode))"

Check (Test-Path $exe) "Harmonia.exe is in $dir"
Check (Test-Path (Join-Path $dir 'unins000.exe')) 'The uninstaller is next to it'
Check (Test-Path $shortcut) 'Start menu: Harmonia'
Check (-not (Test-Path $desktop)) 'No desktop icon when it was not asked for'

$version = (Get-Item $Installer).BaseName -replace '^Harmonia-Setup-', ''
$entry = Get-ItemProperty -LiteralPath "HKCU:\$uninstallKey" -ErrorAction SilentlyContinue
Check ($null -ne $entry -and $entry.DisplayVersion -eq $version) "Apps & features: Harmonia $version"
Check ($null -ne $entry -and $entry.DisplayName -eq 'Harmonia') 'Its name there is Harmonia'

Check ((Get-Default "$classes\.harmonia") -eq 'Harmonia.Project') '.harmonia files are Harmonia projects'
Check ((Get-Default "$classes\Harmonia.Project\shell\open\command") -eq "`"$exe`" `"%1`"") 'A double click opens them with the installed Harmonia'
Check ((Get-Default "$classes\Harmonia.Project\DefaultIcon") -eq "$exe,0") "They show Harmonia's icon"

# The installed program runs on its own: everything it needs came with it.
if (Test-Path $exe)
{
    $log = Join-Path $work 'selftest.log'
    $run = Start-Process -FilePath $exe -Wait -PassThru -ArgumentList @('--selftest', '--no-device', '--quiet', "`"--log=$log`"")
    $passed = (Test-Path $log) -and (Select-String -Path $log -Pattern 'ALL CHECKS PASSED' -Quiet)
    Check ($run.ExitCode -eq 0 -and $passed) "The installed Harmonia passes its self-test (exit code $($run.ExitCode))"
    if (-not $passed -and (Test-Path $log)) { Get-Content $log | Select-String 'FAIL' | ForEach-Object { Write-Host "       $_" } }
}

# The uninstaller hands over to a copy of itself and returns at once: wait for the folder to empty.
$uninstaller = Join-Path $dir 'unins000.exe'
if (Test-Path $uninstaller)
{
    Start-Process -FilePath $uninstaller -Wait -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') | Out-Null
    $deadline = (Get-Date).AddSeconds(60)
    while (((Test-Path $exe) -or (Test-Path $uninstaller) -or (Test-Path "HKCU:\$uninstallKey")) -and (Get-Date) -lt $deadline)
    {
        Start-Sleep -Milliseconds 500
    }
}

Check (-not (Test-Path $exe)) 'Uninstalled: Harmonia.exe is gone'
Check (-not (Test-Path $shortcut)) 'Uninstalled: the Start menu item is gone'
Check (-not (Test-Path "HKCU:\$uninstallKey")) 'Uninstalled: gone from Apps & features'
Check (-not (Test-Path "$classes\Harmonia.Project")) 'Uninstalled: the project file type is gone'
Check ($null -eq (Get-Default "$classes\.harmonia")) 'Uninstalled: .harmonia files no longer point to it'

if (Test-Path $work) { Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue }

if ($failed -eq 0) { Write-Host 'RESULT: ALL CHECKS PASSED' } else { Write-Host "RESULT: $failed CHECK(S) FAILED" }
exit $failed
