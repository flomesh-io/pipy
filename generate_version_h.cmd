@echo off

SET cur_dir=%CD%
CD %~dp0

for /f %%i in ('git describe --abbrev^=0 --tags') do set VERSION=%%i

for /f %%i in ('git log -1 --format^=%%H') do set COMMIT=%%i

for /f "eol= tokens=* delims=" %%i in ('"git log -1 --format=%%cD"') do set COMMIT_DATE=%%i

echo. > %1

echo #ifndef __VERSION_H__ >> %1
echo #define __VERSION_H__ >> %1
echo. >> %1
echo #define PIPY_VERSION "%VERSION%" >> %1
echo #define PIPY_COMMIT "%COMMIT%" >> %1
echo #define PIPY_COMMIT_DATE "%COMMIT_DATE%" >> %1
echo. >> %1
echo #endif // __VERSION_H__ >> %1

CD "%cur_dir%"
