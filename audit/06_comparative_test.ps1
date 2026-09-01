# GNO Comparative Test: BEFORE vs AFTER
# План на 2 дня + шаблон таблицы + интерпретация результатов
# Запускать от имени Администратора!

$ErrorActionPreference = "SilentlyContinue"
Write-Host "=== GNO COMPARATIVE TEST: BEFORE vs AFTER ===" -ForegroundColor Cyan

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$outDir = "C:\GNO_Audit\Comparison\$timestamp"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

# ==========================================
# ДЕНЬ 1: Базовые показатели (БЕЗ оптимизации)
# ==========================================
Write-Host "`n=== ДЕНЬ 1: БАЗОВЫЕ ПОКАЗАТЕЛИ ===" -ForegroundColor Yellow
Write-Host "Убедитесь, что GNO НЕ применял оптимизации!"

$targets = @(
    @{Name="Cloudflare (Frankfurt)"; IP="1.1.1.1"},
    @{Name="Google (Global)";        IP="8.8.8.8"},
    @{Name="Game Server (Moscow)";   IP="8.8.8.8"}  # замените на IP вашего игрового сервера
)

$games = @("CS2", "Dota 2", "Valorant")

Write-Host "`nДля каждой игры:"
Write-Host "1. Запустите игру"
Write-Host "2. Зайдите в матча/игру на 5-10 минут"
Write-Host "3. Запишите показания из GNO (вкладка Мониторинг)"
Write-Host "4. параллельно запустите пинг из cmd: ping -n 60 <IP сервера>"
Write-Host ""

# Автоматический пинг
foreach ($t in $targets) {
    Write-Host "`nПинг $($t.Name) ($($t.IP))..." -ForegroundColor Green
    $pings = @()
    for ($i = 0; $i -lt 30; $i++) {
        $line = ping -n 1 -w 2000 $t.IP | Select-String "time=(\d+)ms"
        if ($line) { $pings += [int]$line.Matches[0].Groups[1].Value }
        Start-Sleep -Milliseconds 500
    }
    if ($pings.Count -gt 0) {
        $avg = ($pings | Measure-Object -Average).Average
        $jitter = [math]::Sqrt(($pings | ForEach-Object { [math]::Pow($_ - $avg, 2) } | Measure-Object -Average).Average)
        Write-Host "  Ping: $([math]::Round($avg, 1)) ms | Jitter: $([math]::Round($jitter, 2)) ms | Min: $($pings | Measure-Object -Minimum | Select-Object -ExpandProperty Minimum) | Max: $($pings | Measure-Object -Maximum | Select-Object -ExpandProperty Maximum)"
    }
}

# ==========================================
# ШАБЛОН ТАБЛИЦЫ
# ==========================================
Write-Host "`n=== ШАБЛОН ТАБЛИЦЫ ДЛЯ ЗАПОЛНЕНИЯ ===" -ForegroundColor Cyan

$csvPath = "$outDir\comparison_template.csv"
@"
Игра,Сервер,Базовый_пинг,Базовый_джиттер,Базовый_loss,После_буста_пинг,После_буста_джиттер,После_буста_loss,Дельта_пинг,Дельта_джиттер
CS2,Cloudflare,,,,,,,
CS2,Google,,,,,,,
Dota2,Cloudflare,,,,,,,
Dota2,Google,,,,,,,
Valorant,Cloudflare,,,,,,,
Valorant,Google,,,,,,,
"@ | Out-File $csvPath -Encoding UTF8

Write-Host "Шаблон CSV сохранён: $csvPath" -ForegroundColor Yellow
Write-Host ""
Write-Host "Заполните таблицу вручную или я помогу автоматически." -ForegroundColor White

# ==========================================
# ДЕНЬ 2: После оптимизации
# ==========================================
Write-Host "`n=== ДЕНЬ 2: ПОСЛЕ ОПТИМИЗАЦИИ ===" -ForegroundColor Yellow
Write-Host "
ИНСТРУКЦИЯ:
1. Примените полную оптимизацию 'Boost' в GNO
2. Перезагрузите компьютер (если потребовалось)
3. Повторите те же измерения что и в День 1
4. Введите данные в шаблон CSV
5. Вернитесь сюда для автоматического сравнения
"
Read-Host "Нажмите Enter когда данные Дня 2 будут готовы"

# Автоматический пинг Дня 2
Write-Host "`nПинг Дня 2:" -ForegroundColor Green
foreach ($t in $targets) {
    Write-Host "  $($t.Name)..." -NoNewline
    $pings = @()
    for ($i = 0; $i -lt 30; $i++) {
        $line = ping -n 1 -w 2000 $t.IP | Select-String "time=(\d+)ms"
        if ($line) { $pings += [int]$line.Matches[0].Groups[1].Value }
        Start-Sleep -Milliseconds 500
    }
    if ($pings.Count -gt 0) {
        $avg = ($pings | Measure-Object -Average).Average
        $jitter = [math]::Sqrt(($pings | ForEach-Object { [math]::Pow($_ - $avg, 2) } | Measure-Object -Average).Average)
        Write-Host " Ping=$([math]::Round($avg,1))ms Jitter=$([math]::Round($jitter,2))ms"
    }
}

# ==========================================
# ИНТЕРПРЕТАЦИЯ РЕЗУЛЬТАТОВ
# ==========================================
Write-Host "`n=== ИНТЕРПРЕТАЦИЯ РЕЗУЛЬТАТОВ ===" -ForegroundColor Cyan
Write-Host "
  КРИТЕРИИ ОЦЕНКИ:

  Джиттер снизился на > 5 мс:
    → Оптимизация РАБОТАЕТ (TCP/MTU/DNS tweaks реально влияют)

  Пинг упал на > 5 мс:
    → ВЕРОЯТНО из-за смены маршрута у провайдера
    → GNO НЕ влияет на маршрутизацию (по заявлению)
    → Проверьте: если пинг упал ПОСЛЕ перезагрузки — это провайдер

  Пинг вырос на > 10 мс:
    → ПРОБЛЕМА: оптимизация ухудшила соединение
    → Откатите изменения и проверьте снова

  Loss увеличился:
    → ПРОБЛЕМА: TcpAckFrequency=1 может вызвать потери на Wi-Fi
    → Решение: отключите TcpAckFrequency для Wi-Fi подключений

  Скорость DNS упала:
    → Проверьте DNS сервер: nslookup google.com 1.1.1.1

  Оптимальные показатели:
    Ping:  < 40 мс (A+), 40-60 (A), 60-80 (B), 80-100 (C)
    Jitter: < 5 мс (A+), 5-10 (A), 10-20 (B), 20-30 (C)
    Loss:  < 0.1% (A+), 0.1-0.5% (A), 0.5-1% (B), 1-2% (C)
"

Write-Host "Результаты сохранены в: $outDir" -ForegroundColor Yellow
Write-Host "Поделитесь итоговой таблицей для интерпретации!"
