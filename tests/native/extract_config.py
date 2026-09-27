#!/usr/bin/env python3
"""Copy the port's exact configuration function into the native build tree."""
import hashlib
import json
from pathlib import Path
import sys

root, output = map(Path, sys.argv[1:])
source = root / "plugins/openvins/plugin.cpp"
text = source.read_text()
start = text.index("VIOConfig create_vio_config()")
brace = text.index("{", start)
depth = 1
end = brace + 1
while depth:
    depth += (text[end] == "{") - (text[end] == "}")
    end += 1
function = text[start:end]
output.mkdir(parents=True, exist_ok=True)
(output / "vio_config.hpp").write_text(
    "// Generated from plugin.cpp; never edit this copy.\n"
    "#pragma once\n#include \"SLAMMath.hpp\"\nnamespace OpenVINS {\n"
    + function + "\n}\n")
files = [root / "plugins/openvins/SLAMMath.cpp", root / "plugins/openvins/SLAMMath.hpp"]
manifest = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
manifest["create_vio_config_sha256"] = hashlib.sha256(function.encode()).hexdigest()
(output / "estimator_sources.json").write_text(json.dumps(manifest, indent=2) + "\n")
