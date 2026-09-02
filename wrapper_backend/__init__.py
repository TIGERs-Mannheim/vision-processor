from __future__ import annotations

import pathlib
import sys

_BACKEND_DIR = pathlib.Path(__file__).resolve().parent
_REPO_ROOT = _BACKEND_DIR.parent
_PROTO_DEFS = _REPO_ROOT / "proto"
_PROTO_SRC = _PROTO_DEFS / "proto" / "vision"
_PROTO_OUT = _BACKEND_DIR / "proto" / "vision"

sys.path.insert(0, str(_PROTO_DEFS / "python"))
from python_bindings import generate_python_bindings  # noqa: E402


def _bindings_are_stale() -> bool:
    outputs = list(_PROTO_OUT.glob("*_pb2.py"))
    if not outputs:
        return True
    sources = list(_PROTO_SRC.glob("*.proto"))
    if not sources:
        return True
    oldest_out = min(o.stat().st_mtime for o in outputs)
    newest_src = max(s.stat().st_mtime for s in sources)
    return newest_src > oldest_out


if _bindings_are_stale():
    print("Compiling Protobuf files...", file=sys.stderr)
    # The generator nests everything under `proto` and rewrites the generated
    # cross-imports to match; see proto/python/python_bindings.py for why bare
    # `--python_out` is not enough.
    generate_python_bindings(
        out_dir=_BACKEND_DIR, package="proto", includes=["vision", "gamecontroller"]
    )

# The generated package is `wrapper_backend/proto/`, so `wrapper_backend/` must
# be on sys.path for `from proto.vision.* import ...` to resolve.
sys.path.insert(0, str(_BACKEND_DIR))
