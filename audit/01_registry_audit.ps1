# GNO Registry Audit Script
# Запускать от имени Администратора!
# Скрипт экспортирует все ключи реестра ДО применения оптимизаций GNO

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$outDir = "C:\GNO_Audit\Registry\$timestamp"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

Write-Host "=== GNO REGISTRY AUDIT ===" -ForegroundColor Cyan
Write-Host "Выгружаю ключи реестра в: $outDir" -ForegroundColor Yellow

# ==========================================
# 1. TCP/IP Parameters (Network tweaks)
# ==========================================
Write-Host "`n[1/7] Tcpip\Parameters..." -ForegroundColor Green
$tcpipPath = "HKLM:\SYSTEM\CurrentControlSet\Services\Tcpip\Parameters"
if (Test-Path $tcpipPath) {
    reg export $tcpipPath "$outDir\01_Tcpip_Parameters.reg" /y 2>$null
    # Отдельно выводим ключевые значения
    Get-ItemProperty -Path $tcpipPath -ErrorAction SilentlyContinue |
        Select-Object TcpAckFrequency, TcpNoDelay, TcpInitialRtt, DefaultTTL, Tcp1323Opts, TcpAutotuning |
        Format-List | Out-File "$outDir\01_Tcpip_Values.txt"
    Write-Host "  TcpAckFrequency = $((Get-ItemProperty $tcpipPath -Name TcpAckFrequency -EA 0).TcpAckFrequency)"
    Write-Host "  TcpNoDelay     = $((Get-ItemProperty $tcpipPath -Name TcpNoDelay -EA 0).TcpNoDelay)"
    Write-Host "  TcpInitialRtt  = $((Get-ItemProperty $tcpipPath -Name TcpInitialRtt -EA 0).TcpInitialRtt)"
} else {
    Write-Host "  [!] Ключ не найден" -ForegroundColor Red
}

# ==========================================
# 2. AFD Parameters (Winsock)
# ==========================================
Write-Host "`n[2/7] AFD\Parameters..." -ForegroundColor Green
$afdPath = "HKLM:\SYSTEM\CurrentControlSet\Services\AFD\Parameters"
if (Test-Path $afdPath) {
    reg export $afdPath "$outDir\02_AFD_Parameters.reg" /y 2>$null
    Get-ItemProperty -Path $afdPath -ErrorAction SilentlyContinue | Format-List | Out-File "$outDir\02_AFD_Values.txt"
} else {
    Write-Host "  Ключ не найден (нормально)" -ForegroundColor DarkGray
}

# ==========================================
# 3. Internet Settings (browser/proxy)
# ==========================================
Write-Host "`n[3/7] Internet Settings..." -ForegroundColor Green
$inetPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings"
reg export $inetPath "$outDir\03_Internet_Settings.reg" /y 2>$null
Get-ItemProperty -Path $inetPath -ErrorAction SilentlyContinue |
    Select-Object ProxyEnable, ProxyServer, AutoConfigURL |
    Format-List | Out-File "$outDir\03_Internet_Values.txt"

# ==========================================
# 4. MMCSS (Multimedia Class Scheduler)
# ==========================================
Write-Host "`n[4/7] Multimedia\SystemProfile..." -ForegroundColor Green
$mmcssPath = "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile"
if (Test-Path $mmcssPath) {
    reg export $mmcssPath "$outDir\04_MMCSS.reg" /y 2>$null
    Get-ItemProperty -Path $mmcssPath -ErrorAction SilentlyContinue |
        Select-Object NetworkThrottlingIndex, SystemResponsiveness |
        Format-List | Out-File "$outDir\04_MMCSS_Values.txt"
    Write-Host "  NetworkThrottlingIndex = $((Get-ItemProperty $mmcssPath -Name NetworkThrottlingIndex -EA 0).NetworkThrottlingIndex)"
    Write-Host "  SystemResponsiveness   = $((Get-ItemProperty $mmcssPath -Name SystemResponsiveness -EA 0).SystemResponsiveness)"
}

