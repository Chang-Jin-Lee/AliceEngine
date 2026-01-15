@echo off
setlocal ENABLEDELAYEDEXPANSION

REM =========================================================================
REM [설정] 설치 경로 지정 (이 부분만 수정하면 됩니다)
REM 1. "AUTO" 로 설정 시: D드라이브가 있으면 D:\vcpkg, 없으면 C:\vcpkg 자동 선택
REM 2. 특정 경로 지정 시: 예) set "USER_DEFINED_PATH=E:\MyLibs\vcpkg"
REM =========================================================================
set "USER_DEFINED_PATH=AUTO"

REM ===============================================
REM AliceRenderer vcpkg 셋업 스크립트 (최종 수정판)
REM ===============================================

echo [AliceRenderer] vcpkg 셋업을 시작합니다.

REM -----------------------------------------------------------
REM [1] Git 설치 여부 확인
REM -----------------------------------------------------------
where git >nul 2>nul
if %errorlevel% neq 0 (
    echo [오류] Git이 설치되어 있지 않거나 PATH에 없습니다.
    echo Git을 먼저 설치해주세요: https://git-scm.com/
    pause
    exit /b 1
)

REM -----------------------------------------------------------
REM [2] 설치 경로 결정 (우선순위: 사용자지정 > 환경변수 > 자동감지)
REM -----------------------------------------------------------

REM 1. 스크립트 상단 사용자 지정 경로 확인
if /i "%USER_DEFINED_PATH%" neq "AUTO" (
    set "TARGET_ROOT=%USER_DEFINED_PATH%"
    echo  - 스크립트 상단에 지정된 경로를 사용합니다: !TARGET_ROOT!
) else (
    REM 2. 시스템 환경변수 확인
    if defined VCPKG_ROOT (
        set "TARGET_ROOT=%VCPKG_ROOT%"
        echo  - 시스템 환경변수 VCPKG_ROOT를 사용합니다: !TARGET_ROOT!
    ) else (
        REM 3. 자동 감지 (D드라이브 유무)
        if exist "D:\" (
            set "TARGET_ROOT=D:\vcpkg"
            echo  - D드라이브가 감지되었습니다. 설치 경로: !TARGET_ROOT!
        ) else (
            set "TARGET_ROOT=C:\vcpkg"
            echo  - D드라이브가 없습니다. C드라이브에 설치합니다: !TARGET_ROOT!
        )
    )
)

set "VCPKG_EXE=%TARGET_ROOT%\vcpkg.exe"

REM -----------------------------------------------------------
REM [3] vcpkg 클론 및 부트스트랩
REM -----------------------------------------------------------

REM 폴더 자체가 없으면 클론
if not exist "%TARGET_ROOT%\.git" (
    echo.
    echo [1/4] vcpkg 저장소를 클론합니다...
    
    REM 폴더가 없으면 생성
    if not exist "%TARGET_ROOT%" mkdir "%TARGET_ROOT%"
    
    git clone https://github.com/microsoft/vcpkg.git "%TARGET_ROOT%"
    if !errorlevel! neq 0 (
        echo [오류] git clone 실패. 해당 폴더가 이미 존재하고 비어있지 않은지 확인하세요.
        pause
        exit /b 1
    )
) else (
    echo  - vcpkg 저장소가 이미 존재합니다. git pull로 업데이트를 시도합니다.
    pushd "%TARGET_ROOT%"
    git pull
    popd
)

REM vcpkg.exe가 없으면 빌드(bootstrap)
if not exist "%VCPKG_EXE%" (
    echo.
    echo [2/4] bootstrap-vcpkg.bat 실행 중...
    pushd "%TARGET_ROOT%"
    call bootstrap-vcpkg.bat
    if !errorlevel! neq 0 (
        echo [오류] bootstrap 실패.
        popd
        pause
        exit /b 1
    )
    popd
)

REM -----------------------------------------------------------
REM [4] 라이브러리 설치
REM -----------------------------------------------------------
echo.
echo [3/4] 필수 라이브러리 설치 (시간이 걸립니다)...

"%VCPKG_EXE%" install directxtk:x64-windows-static-md
"%VCPKG_EXE%" install directxtex[dx11]:x64-windows-static-md
"%VCPKG_EXE%" install imgui[dx11-binding]:x64-windows-static-md
"%VCPKG_EXE%" install imgui[win32-binding]:x64-windows-static-md --recurse
"%VCPKG_EXE%" install assimp:x64-windows

REM -----------------------------------------------------------
REM [5] Visual Studio 통합
REM -----------------------------------------------------------
echo.
echo [4/4] Visual Studio 통합 설정 (User-wide)
"%VCPKG_EXE%" integrate install

echo.
echo ========================================================
echo [완료] 모든 셋업이 끝났습니다.
echo Visual Studio를 재시작하면 라이브러리를 사용할 수 있습니다.
echo 설치 위치: %TARGET_ROOT%
echo ========================================================
echo.
pause
exit /b 0