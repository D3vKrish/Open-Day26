@echo off
title Embedded Equipment Health Monitor - Web Dashboard
echo ============================================================
echo   Embedded Equipment Health & Communication Monitor
echo         Electronics & Robotics Club | BITS Pilani
echo ============================================================
echo.
echo Starting Python Flask Server on Port 5000...
echo.
start "" http://localhost:5000
python server.py
pause
