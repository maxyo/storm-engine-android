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
    plain, sparse, changed, removed, empty, nations, reordered, gaps, untranslated, no_service, bad_uv, texture = snapshots
    assert all(item["schemaVersion"] == 2 for item in snapshots)
    assert all("columnHeaders" not in row for row in plain["widgets"] + removed["widgets"])
    expected = ['Товар "A"\nшт.', '', 'Вес']
    assert all(row["columnHeaders"] == expected for row in sparse["widgets"])
    assert all(row["columnHeaders"] == ['', '', 'Новый вес'] for row in changed["widgets"])
    for before, after in zip(plain["widgets"], sparse["widgets"]):
        assert {key: value for key, value in after.items() if key not in ("columnHeaders", "headerCells")} == before
    assert all("headerCells" not in row for row in plain["widgets"] + removed["widgets"])
    assert [cell["column"] for cell in sparse["widgets"][0]["headerCells"]] == [0, 2]
    assert [cell["column"] for cell in changed["widgets"][0]["headerCells"]] == [0, 1, 2]
    assert all(row["headerCells"] == [] for row in empty["widgets"])
    header = nations["widgets"][0]["headerCells"]
    assert header[1]["images"][0]["source"] == {"kind": "atlas", "group": "NATIONS", "image": 'Zeta"\\\n'}
    assert header[1]["text"] == 'Реальная "страна"\n'
    assert header[1]["colorArgb"] == 0xff123456
    assert header[2]["text"] == '' and header[2]["images"][0]["source"]["image"] == 'Alpha'
    assert header[3]["images"] == header[1]["images"]  # duplicate actual identity stays duplicate
    assert header[4]["images"] == [] and len(header[5]["images"]) == 2 and header[5]["text"] == ''
    assert header[6]["text"] == 'Explicit caption'
    assert nations["widgets"][0]["columnHeaders"] == ['Actual heading', '', '', '', '', '', 'Explicit caption']
    for before, after in zip(nations["widgets"], reordered["widgets"]):
        assert {k: v for k, v in before.items() if k != "headerCells"} == {k: v for k, v in after.items() if k != "headerCells"}
        assert after["headerCells"][1]["images"] == before["headerCells"][2]["images"]
        assert after["headerCells"][2]["images"] == before["headerCells"][1]["images"]
        assert after["headerCells"][2]["text"] == before["headerCells"][1]["text"]
    assert [cell["column"] for cell in gaps["widgets"][0]["headerCells"]] == [0, 1, 2, 4, 6]
    assert gaps["widgets"][0]["headerCells"][2]["images"] == []
    assert untranslated["widgets"][0]["headerCells"][2]["text"] == ''
    assert no_service["widgets"][0]["headerCells"][2]["text"] == ''
    assert bad_uv["widgets"][0]["headerCells"][3]["images"] == []
    assert len(texture["widgets"][0]["headerCells"][3]["images"]) == 1
print("PASS sanitizer table headers: actual source identities/captions, reordering, duplicate/missing/ambiguous sources, safe icons, sparse indexes, UTF-8/escaping, absence vs empty; body/actions/epoch guards preserved")
