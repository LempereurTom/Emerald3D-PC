@echo off
if "%PC_PYTHON%"=="" (python -I "%~dp0pc_nm.py" %*) else ("%PC_PYTHON%" -I "%~dp0pc_nm.py" %*)
