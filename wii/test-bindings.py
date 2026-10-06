#!/usr/bin/env python3
"""Ensure actual generated bindings expose implementations, not placeholders."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
host = subprocess.check_output(["cc", "-dumpmachine"], text=True).strip()
generator = root / "wii/.deps/netsurf-workspace" / f"inst-{host}/bin/nsgenbind"
with tempfile.TemporaryDirectory() as directory:
    output = Path(directory)
    subprocess.run([
        str(generator), "-I", str(root / "content/handlers/javascript/WebIDL"),
        str(root / "content/handlers/javascript/duktape/netsurf.bnd"), directory,
    ], check=True)
    input_binding = (output / "html_input_element.c").read_text()
    element_binding = (output / "element.c").read_text()
    assert '"validity"' not in input_binding, "Unsupported validity is exposed"
    assert '"checkValidity"' not in input_binding, "Unsupported validation method is exposed"
    assert '"value"' in input_binding, "Reflected input value was removed"
    assert '"setAttribute"' in element_binding, "Implemented DOM method was removed"
    assert '"getBoundingClientRect"' in element_binding, "Geometry binding was removed"
    assert '"screen"' in (output / "window.c").read_text(), "Screen binding was removed"
print("PASS: actual generator omits unsupported APIs and retains implemented/reflected bindings")
