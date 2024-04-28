@ECHO off

CD %~dp0

IF "%1" == "install" (
  ECHO Copying to C:\Windows\System32...
  ECHO This operation needs to be done as an administrator
  COPY %~dp0\bin\Release\pipy.exe %WINDIR%\System32\pipy.exe
  GOTO EOF
)

IF "%1" == "no-gui" (
  SET pipy_gui=OFF
) ELSE (
  IF "%1" == "" (
    SET pipy_gui=ON
  ) ELSE (
    ECHO "Usage: build.cmd [no-gui|install]"
    GOTO EOF
  )
)

IF NOT EXIST build (MD build)

CD build
cmake -DPIPY_GUI=%pipy_gui% -DPIPY_SAMPLES=%pipy_gui% -DPIPY_SOIL_FREED_SPACE=OFF -DPIPY_ASSERT_SAME_THREAD=OFF -DPIPY_LTO=OFF ..
msbuild pipy.sln -t:pipy -p:Configuration=Release
CD %~dp0

:EOF
