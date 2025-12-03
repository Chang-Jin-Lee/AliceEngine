@echo off
setlocal ENABLEDELAYEDEXPANSION

REM ===============================================
REM AliceRenderer vcpkg 최초 셋업 스크립트
REM - vcpkg 클론 + bootstrap
REM - DirectXTK / DirectXTex / ImGui / Assimp 설치
REM ===============================================

echo [AliceRenderer] vcpkg 셋업을 시작합니다.

REM 기본 vcpkg 경로 (원하면 수정 가능)
set "VCPKG_ROOT=D:\vcpkg"

REM 이미 VCPKG_ROOT 환경변수가 있으면 그 값을 우선 사용
if defined VCPKG_ROOT_ENV (
    set "VCPKG_ROOT=%VCPKG_ROOT_ENV%"
)

REM 사용자가 VCPKG_ROOT 환경변수를 설정했는지 확인
if defined VCPKG_ROOT (
    echo  - VCPKG_ROOT = %VCPKG_ROOT%
) else (
    echo  - VCPKG_ROOT 가 설정되지 않았습니다. 기본값 D:\vcpkg 를 사용합니다.
)

REM vcpkg 폴더가 없으면 클론
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo.
    echo [1/3] vcpkg 저장소를 클론합니다...
    git clone https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
    if errorlevel 1 (
        echo git clone 실패. 경로/네트워크를 확인하세요.
        pause
        exit /b 1
    )

    echo.
    echo [2/3] bootstrap-vcpkg.bat 실행 중...
    pushd "%VCPKG_ROOT%"
    call bootstrap-vcpkg.bat
    if errorlevel 1 (
        echo bootstrap-vcpkg.bat 실패.
        popd
        pause
        exit /b 1
    )
    popd
) else (
    echo  - 기존 vcpkg 설치를 사용합니다. (%VCPKG_ROOT%)
)

echo.
echo [3/3] 필수 라이브러리 설치 (시간이 조금 걸릴 수 있습니다)...
set "VCPKG_EXE=%VCPKG_ROOT%\vcpkg.exe"

REM 강제 정적 라이브러리(CRT는 DLL) 트리플릿: x64-windows-static-md
"%VCPKG_EXE%" install directxtk:x64-windows-static-md
"%VCPKG_EXE%" install directxtex[dx11]:x64-windows-static-md
"%VCPKG_EXE%" install imgui[dx11-binding]:x64-windows-static-md
"%VCPKG_EXE%" install imgui[win32-binding]:x64-windows-static-md --recurse

REM Assimp 는 동적 라이브러리로 설치 (향후 FBX/PMX 로더에서 사용 예정)
"%VCPKG_EXE%" install assimp:x64-windows

echo.
echo [완료] vcpkg 셋업이 끝났습니다.
echo  - CMake 에서는 CMakeLists.txt 안의 VCPKG_ROOT/VCPKG_TRIPLET_* 설정을 사용합니다.
echo  - 기본값: VCPKG_ROOT=%VCPKG_ROOT%
echo.
pause
exit /b 0



