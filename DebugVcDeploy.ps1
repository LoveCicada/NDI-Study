#Requires -Version 5.1
<#
.SYNOPSIS
    Deploy MSVC / UCRT Debug (/MDd) runtime libraries (windeployqt-style).

.EXAMPLE
    .\DebugVcDeploy.bat --dir NDI .\build\bin\Debug\NDIReceiver.exe
#>

$Script:VcDeployToolName = 'DebugVcDeploy'
$Script:VcDeployConfiguration = 'Debug'
$Script:VcDeployShowHelpEnv = 'DEBUG_VCDEPLOY_SHOW_HELP'

. "$PSScriptRoot\VcDeployCommon.ps1"
Start-VcDeploy @args