# ==========================================
# 5. Game DVR / GameConfigStore
# ==========================================
Write-Host "`n[5/7] Game DVR / GameConfigStore..." -ForegroundColor Green
$gamedvrPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\GameDVR"
$gamecfgPath = "HKCU:\System\GameConfigStore"
$gamebarPath = "HKCU:\Software\Microsoft\GameBar"

if (Test-Path $gamedvrPath) {
    reg export $gamedvrPath "$outDir\05a_GameDVR.reg" /y 2>$null
    Get-ItemProperty -Path $gamedvrPath -EA 0 | Select-Object AppCaptureEnabled, GameDVR_Enabled | Format-List | Out-File "$outDir\05a_GameDVR_Values.txt"
}
if (Test-Path $gamecfgPath) {
    reg export $gamecfgPath "$outDir\05b_GameConfigStore.reg" /y 2>$null
    Get-ItemProperty -Path $gamecfgPath -EA 0 | Select-Object GameDVR_Enabled, GameDVR_HonorUserFSEBehaviorMode, AppCaptureEnabled | Format-List | Out-File "$outDir\05b_GameConfig_Values.txt"
}
if (Test-Path $gamebarPath) {
    reg export $gamebarPath "$outDir\05c_GameBar.reg" /y 2>$null
    Get-ItemProperty -Path $gamebarPath -EA 0 | Select-Object AllowAutoGameMode | Format-List | Out-File "$outDir\05c_GameBar_Values.txt"
}

# ==========================================
# 6. Compatibility Flags (fullscreen optimizations)
# ==========================================
Write-Host "`n[6/7] AppCompatFlags (fullscreen)..." -ForegroundColor Green
$compatPath = "HKCU:\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers"
if (Test-Path $compatPath) {
    reg export $compatPath "$outDir\06_AppCompatLayers.reg" /y 2>$null
}

# ==========================================
# 7. Mouse settings
# ==========================================
Write-Host "`n[7/7] Mouse settings..." -ForegroundColor Green
$mousePath = "HKCU:\Control Panel\Mouse"
if (Test-Path $mousePath) {
    reg export $mousePath "$outDir\07_Mouse.reg" /y 2>$null
    Get-ItemProperty -Path $mousePath -EA 0 | Select-Object MouseSpeed, MouseThreshold1, MouseThreshold2 | Format-List | Out-File "$outDir\07_Mouse_Values.txt"
}

# ==========================================
# 8. Power Plan
# ==========================================
Write-Host "`n[Bonus] Power Plan..." -ForegroundColor Green
$activePlan = powercfg /getactivescheme 2>$null
$activePlan | Out-File "$outDir\08_PowerPlan.txt"
Write-Host "  $activePlan"

# ==========================================
# 9. CS2 config (if exists)
# ==========================================
Write-Host "`n[Bonus] CS2 config..." -ForegroundColor Green
$steamPath = (Get-ItemProperty "HKCU:\Software\Valve\Steam" -EA 0).SteamPath
if ($steamPath) {
    $cs2Cfg = Get-ChildItem "$steamPath\userdata\*\730\local\cfg\cs2_user_convars_0_pc.cfg" -EA 0
    if ($cs2Cfg) {
        Copy-Item $cs2Cfg.FullName "$outDir\09_CS2_Config.cfg" -Force
        Write-Host "  Найден: $($cs2Cfg.FullName)"
    }
}

# ==========================================
# Summary
# ==========================================
Write-Host "`n=== ЭКСПОРТ ЗАВЕРШЁН ===" -ForegroundColor Cyan
Write-Host "Папка: $outDir" -ForegroundColor Yellow
Write-Host "Файлы:" -ForegroundColor Yellow
Get-ChildItem $outDir | ForEach-Object { Write-Host "  $($_.Name)" }

Write-Host "`n--- ИНСТРУКЦИЯ ---" -ForegroundColor White
Write-Host "1. Запустите GNO и примените все оптимизации"
Write-Host "2. Запустите этот скрипт снова (создаст новую папку с текущей датой)"
Write-Host "3. Сравните файлы: FC /L 01_Tcpip_Parameters.reg <прошлая_версия> 01_Tcpip_Parameters.reg"
