# 以管理员身份运行一次：
#   1) 防火墙放行 8000 端口（只允许同一局域网的设备访问）
#   2) 建计划任务 "XiaozhiServer"：开机就启动小智服务端（不用登录），挂了自动重启
$ErrorActionPreference = "Stop"
$script = (Resolve-Path (Join-Path $PSScriptRoot "run-server.ps1")).Path
$port = 8000

$rule = "Xiaozhi Server $port"
if (-not (Get-NetFirewallRule -DisplayName $rule -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -DisplayName $rule -Direction Inbound -Protocol TCP -LocalPort $port `
        -RemoteAddress LocalSubnet -Action Allow -Profile Any | Out-Null
}

$action = New-ScheduledTaskAction -Execute "powershell.exe" `
    -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$script`""
$trigger = New-ScheduledTaskTrigger -AtStartup
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -StartWhenAvailable -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1)
$principal = New-ScheduledTaskPrincipal -UserId "SYSTEM" -LogonType ServiceAccount -RunLevel Highest
Register-ScheduledTask -TaskName "XiaozhiServer" -Action $action -Trigger $trigger `
    -Settings $settings -Principal $principal -Force | Out-Null
Start-ScheduledTask -TaskName "XiaozhiServer"
Write-Output "已安装并启动计划任务 XiaozhiServer；防火墙规则：$rule"
