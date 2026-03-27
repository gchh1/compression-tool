.PHONY: all pyd exe run clean

PYTHON ?= python
SCRIPT := test_ctypes_global.py
MODULE := c_api_test
PYD := $(MODULE).cp312-win_amd64.pyd
EXE_NAME := test_pybind_global
DIST_EXE := dist/$(EXE_NAME).exe

all: exe

pyd:
	$(PYTHON) -m pip install pybind11 pyinstaller
	g++ -O3 -Wall -shared -std=c++17 -ID:/Python/Include -ID:/Python/Lib/site-packages/pybind11/include c_api_test_pybind.cpp -LD:/Python/libs -lpython312 -o $(PYD)
	powershell -NoProfile -Command "$$bin = Split-Path (Get-Command gcc).Source; Copy-Item (Join-Path $$bin 'libstdc++-6.dll') -Destination . -Force; Copy-Item (Join-Path $$bin 'libgcc_s_seh-1.dll') -Destination . -Force; Copy-Item (Join-Path $$bin 'libwinpthread-1.dll') -Destination . -Force"

exe: pyd
	$(PYTHON) -m PyInstaller --noconfirm --clean --onefile --name $(EXE_NAME) --add-binary "$(PYD);." --add-binary "libstdc++-6.dll;." --add-binary "libgcc_s_seh-1.dll;." --add-binary "libwinpthread-1.dll;." $(SCRIPT)

run: pyd
	$(PYTHON) $(SCRIPT)

clean:
	powershell -NoProfile -Command "if (Test-Path 'build') { Remove-Item -Recurse -Force 'build' }; if (Test-Path 'dist') { Remove-Item -Recurse -Force 'dist' }; if (Test-Path '$(EXE_NAME).spec') { Remove-Item -Force '$(EXE_NAME).spec' }; if (Test-Path '$(PYD)') { Remove-Item -Force '$(PYD)' }"

