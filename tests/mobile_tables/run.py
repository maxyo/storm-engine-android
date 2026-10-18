#!/usr/bin/env python3
"""Exercise real table collection with engine attributes and JSON serialization."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
source = root / "src/libs/xinterface/src"
table = (source / "nodes/xi_table.cpp").read_text()
collect = table[table.index("void CXI_TABLE::MobileCollect("):table.index("bool CXI_TABLE::MobileApply(")]
with tempfile.TemporaryDirectory(prefix="storm-mobile-tables-") as directory:
    build = Path(directory)
    (build / "table_collect.inc").write_text(collect)
    apply = table[table.index("bool CXI_TABLE::MobileApply("):].split("\n#endif", 1)[0]
    (build / "table_apply.inc").write_text(apply)
    binary = build / "check"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++20", "-O1", "-g", "-DEX_OFF",
        "-Wno-deprecated-declarations", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-include", "memory", "-I" + str(root / "tests/mobile_inventory/stubs"),
        "-I" + str(root / "src/libs/core/include"), "-I" + str(root / "src/libs/util/include"),
        "-I" + str(source), "-I" + str(build), str(here / "check.cpp"),
        str(root / "src/libs/core/src/attributes.cpp"), "-o", str(binary),
    ], check=True)
    snapshots = [json.loads(line) for line in subprocess.check_output([str(binary)], text=True).splitlines()]
    plain, sparse, changed, removed = snapshots
    assert all(item["schemaVersion"] == 2 for item in snapshots)
    assert all("columnHeaders" not in row for row in plain["widgets"] + removed["widgets"])
    expected = ['Товар "A"\nшт.', '', 'Вес']
    assert all(row["columnHeaders"] == expected for row in sparse["widgets"])
    assert all(row["columnHeaders"] == ['', '', 'Новый вес'] for row in changed["widgets"])
    for before, after in zip(plain["widgets"], sparse["widgets"]):
        assert {key: value for key, value in after.items() if key != "columnHeaders"} == before
print("PASS table headers preserve sparse column indexes, UTF-8/escaping, updates, absent headers and existing row contract; commerce row/owner/epoch/lock guards execute real MobileApply")
