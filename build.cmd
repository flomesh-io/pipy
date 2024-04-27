@echo off

CD %~dp0

IF "%1" == "no-gui" (
  SET pipy_gui=OFF
) ELSE (
  SET pipy_gui=ON
)

IF NOT EXIST build (MD build)

CD build
cmake -DPIPY_GUI=%pipy_gui% -DPIPY_SAMPLES=%pipy_gui% -DPIPY_SOIL_FREED_SPACE=OFF -DPIPY_ASSERT_SAME_THREAD=OFF -DPIPY_LTO=OFF ..
msbuild pipy.sln -t:pipy -p:Configuration=Release
