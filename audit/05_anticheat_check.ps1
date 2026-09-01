# GNO Anti-Cheat Compatibility Check
# Проверка влияния оптимизаций GNO на анти-читы
# Запускать от имени Администратора!

$ErrorActionPreference = "SilentlyContinue"
Write-Host "=== GNO ANTI-CHEAT COMPATIBILITY CHECK ===" -ForegroundColor Cyan

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$outDir = "C:\GNO_Audit\AntiCheat\$timestamp"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

# ==========================================
# 1. Статус служб анти-читов ДО
# ==========================================
Write-Host "`n[1/5] Статус служб анти-читов..." -ForegroundColor Green

$antiCheatServices = @(
    "BEService",              # BattlEye
    "BattlEye",               # BattlEye
    "EasyAntiCheat",          # Easy Anti-Cheat
    "EasyAntiCheatSvc",       # EAC service
    "vgc",                    # Vanguard (Riot)
    "vgk",                    # Vanguard kernel
    "npsvctrig",              # NVIDIA (может конфликтовать)
    "PlayerFrameworkService", # Xbox
    "XblAuthManager",         # Xbox Live
    "XblGameSave",            # Xbox Live
    "XboxNetApiSvc"           # Xbox Live
)

Write-Host "`n  Службы анти-читов:" -ForegroundColor Yellow
$serviceReport = @()
foreach ($svc in $antiCheatServices) {
    $service = Get-Service -Name $svc -EA 0
    if ($service) {
        $status = [PSCustomObject]@{
            Name   = $svc
            Status = $service.Status
            StartType = $service.StartType
        }
        $serviceReport += $status
        $color = if ($service.Status -eq "Running") { "Green" } else { "DarkGray" }
        Write-Host "    $($svc): $($service.Status) ($($service.StartType))" -ForegroundColor $color
    }
}
$serviceReport | Export-Csv "$outDir\01_services_before.csv" -NoTypeInformation

# ==========================================
# 2. Проверка драйверов анти-читов
# ==========================================
Write-Host "`n[2/5] Проверка драйверов..." -ForegroundColor Green

$antiCheatDrivers = @(
    "*BattlEye*", "*BE*", "*EAC*", "*EasyAntiCheat*",
    "*vgk*", "*vgc*", "*RIOT*"
)

$driverReport = @()
foreach ($pattern in $antiCheatDrivers) {
    $drivers = Get-WmiObject Win32_PnPSignedDriver -EA 0 | Where-Object { $_.DeviceName -like $pattern }
    foreach ($d in $drivers) {
        $driverReport += [PSCustomObject]@{
            DeviceName = $d.DeviceName
            DriverVersion = $d.DriverVersion
            DriverDate = $d.DriverDate
            InfName = $d.InfName
        }
        Write-Host "    $($d.DeviceName) v$($d.DriverVersion)" -ForegroundColor Green
    }
}
$driverReport | Export-Csv "$outDir\02_drivers.csv" -NoTypeInformation

# ==========================================
# 3. Проверка изменений реестра, затрагивающих анти-читы
# ==========================================
Write-Host "`n[3/5] Проверка реестра (влияние на анти-читы)..." -ForegroundColor Yellow

# Проверяем ключи, которые GNO изменяет и которые МОГУТ повлиять на анти-читы
$riskyKeys = @(
    @{Path="HKLM:\SYSTEM\CurrentControlSet\Control\PriorityControl"; Name="Win32PrioritySeparation"; Desc="CPU scheduling"},
    @{Path="HKLM:\SYSTEM\CurrentControlSet\Services\Tcpip\Parameters"; Name="TcpAckFrequency"; Desc="TCP ACK frequency"},
    @{Path="HKLM:\SYSTEM\CurrentControlSet\Services\Tcpip\Parameters"; Name="TcpNoDelay"; Desc="Nagle algorithm"},
    @{Path="HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile"; Name="NetworkThrottlingIndex"; Desc="Network throttling"},
    @{Path="HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile"; Name="SystemResponsiveness"; Desc="System responsiveness"},
    @{Path="HKLM:\SYSTEM\CurrentControlSet\Control\GraphicsDrivers"; Name="HwSchMode"; Desc="Hardware GPU scheduling"},
    @{Path="HKCU:\System\GameConfigStore"; Name="GameDVR_Enabled"; Desc="Game DVR"},
    @{Path="HKCU:\Software\Microsoft\GameBar"; Name="AllowAutoGameMode"; Desc="Game Mode"}
)

foreach ($k in riskyKeys) {
    if (Test-Path $k.Path) {
        $val = (Get-ItemProperty -Path $k.Path -Name $k.Name -EA 0).($k.Name)
        if ($null -ne $val) {
            Write-Host "    $($k.Desc): $($k.Name) = $val" -ForegroundColor White
        }
    }
}

# ==========================================
# 4. Проверка целостности файлов анти-читов
# ==========================================
Write-Host "`n[4/5] Проверка файлов анти-читов..." -ForegroundColor Green

$acPaths = @(
    "$env:ProgramFiles\BattlEye",
    "$env:ProgramFiles(x86)\BattlEye",
    "$env:ProgramFiles\EasyAntiCheat",
    "$env:ProgramFiles(x86)\EasyAntiCheat",
    "$env:ProgramFiles\Riot Vanguard",
    "C:\Riot Vanguard"
)

foreach ($p in $acPaths) {
    if (Test-Path $p) {
        $files = Get-ChildItem $p -Recurse -File -EA 0
        Write-Host "    $p : $($files.Count) файлов" -ForegroundColor Green
        $files | Select-Object Name, Length, LastWriteTime | Export-Csv "$outDir\04_ac_files_$(Split-Path $p -Leaf).csv" -NoTypeInformation
    }
}

# ==========================================
# 5. Рекомендации
# ==========================================
Write-Host "`n[5/5] РЕКОМЕНДАЦИИ ПО СОВМЕСТИМОСТИ" -ForegroundColor Cyan
Write-Host "
  БЕЗОПАСНЫЕ оптимизации GNO (НЕ затрагивают анти-читы):
  [OK] DNS серверы (1.1.1.1 / 1.0.0.1)
  [OK] MTU интерфейса (netsh)
  [OK] Power Plan (Высокая производительность)
  [OK] Game DVR отключение (только запись клипов)
  [OK] Отключение анимаций Windows
  [OK] USB Selective Suspend
  [OK] Power Throttling
  [OK] Отключение телеметрии Windows
  [OK] Visual Effects → Best Performance
  [OK] Startup Delay = 0

  ПОТЕНЦИАЛЬНО РИСКОВАННЫЕ (могут триггерить анти-читы):
  [!] Win32PrioritySeparation = 38 (CPU scheduling)
  [!] HAGS (Hardware-Accelerated GPU Scheduling)
  [!] NetworkThrottlingIndex = 0xFFFFFFFF
  [!] SystemResponsiveness = 0
  [!] TcpAckFrequency = 1 (Nagle off)
  [!] Приоритет процесса AboveNormal/High

  ОПАСНЫЕ (НЕ применяйте без необходимости):
  [X] Изменение системных драйверов
  [X] Отключение служб безопасности Windows
  [X] Модификация файловой системы игр
" -ForegroundColor White

# ==========================================
# ИТОГ
# ==========================================
Write-Host "`n=== ИТОГ ===" -ForegroundColor Cyan
Write-Host "Папка: $outDir" -ForegroundColor Yellow
Write-Host "Скопируйте вывод этого скрипта в GNO для анализа."
