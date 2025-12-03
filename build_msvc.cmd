@echo off
setlocal

rem Visual Studio 2022용 솔루션 생성 (모든 빌드 산출물은 build/ 폴더 안에 생성)

cmake -S . -B build -G "Visual Studio 17 2022"
if errorlevel 1 (
    echo [CMake] 프로젝트 생성 실패.
    exit /b 1
)

echo [OK] build\AliceRenderer.sln 이(가) 생성되었습니다. 이 파일을 열어 사용하세요.

endlocal
pause


