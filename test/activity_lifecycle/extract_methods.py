"""Compile production transition methods with host-only UI/RTOS stand-ins."""

from pathlib import Path
import re
import sys


source = Path(sys.argv[1]).read_text(encoding="utf-8")
methods = ("loop", "exitActivity", "replaceActivity", "pushActivity", "popActivity", "requestUpdate")
boundaries = list(re.finditer(r"^(?:void|bool|ScreenshotInfo) ActivityManager::(\w+)\(", source, re.MULTILINE))
selected = {}
for index, match in enumerate(boundaries):
    if match.group(1) in methods:
        end = boundaries[index + 1].start() if index + 1 < len(boundaries) else len(source)
        selected[match.group(1)] = source[match.start():end]
if set(selected) != set(methods):
    raise RuntimeError("ActivityManager transition methods changed; review the lifecycle test harness")
Path(sys.argv[2]).write_text("\n".join(selected[name] for name in methods), encoding="utf-8")
