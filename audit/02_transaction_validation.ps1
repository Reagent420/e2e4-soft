# GNO Transaction Engine Validation
# Проверка: BACKUP → APPLY → VERIFY → ROLLBACK
# Запускать от имени Администратора!

$ErrorActionPreference = "SilentlyContinue"
Write-Host "=== GNO TRANSACTION ENGINE VALIDATION ===" -ForegroundColor Cyan

# ==========================================
# ШАГ 1: Выбор параметра для теста
# ==========================================
Write-Host "`n--- ШАГ 1: Тестовый параметр ---" -ForegroundColor Yellow
Write-Host "Будем проверять: Game DVR (AppCaptureEnabled)"
Write-Host "Реестр: HKCU\Software\Microsoft\Windows\CurrentVersion\GameDVR"

$regPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\GameDVR"
$valueName = "AppCaptureEnabled"

# Запоминаем ДО
$valueBefore = (Get-ItemProperty -Path $regPath -Name $valueName -EA 0).$valueName
Write-Host "`n  Текущее значение (ДО): $valueBefore" -ForegroundColor Green

# ==========================================
# ШАГ 2: Проверка папки бэкапов GNO
# ==========================================
Write-Host "`n--- ШАГ 2: Поиск бэкапов GNO ---" -ForegroundColor Yellow

$searchPaths = @(
    "$env:APPDATA\E2E4\backups",
    "$env:APPDATA\E2E4\GNO\backups",
    "$env:LOCALAPPDATA\E2E4\backups",
    "$env:APPDATA\E2E4\sessions",
    "$env:LOCALAPPDATA\E2E4",
    "$env:APPDATA\E2E4"
)

$backupFound = $false
foreach ($path in $searchPaths) {
    if (Test-Path $path) {
        $files = Get-ChildItem $path -Recurse -File -EA 0
        if ($files) {
            Write-Host "  Найдена папка: $path" -ForegroundColor Green
            $files | Select-Object -First 5 Name, Length, LastWriteTime | Format-Table
            $backupFound = $true
        }
    }
}

if (-not $backupFound) {
    Write-Host "  Бэкапы не найдены. Ищем шире..." -ForegroundColor DarkYellow
    Get-ChildItem "$env:APPDATA" -Directory -EA 0 | Where-Object { $_.Name -match "E2E4|gno|GNO" } | ForEach-Object {
        Write-Host "  Каталог: $($_.FullName)" -ForegroundColor Yellow
        Get-ChildItem $_.FullName -Recurse -File -EA 0 | Select-Object -First 10 FullName, Length, LastWriteTime
    }
}

# ==========================================
# ШАГ 3: Инструкция по ручной проверке
# ==========================================
Write-Host "`n--- ШАГ 3: Ручная валидация ---" -ForegroundColor Yellow
Write-Host "
ИНСТРУКЦИЯ:
1. Откройте GNO → вкладка «Тонкая настройка» или «Оптимизатор»
2. Найдите действие 'Xbox Game DVR' / 'AppCaptureEnabled'
3. Нажмите 'Применить' для этого конкретного параметра
4. Вернитесь в PowerShell и нажмите Enter для продолжения
"
Read-Host "Нажмите Enter ПОСЛЕ применения параметра в GNO"

# Проверяем ПОСЛЕ
$valueAfter = (Get-ItemProperty -Path $regPath -Name $valueName -EA 0).$valueName
Write-Host "  Значение ПОСЛЕ применения: $valueAfter" -ForegroundColor Green

if ($valueAfter -ne $valueBefore) {
    Write-Host "  [OK] Значение изменилось: $valueBefore → $valueAfter" -ForegroundColor Green
} else {
    Write-Host "  [!] Значение не изменилось — проверьте, применилось ли действие" -ForegroundColor Red
}

# Проверяем бэкап
Write-Host "`n--- Проверка бэкапа ---" -ForegroundColor Yellow
foreach ($path in $searchPaths) {
    if (Test-Path $path) {
        $newFiles = Get-ChildItem $path -Recurse -File -EA 0 | Where-Object { $_.LastWriteTime -gt (Get-Date).AddMinutes(-5) }
        if ($newFiles) {
            Write-Host "  [OK] Найден свежий бэкап:" -ForegroundColor Green
            $newFiles | Select-Object FullName, Length, LastWriteTime
        }
    }
}

# ==========================================
# ШАГ 4: Откат
# ==========================================
Write-Host "`n--- ШАГ 4: Откат ---" -ForegroundColor Yellow
Write-Host "Нажмите Enter для отката через GNO"
Read-Host "Нажмите Enter ПОСЛЕ нажатия 'Откатить' в GNO"

$valueRolled = (Get-ItemProperty -Path $regPath -Name $valueName -EA 0).$valueName
Write-Host "  Значение ПОСЛЕ отката: $valueRolled" -ForegroundColor Green

if ($valueRolled -eq $valueBefore) {
    Write-Host "  [OK] Значение вернулось к исходному!" -ForegroundColor Green
} else {
    Write-Host "  [FAIL] Значение НЕ вернулось: ожидалось $valueBefore, получено $valueRolled" -ForegroundColor Red
}

# ==========================================
# ИТОГ
# ==========================================
Write-Host "`n=== ИТОГ ВАЛИДАЦИИ ===" -ForegroundColor Cyan
Write-Host "  ДО:        $valueBefore"
Write-Host "  ПОСЛЕ:     $valueAfter"
Write-Host "  ОТКАТ:     $valueRolled"
Write-Host "  Изменение: $(if ($valueAfter -ne $valueBefore) { 'ДА' } else { 'НЕТ' })"
Write-Host "  Откат:     $(if ($valueRolled -eq $valueBefore) { 'УСПЕШЕН' } else { 'НЕВДОЛЖЁН' })"
