@echo off
rem ------------------------------------------------------------------
rem flash.bat - nap ojt-can-node vao Arduino UNO bang avrdude (khong can Arduino IDE)
rem
rem   flash.bat COM5            crystal module MCP2515 = 8 MHz  (mac dinh)
rem   flash.bat COM5 16         crystal module MCP2515 = 16 MHz
rem   flash.bat COM5 8 57600    UNO clone doi cu (bootloader 57600)
rem
rem avrdude.exe + avrdude.conf: dat cung thu muc voi file nay, hoac them vao PATH.
rem Dong PuTTY / monitor dang mo cong COM truoc khi nap.
rem ------------------------------------------------------------------
setlocal
if "%~1"=="" (
    echo Usage: flash.bat COMx [8^|16] [115200^|57600]
    exit /b 1
)
set PORT=%~1
set MHZ=%~2
if "%MHZ%"=="" set MHZ=8
set BAUD=%~3
if "%BAUD%"=="" set BAUD=115200

set HEX=%~dp0ojt-can-node-%MHZ%mhz.hex
if not exist "%HEX%" (
    echo Khong thay file %HEX%  ^(chi co 8 hoac 16^)
    exit /b 1
)

set AVRDUDE=avrdude
if exist "%~dp0avrdude.exe" set AVRDUDE="%~dp0avrdude.exe"

echo Nap %HEX% vao %PORT% (%BAUD% baud) ...
%AVRDUDE% -c arduino -p m328p -P %PORT% -b %BAUD% -D -U flash:w:"%HEX%":i
if errorlevel 1 (
    echo.
    echo NAP THAT BAI: kiem tra so COM, dong PuTTY, thu: flash.bat %PORT% %MHZ% 57600
    exit /b 1
)
echo.
echo NAP XONG. Mo PuTTY: Serial %PORT% 115200 de go lenh.
endlocal
