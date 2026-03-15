import sys
from pathlib import Path

# Ensure the blobtemplates package (sibling to tests/) is importable
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
