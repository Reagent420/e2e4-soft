@echo off
chcp 65001 >nul
title GNO — Push to GitHub

echo ========================================
echo  GNO — Push to GitHub
echo ========================================
echo.

REM Проверка gh
where gh >nul 2>nul
if errorlevel 1 (
    echo [ERROR] GitHub CLI (gh) не найден.
    echo Установи: winget install GitHub.cli
    echo Или скачай: https://cli.github.com
    pause
    exit /b 1
)

echo [1/4] Авторизация в GitHub...
gh auth status 2>nul
if errorlevel 1 (
    echo Не авторизован — запускаю gh auth login...
    gh auth login --web --git-protocol https
    if errorlevel 1 (
        echo [ERROR] Авторизация не удалась
        pause
        exit /b 1
    )
) else (
    echo Уже авторизован.
)

echo.
echo [2/4] Проверка git remote...
git remote get-url origin >nul 2>nul
if errorlevel 1 (
    echo Remote 'origin' не настроен — создаю репозиторий...
    gh repo create gno-native --public --source=. --push
) else (
    echo Remote уже есть — просто пушу...
    git push -u origin main
)

if errorlevel 1 (
    echo.
    echo [ERROR] Push не удался
    pause
    exit /b 1
)

echo.
echo [3/4] Готово! Репозиторий: https://github.com/%USERNAME%/gno-native
echo.
echo [4/4] Проверь на GitHub.
echo.
pause