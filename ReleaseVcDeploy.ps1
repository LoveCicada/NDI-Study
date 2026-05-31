#Requires -Version 5.1
<#
.SYNOPSIS
    Deploy MSVC / UCRT Release (/MD) runtime libraries (windeployqt-style).

.EXAMPLE
    .\ReleaseVcDeploy.bat --dir NDI .\build\bin\Release\NDIReceiver.exe
#>

$Script:VcDeployToolName = 'ReleaseVcDeploy'
$Script:VcDeployConfiguration = 'Release'
$Script:VcDeployShowHelpEnv = 'RELEASE_VCDEPLOY_SHOW_HELP'

. "$PSScriptRoot\VcDeployCommon.ps1"
Start-VcDeploy @args
