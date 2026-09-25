#!/usr/bin/env python3
# lazy
import glob, json, os, sys

cxx, flags = sys.argv[1], sys.argv[2]
root = os.getcwd()

entries = []
for src in sorted(glob.glob("src/kernel/**/*.cpp", recursive=True)):
    obj = os.path.join(root, "build", src[len("src/kernel/"):].replace(".cpp", ".o"))
    entries.append({
        "directory": root,
        "file": os.path.join(root, src),
        "command": f"{cxx} {flags} -c {src} -o {obj}",
    })

with open("compile_commands.json", "w") as f:
    json.dump(entries, f, indent=2)