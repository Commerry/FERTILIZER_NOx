@echo off
REM ===========================================================================
REM  sync_protocol.bat
REM
REM  link_protocol.h มี 2 สำเนา (Nano กับ ESP32) และต้องเหมือนกันเป๊ะ ๆ
REM  ถ้าไม่ตรงกัน: CRC จะไม่ตรง -> ลิงก์เงียบสนิท -> ไล่หาสาเหตุยากมาก
REM
REM  ดับเบิลคลิกไฟล์นี้ทุกครั้งหลังแก้ link_protocol.h
REM ===========================================================================
setlocal
set SRC=%~dp0link_protocol.h
set DST=%~dp0GUITION ESP32-4848S040\Guition_HMI\link_protocol.h

copy /Y "%SRC%" "%DST%" >nul
if errorlevel 1 (
    echo [FAIL] คัดลอกไม่สำเร็จ
    pause
    exit /b 1
)

fc /B "%SRC%" "%DST%" >nul
if errorlevel 1 (
    echo [FAIL] ไฟล์ยังไม่ตรงกัน
    pause
    exit /b 1
)

echo [OK] link_protocol.h ตรงกันแล้วทั้ง 2 ฝั่ง
echo      Nano  : %SRC%
echo      ESP32 : %DST%
pause
