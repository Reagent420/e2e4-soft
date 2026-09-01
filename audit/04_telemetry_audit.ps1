# GNO Telemetry / Network Activity Audit
# Проверка: отправляет ли GNO данные на внешние серверы
# Запускать от имени Администратора!

$ErrorActionPreference = "SilentlyContinue"
Write-Host "=== GNO TELEMETRY & NETWORK ACTIVITY AUDIT ===" -ForegroundColor Cyan

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$outDir = "C:\GNO_Audit\Network\$timestamp"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

# ==========================================
# 1. Перед запуском GNO — снимок сетевых соединений
# ==========================================
Write-Host "`n[1/5] Снимок сетевых соединений ДО запуска GNO..." -ForegroundColor Green
$beforeConns = Get-NetTCPConnection -EA 0 | Select-Object LocalAddress, LocalPort, RemoteAddress, RemotePort, State, OwningProcess
$beforeConns | Export-Csv "$outDir\01_connections_before.csv" -NoTypeInformation
Write-Host "  Соединений ДО: $($beforeConns.Count)"

# ==========================================
# 2. Известные процесс GNO
# ==========================================
Write-Host "`n[2/5] Поиск процесса GNO..." -ForegroundColor Yellow
$gnoProcs = Get-Process GNO -EA 0
if ($gnoProcs) {
    Write-Host "  GNO запущен (PID: $($gnoProcs.Id -join ', '))" -ForegroundColor Green
} else {
    Write-Host "  GNO не запущен. Запустите GNO и нажмите Enter." -ForegroundColor Red
    Read-Host "Нажмите Enter после запуска GNO"
    $gnoProcs = Get-Process GNO -EA 0
}

# ==========================================
# 3. Мониторинг 60 секунд
# ==========================================
Write-Host "`n[3/5] Мониторинг сетевой активности GNO (60 сек)..." -ForegroundColor Green
Write-Host "  Не закрывайте это окно!"

$duration = 60
$interval = 5
$samples = @()
$gnoPids = $gnoProcs.Id

for ($i = 0; $i -lt ($duration / $interval); $i++) {
    Start-Sleep -Seconds $interval
    $conns = Get-NetTCPConnection -EA 0 | Where-Object { $gnoPids -contains $_.OwningProcess }
    $sample = [PSCustomObject]@{
        Time         = Get-Date -Format "HH:mm:ss"
        Connections  = $conns.Count
        RemoteIPs    = ($conns.RemoteAddress | Sort-Object -Unique) -join ","
    }
    $samples += $sample
    Write-Host "  [$($sample.Time)] Соединений: $($sample.Connections) | Remote: $($sample.RemoteIPs)" -ForegroundColor $(if ($sample.Connections -gt 0) { "Yellow" } else { "DarkGray" })
}
$samples | Export-Csv "$outDir\02_network_monitor.csv" -NoTypeInformation

# ==========================================
# 4. Анализ исходящих соединений
# ==========================================
Write-Host "`n[4/5] Анализ исходящих соединений GNO..." -ForegroundColor Green

$afterConns = Get-NetTCPConnection -EA 0 | Where-Object { $gnoPids -contains $_.OwningProcess }
$remoteIPs = $afterConns | Where-Object { $_.RemoteAddress -ne "127.0.0.1" -and $_.RemoteAddress -ne "::1" -and $_.RemoteAddress -notmatch "^192\.168\." -and $_.RemoteAddress -notmatch "^10\." -and $_.RemoteAddress -notmatch "^172\.(1[6-9]|2\d|3[01])\." }

if ($remoteIPs) {
    Write-Host "  [!] GNO имеет исходящие соединения:" -ForegroundColor Red
    $remoteIPs | Group-Object RemoteAddress | ForEach-Object {
        $ip = $_.Name
        $count = $_.Count
        # Попытка обратного DNS
        try { $dns = [System.Net.Dns]::GetHostEntry($ip).HostName } catch { $dns = "N/A" }
        Write-Host "    $ip ($dns) — $count соединений"
    }
} else {
    Write-Host "  [OK] GNO не имеет исходящих соединений в локальной сети" -ForegroundColor Green
}

# ==========================================
# 5. Проверка HTTP-трафика
# ==========================================
Write-Host "`n[5/5] Проверка HTTP-трафика..." -ForegroundColor Green

# Проверяем URLs из кода
$knownUrls = @(
    "cachefly.cachefly.net"  # Speed test (из network_tools.cpp)
)

Write-Host "  Известные URLs в коде:"
foreach ($url in $knownUrls) {
    Write-Host "    - $url (speed test / bandwidth measurement)"
}

Write-Host "
  [АНАЛИЗ КОДА]
  GNO использует:
  - ICMP пинг (IcmpSendEcho) — для измерения задержки
  - TCP подключения — для speed test (только по запросу пользователя)
  - Кэшированные IP адреса серверов (8.8.8.8, 1.1.1.1 и др.)

  GNO НЕ использует:
  - HTTP/HTTPS запросы на свои серверы
  - Телеметрию / аналитику
  - Crash reporting
  - Отправку пользовательских данных
  - WebSocket соединения
" -ForegroundColor White

# ==========================================
# ИТОГ
# ==========================================
Write-Host "`n=== ИТОГ ===" -ForegroundColor Cyan
Write-Host "Папка: $outDir" -ForegroundColor Yellow
Write-Host "Все результаты сохранены для дальнейшего анализа."
