@echo off
rem Host-side math tests: compiles the real firmware sources on the PC and runs
rem the assertions in test_math.cpp. Requires: pip install --user ziglang
cd /d "%~dp0.."
python -m ziglang c++ -std=c++17 -w -Isrc src/geo.cpp src/fusion.cpp src/sun.cpp src/route_util.cpp test/test_math.cpp -o test/test_math.exe || exit /b 1
test\test_math.exe
