import sys
from pathlib import Path

# Ensure src/ is on sys.path so 'import gui' works
_src = Path(__file__).resolve().parent.parent
if str(_src) not in sys.path:
    sys.path.insert(0, str(_src))

from gui.main import run_gui

if __name__ == "__main__":
    run_gui()
