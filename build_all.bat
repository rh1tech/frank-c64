@echo off
setlocal EnableDelayedExpansion

REM ===== Paths (без кавычек в переменных) =====
set SRC_DIR=C:\Pico\murmulator\Cross\murmc64
set BUILD_DIR=%SRC_DIR%\build
set CMAKE=C:\Users\New\.pico-sdk\cmake\v3.31.5\bin\cmake.exe

REM ===== Config sets =====
set BOARD_VARIANTS=M1 M2 PC Z0
set VIDEO_TYPES=VGA HDMI
set AUDIO_TYPES=I2S PWM
set MOS2_MODES=OFF ON
set CPU_SPEEDS=378

for %%B in (%BOARD_VARIANTS%) do (
  for %%V in (%VIDEO_TYPES%) do (
    for %%A in (%AUDIO_TYPES%) do (
      for %%M in (%MOS2_MODES%) do (
        for %%C in (%CPU_SPEEDS%) do (

          REM --- skip invalid: CPU=428 only for VGA ---
          set "SKIP=0"
          if "%%C"=="428" if not "%%V"=="VGA" set "SKIP=1"

          REM --- skip invalid: Z0 and PC do not support VGA ---
          if "%%V"=="VGA" if "%%B"=="Z0" set "SKIP=1"
          if "%%V"=="VGA" if "%%B"=="PC" set "SKIP=1"

          REM --- skip invalid: PC does not support i2s audio ---
          if "%%B"=="PC" if "%%A"=="I2S" set "SKIP=1"

          if "!SKIP!"=="1" (
            echo Skipping: %%B %%V %%A MOS2=%%M CPU=%%C
          ) else (

            echo.
            echo ============================================
            echo Building: %%B %%V %%A MOS2=%%M CPU=%%C
            echo Build dir: !BUILD_DIR!
            echo ============================================

            if not exist "!BUILD_DIR!" mkdir "!BUILD_DIR!"
            pushd "!BUILD_DIR!" || (echo pushd failed & exit /b 1)

            echo "%CMAKE%" -S "%SRC_DIR%" -B "%BUILD_DIR%" ^
              -DBOARD_VARIANT=%%B ^
              -DVIDEO_TYPE=%%V ^
              -DAUDIO_TYPE=%%A ^
              -DMOS2=%%M ^
              -DCPU_SPEED=%%C

            "%CMAKE%" -S "%SRC_DIR%" -B "%BUILD_DIR%" ^
              -DBOARD_VARIANT=%%B ^
              -DVIDEO_TYPE=%%V ^
              -DAUDIO_TYPE=%%A ^
              -DMOS2=%%M ^
              -DCPU_SPEED=%%C

	    echo "%CMAKE%" --build "%BUILD_DIR%" --config MinSizeRel --target all
            "%CMAKE%" --build "%BUILD_DIR%" --config MinSizeRel --target all

            if errorlevel 1 (
              echo CMake configure FAILED
              popd
              exit /b 1
            )

            "%CMAKE%" --build . --config MinSizeRel --target all
            if errorlevel 1 (
              echo Build FAILED
              popd
              exit /b 1
            )

            popd
          )
        )
      )
    )
  )
)

echo.
echo ===== ALL BUILDS COMPLETED SUCCESSFULLY =====
endlocal
