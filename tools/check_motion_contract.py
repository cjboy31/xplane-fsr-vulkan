"""Check that the shared motion contract matches the pinned v1 files.

SPDX-License-Identifier: MIT
Byte-identical in the FSR / frame generation mod and Native AA. If you change
the contract, change it in both repositories in the same pass and update the
hashes below in both copies of this script.
"""
import hashlib
import sys
from pathlib import Path

PINNED = {
    "motion_contract.hpp": "f1c1b1ba2a10491705b90ae949c1c4da7327379b6b944e0bf9a77bc38f83c915",
    "motion_contract.glsl": "f2b609ba34c84a13897c06775db4e410f5cbda552d4cd36ceb450f2cc7865270",
    "motion_contract_tests.cpp": "3525464197e81924c9323f54f60b37b22313e9b621b1dfe440ffa2118847aabe",
}


def main():
    root = Path(__file__).resolve().parent.parent
    skip = {".git", "build", "app", "dist", "__pycache__"}
    names = [str(p.relative_to(root)) for p in root.rglob("motion_contract*")
             if p.is_file() and not skip.intersection(p.relative_to(root).parts)]
    found = {}
    for name in names:
        base = Path(name).name
        if base in PINNED:
            if base in found:
                print(f"FAIL: more than one {base}: {found[base]} and {name}")
                return 1
            found[base] = name
    problems = []
    for base, expected in PINNED.items():
        if base not in found:
            problems.append(f"missing {base}")
            continue
        actual = hashlib.sha256((root / found[base]).read_bytes()).hexdigest()
        if actual != expected:
            problems.append(f"{found[base]} differs from the pinned contract ({actual})")
    if problems:
        print("FAIL: " + "; ".join(problems))
        return 1
    print("PASS: shared motion contract v1 matches the pinned files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
