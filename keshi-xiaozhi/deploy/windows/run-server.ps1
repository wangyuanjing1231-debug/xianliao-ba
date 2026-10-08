# 小智服务端守护脚本：服务端意外退出后 5 秒自动重启。
# 由计划任务 "XiaozhiServer" 开机运行（见 install-autostart.ps1），一般不用手动跑。
$ErrorActionPreference = "Continue"
$server = (Resolve-Path (Join-Path $PSScriptRoot "..\..\server")).Path
$logs = Join-Path $server "logs"
New-Item -ItemType Directory -Force $logs | Out-Null
$py = Join-Path $server ".venv\Scripts\python.exe"
Set-Location $server
while ($true) {
    # 正常日志在 logs\server.log；这里只留最近一次的报错输出，方便查启动失败
    & $py main.py 1> $null 2> (Join-Path $logs "last-stderr.txt")
    Start-Sleep -Seconds 5
}
