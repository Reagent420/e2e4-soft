# GNO DNS & Latency Audit
# Сравнение показаний GNO с эталонными замерами
# Запускать от имени Администратора!

$ErrorActionPreference = "SilentlyContinue"
Write-Host "=== GNO DNS & LATENCY AUDIT ===" -ForegroundColor Cyan

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$outDir = "C:\GNO_Audit\DNS\$timestamp"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

# ==========================================
# 1. Эталонный пинг (внешний)
# ==========================================
Write-Host "`n[1/4] Эталонный пинг к публичным DNS..." -ForegroundColor Green

$targets = @(
    @{Name="Cloudflare"; IP="1.1.1.1"},
    @{Name="Google";     IP="8.8.8.8"},
    @{Name="Quad9";      IP="9.9.9.9"},
    @{Name="OpenDNS";    IP="208.67.222.222"},
    @{Name="Yandex";     IP="77.88.8.8"}
)

$results = @()
foreach ($t in $targets) {
    Write-Host "  Пинг $($t.Name) ($($t.IP))..." -NoNewline
    $ping = ping -n 20 -w 2000 $t.IP
    $stats = $ping | Select-String "Lost = (\d+)" | ForEach-Object { [int]$_.Matches[0].Groups[1].Value }
    $avg = $ping | Select-String "Average = (\d+)ms" | ForEach-Object { [int]$_.Matches[0].Groups[1].Value }
    $min = $ping | Select-String "Minimum = (\d+)ms" | ForEach-Object { [int]$_.Matches[0].Groups[1].Value }
    $max = $ping | Select-String "Maximum = (\d+)ms" | ForEach-Object { [int]$_.Matches[0].Groups[1].Value }
    $lossPct = if ($stats -gt 0) { [math]::Round($stats / 20 * 100, 1) } else { 0 }

    $result = [PSCustomObject]@{
        Server     = $t.Name
        IP         = $t.IP
        AvgMs      = $avg
        MinMs      = $min
        MaxMs      = $max
        LossPct    = $lossPct
    }
    $results += $result
    Write-Host " avg=${avg}ms loss=${lossPct}%"
}

$results | Format-Table -AutoSize | Out-File "$outDir\01_baseline_ping.txt"

# ==========================================
# 2. Расчёт джиттера
# ==========================================
Write-Host "`n[2/4] Расчёт джиттера (среднеквадратичное отклонение)..." -ForegroundColor Green

foreach ($t in $targets) {
    $pings = @()
    for ($i = 0; $i -lt 30; $i++) {
        $line = ping -n 1 -w 1000 $t.IP | Select-String "time=(\d+)ms"
        if ($line) { $pings += [int]$line.Matches[0].Groups[1].Value }
        Start-Sleep -Milliseconds 200
    }
    if ($pings.Count -gt 1) {
        $avg = ($pings | Measure-Object -Average).Average
        $jitter = [math]::Sqrt(($pings | ForEach-Object { [math]::Pow($_ - $avg, 2) } | Measure-Object -Average).Average)
        Write-Host "  $($t.Name): jitter = $([math]::Round($jitter, 2)) ms (avg=$([math]::Round($avg, 1)) ms, n=$($pings.Count))"
    }
}

# ==========================================
# 3. DNS Resolution Speed
# ==========================================
Write-Host "`n[3/4] DNS Resolution Speed Test..." -ForegroundColor Green

$dnsTests = @("google.com", "steam-store.com", "valorant.com", "discord.com", "dota2.com")
foreach ($domain in $dnsTests) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        [System.Net.Dns]::GetHostAddresses($domain) | Out-Null
        $sw.Stop()
        Write-Host "  $domain = $($sw.ElapsedMilliseconds) ms"
    } catch {
        Write-Host "  $domain = FAILED" -ForegroundColor Red
    }
}

# ==========================================
# 4. Сравнение с GNO
# ==========================================
Write-Host "`n[4/4] Инструкция по сравнению с GNO..." -ForegroundColor Yellow
Write-Host "
Для завершения аудита:
1. Откройте GNO → вкладка «Диагностика» или «Мониторинг»
2. Запустите измерение пинга (Quality Score)
3. Запишите показания:
   - Ping:     ___ мс
   - Jitter:   ___ мс
   - Loss:     ___ %
   - Score:    ___ /100
4. Сравните с данными выше

Критерий точности GNO:
  |Пинг_GNO - Пинг_реальный| < 5 мс  → ТОЧНО
  |Jitter_GNO - Jitter_реальный| < 3 мс → ТОЧНО
  |Loss_GNO - Loss_реальный| < 1%   → ТОЧНО

Если расхождения больше — GNO использует другой метод измерения
(ICMP vs TCP ping, разные интервалы и т.д.)
"

Write-Host "Результаты сохранены в: $outDir" -ForegroundColor Yellow
