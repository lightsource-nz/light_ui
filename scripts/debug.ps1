# Starts an OpenOCD server, and optionally gdb, against a built screen-test target.
#
# USAGE:  scripts/debug.ps1 [-Target <name>] [-ServerOnly] [-Attach] [-NoBuild]
#                          [-Ex <cmd>[,<cmd>...]] [-Batch]
#
#   -Ex appends gdb commands; COMMA-SEPARATED for more than one, since it is a PowerShell array
# parameter. -Batch runs them and exits instead of handing over a prompt, which is what makes
# this usable from CI:
#     scripts/debug.ps1 -Batch                          # load the image over SWD and stop
#     scripts/debug.ps1 -Batch -Ex 'monitor reset run'  # load it and leave the board running
#
#   -ServerOnly leaves OpenOCD in the foreground for a debugger to attach to on localhost:3333,
# which is what VS Code's launch configurations want. Without it, gdb is started too.
#   -Attach leaves whatever is already on the board alone; the default loads the ELF just built.
param(
        [string]$Target,
        [string]$Preset,
        [switch]$ServerOnly,
        [switch]$Attach,
        [switch]$NoBuild,
        [string[]]$Ex,
        [switch]$Batch
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'light-tools.ps1')

if (-not $Target) { $Target = (& (Join-Path $PSScriptRoot 'project.config.ps1')).DefaultTarget }

& (Join-Path $LightScripts 'light-debug.ps1') -Target $Target -Preset $Preset `
        -ServerOnly:$ServerOnly -Attach:$Attach -NoBuild:$NoBuild `
        -Ex $Ex -Batch:$Batch -ProjectRoot $root
