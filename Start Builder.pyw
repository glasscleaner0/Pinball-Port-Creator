# SPDX-License-Identifier: GPL-3.0-only
import runpy,sys
from pathlib import Path
p=Path(__file__).resolve().parent/'tools/portable'
sys.path.insert(0,str(p))
runpy.run_path(str(p/'gui.py'),run_name='__main__')
