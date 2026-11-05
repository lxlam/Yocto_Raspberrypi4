@echo off
rem OJT CAN Monitor - double click to start
cd /d "%~dp0"
py -c "import serial, matplotlib" 2>nul || (
    echo Installing pyserial + matplotlib ...
    py -m pip install -r requirements.txt
)
py can_monitor.py
if errorlevel 1 pause
