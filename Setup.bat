REM -----------------------------------------------------------
REM [5] 스카이박스 리소스 다운로드 (GitHub Direct Link)
REM -----------------------------------------------------------
echo.
echo [4/5] 스카이박스 리소스 확인 및 다운로드...

REM GitHub Releases 링크
set "DOWNLOAD_URL=https://github.com/Chang-Jin-Lee/D3D11-AliceTutorial/releases/download/Skybox/Skybox.7z"

REM 현재 배치 파일이 있는 위치 기준으로 Resource 폴더 경로 설정
set "RES_ROOT=%~dp0Resource\Skybox"
set "TEMP_ARC=skybox_temp.7z"

REM 검사할 하위 폴더들
set "CHECK_DIR_1=%RES_ROOT%\Bridge"
set "CHECK_DIR_2=%RES_ROOT%\Sample"
set "CHECK_DIR_3=%RES_ROOT%\Indoor"

REM 세 폴더가 모두 존재하는지 확인
if exist "%CHECK_DIR_1%" (
    if exist "%CHECK_DIR_2%" (
        if exist "%CHECK_DIR_3%" (
            echo  - 이미 스카이박스 리소스가 존재합니다. 다운로드를 건너뜁니다.
            goto SKIP_RESOURCE_DOWNLOAD
        )
    )
)

echo  - 리소스가 누락되었습니다. 다운로드를 시작합니다.
if not exist "%RES_ROOT%" mkdir "%RES_ROOT%"

REM 1. 7zip 압축 해제용 툴(Standalone Console Version) 임시 다운로드
echo  - 압축 해제 도구(7zr.exe) 다운로드 중...
curl -L -o 7zr.exe https://www.7-zip.org/a/7zr.exe >nul 2>&1
if not exist "7zr.exe" (
    echo [오류] 7zr.exe 다운로드 실패. 인터넷 연결을 확인하세요.
    goto SKIP_RESOURCE_DOWNLOAD
)

REM 2. 파일 다운로드 (curl -L 옵션으로 리다이렉트 자동 처리)
echo  - 리소스 파일 다운로드 중... 
echo    URL: %DOWNLOAD_URL%
curl -L -o "%TEMP_ARC%" "%DOWNLOAD_URL%"

REM 파일 유효성 검사 (다운로드 실패 체크)
if not exist "%TEMP_ARC%" (
    echo [오류] 다운로드 파일이 생성되지 않았습니다.
    goto CLEANUP_AND_SKIP
)

REM 파일 크기가 너무 작으면(10KB 미만) 에러로 간주 (GitHub 404 등)
for %%I in ("%TEMP_ARC%") do if %%~zI LSS 10000 (
    echo.
    echo [오류] 다운로드된 파일 크기가 비정상적으로 작습니다 (%%~zI bytes).
    echo GitHub 링크가 잘못되었거나 파일을 찾을 수 없습니다.
    goto CLEANUP_AND_SKIP
)

REM 3. 압축 해제
echo  - 압축 해제 중...
7zr.exe x "%TEMP_ARC%" -o"%RES_ROOT%" -y >nul
if %errorlevel% neq 0 (
    echo [오류] 압축 해제 중 에러가 발생했습니다.
    goto CLEANUP_AND_SKIP
)

echo  - 리소스 설치 완료!

:CLEANUP_AND_SKIP
REM 4. 임시 파일 정리
echo  - 임시 파일 정리 중...
if exist 7zr.exe del 7zr.exe
if exist "%TEMP_ARC%" del "%TEMP_ARC%"

:SKIP_RESOURCE_DOWNLOAD