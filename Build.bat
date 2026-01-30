@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM ----------------------------------------------------------------------
REM [창 제어 설정]
REM /c : 스크립트 실행이 끝나면 창을 닫습니다. (단, 마지막 pause 대기 후)
REM ----------------------------------------------------------------------
if /i "%~1" neq "__INVOKED" (
    cmd /c ""%~f0" __INVOKED"
    exit /b
)

echo ========================================================
echo [Build.bat] Engine 빌드 시스템 (Folder Mode)
echo 목표: 라이브러리 셋업 및 솔루션 생성 자동화
echo ========================================================

set "EXIT_CODE=0"
set "ENGINE_DIR=%~dp0Engine"

REM -----------------------------------------------------------
REM 1. Engine 폴더 확인
REM (서브모듈이 아니므로 없으면 다운로드할 수 없음, 에러 처리)
REM -----------------------------------------------------------
if not exist "%ENGINE_DIR%\" (
    echo.
    echo [FAIL] 'Engine' 폴더를 찾을 수 없습니다!
    echo 이 배치 파일과 같은 위치에 Engine 폴더가 있는지 확인해주세요.
    set "EXIT_CODE=1"
    goto :End
)

REM -----------------------------------------------------------
REM 2. Setup 및 CMake 빌드 실행
REM -----------------------------------------------------------
echo.
echo [STEP 1] Engine Setup (라이브러리 설정)
pushd "%ENGINE_DIR%"

if exist "Setup.bat" (
    REM [핵심] echo. | call ... 
    REM 내부 pause를 스킵하여 매끄럽게 진행
    echo. | call Setup.bat
    if errorlevel 1 (
        echo [FAIL] Setup.bat 실행 실패
        popd
        set "EXIT_CODE=1"
        goto :End
    )
) else (
    echo [FAIL] Setup.bat 파일이 없습니다.
    popd
    set "EXIT_CODE=1"
    goto :End
)

echo.
echo [STEP 2] 솔루션 생성 (build_msvc.cmd)
if exist "build_msvc.cmd" (
    REM [핵심] echo. | call ...
    REM 내부 pause를 스킵
    echo. | call build_msvc.cmd
    if errorlevel 1 (
        echo [FAIL] build_msvc.cmd 실행 실패
        popd
        set "EXIT_CODE=1"
        goto :End
    )
) else (
    echo [FAIL] build_msvc.cmd 파일이 없습니다.
    popd
    set "EXIT_CODE=1"
    goto :End
)

popd

echo.
echo ========================================================
echo [SUCCESS] 모든 작업이 완료되었습니다.
echo Build 폴더에서 솔루션 파일을 확인하세요.
echo ========================================================

:End
echo.
if "%EXIT_CODE%"=="0" (
    echo [RESULT] SUCCESS
) else (
    echo [RESULT] FAIL ^(ExitCode=%EXIT_CODE%^)
)

echo.
echo (아무 키나 누르면 창이 닫힙니다.)
pause
exit /b %EXIT_CODE%