import json
from pathlib import Path
import sys

golden = json.loads(Path(__file__).with_name("builtin_kerning_golden.json").read_text())
lines = ["// Generated from the pre-compaction golden hashes; do not edit."]
lines += [f'#include "builtinFonts/{name}"' for name in golden]
lines += ["static constexpr FontCase kFonts[] = {"]
lines += [f'  {{"{name}", &{Path(name).stem}, {data["matrixFnv32"]}u}},' for name, data in golden.items()]
lines += ["};", ""]
Path(sys.argv[1]).write_text("\n".join(lines))
