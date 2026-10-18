"""Strict Git object <-> GitData conversion. Payloads are data, never programs."""
import base64
import datetime as dt
import gzip
import hashlib
import io
import json
import re


class Reject(ValueError):
    pass


def require(condition, reason):
    if not condition:
        raise Reject(reason)


def keys(value, expected):
    require(type(value) is dict and set(value) == set(expected), "Unexpected JSON fields")


def sha(value):
    require(type(value) is str and re.fullmatch(r"[0-9a-f]{40}", value), "Invalid object SHA")
    return value


def oid(kind, raw):
    require(kind in ("blob", "tree", "commit"), "Unsupported object type")
    return hashlib.sha1(kind.encode() + b" " + str(len(raw)).encode() + b"\0" + raw).hexdigest()


def unique(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "Duplicate JSON field")
        result[key] = value
    return result


def json_load(raw):
    return json.loads(raw, object_pairs_hook=unique,
                      parse_constant=lambda _: require(False, "Nonfinite JSON value"))


def json_bytes(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


def identity(raw):
    match = re.fullmatch(r"([^\n\x00<>]+) <([^\n\x00<>]+)> ([0-9]+) ([+-][0-9]{4})", raw)
    require(match is not None, "Unsupported commit identity")
    name, email, seconds, zone = match.groups()
    require(zone != "-0000", "Unrepresentable negative zero timezone")
    hours, minutes = int(zone[1:3]), int(zone[3:])
    require(hours < 24 and minutes < 60, "Invalid timezone")
    offset = (hours * 60 + minutes) * (1 if zone[0] == "+" else -1)
    stamp = dt.datetime.fromtimestamp(int(seconds), dt.timezone(dt.timedelta(minutes=offset)))
    return {"name": name, "email": email, "date": stamp.isoformat(timespec="seconds")}


def identity_raw(value):
    stamp = dt.datetime.fromisoformat(value["date"])
    require(stamp.utcoffset() is not None, "Missing timezone")
    seconds = int(stamp.timestamp())
    return f'{value["name"]} <{value["email"]}> {seconds} {stamp.strftime("%z")}'


def commit_raw(payload):
    lines = ["tree " + payload["tree"]]
    lines += ["parent " + parent for parent in payload["parents"]]
    lines += ["author " + identity_raw(payload["author"]),
              "committer " + identity_raw(payload["committer"])]
    return ("\n".join(lines) + "\n\n" + payload["message"]).encode("utf-8")


def commit_payload(raw):
    text = raw.decode("utf-8", errors="strict")
    require("\0" not in text and "\n\n" in text, "Invalid commit bytes")
    headers, message = text.split("\n\n", 1)
    lines = headers.split("\n")
    require(len(lines) >= 3 and lines[0].startswith("tree "), "Noncanonical commit headers")
    tree = sha(lines.pop(0)[5:])
    parents = []
    while lines and lines[0].startswith("parent "):
        parents.append(sha(lines.pop(0)[7:]))
    require(len(lines) == 2 and lines[0].startswith("author ")
            and lines[1].startswith("committer "), "Unsupported commit header/order")
    payload = {"tree": tree, "parents": parents, "author": identity(lines[0][7:]),
               "committer": identity(lines[1][10:]), "message": message}
    require(commit_raw(payload) == raw, "Commit bytes do not roundtrip")
    return payload


MODES = {"100644": "blob", "100755": "blob", "120000": "blob",
         "40000": "tree", "160000": "commit"}


def tree_raw(payload):
    result = bytearray()
    for entry in payload["tree"]:
        mode = "40000" if entry["mode"] == "040000" else entry["mode"]
        result += mode.encode() + b" " + entry["path"].encode("utf-8") + b"\0"
        result += bytes.fromhex(entry["sha"])
    return bytes(result)


def tree_payload(raw):
    entries, order, seen = [], [], set()
    position = 0
    while position < len(raw):
        end = raw.find(b"\0", position)
        require(end >= 0 and end + 21 <= len(raw), "Truncated tree")
        header = raw[position:end]
        require(b" " in header, "Invalid tree entry")
        mode, name = header.split(b" ", 1)
        mode = mode.decode("ascii")
        require(mode in MODES, "Unsupported tree mode")
        path = name.decode("utf-8", errors="strict")
        require(path and path not in (".", "..") and path.casefold() != ".git"
                and "/" not in path and "\\" not in path
                and not any(ord(c) < 32 or ord(c) == 127 for c in path), "Unsafe tree name")
        require(name not in seen, "Duplicate tree name")
        seen.add(name)
        order.append(name + (b"/" if mode == "40000" else b"\0"))
        entries.append({"path": path, "mode": "040000" if mode == "40000" else mode,
                        "type": MODES[mode], "sha": raw[end + 1:end + 21].hex()})
        position = end + 21
    require(order == sorted(order), "Noncanonical tree order")
    payload = {"tree": entries}
    require(tree_raw(payload) == raw, "Tree bytes do not roundtrip")
    return payload


def snapshot_trees(entries):
    """Reconstruct all directory bytes from a complete flat leaf list, without a filesystem."""
    require(type(entries) is list and entries, "Empty root trees are unsupported in this courier")
    root = {}
    for entry in entries:
        keys(entry, ("path", "mode", "type", "sha"))
        require(type(entry["path"]) is str, "Invalid snapshot path")
        sha(entry["sha"])
        require(entry["mode"] in ("100644", "100755", "120000", "160000")
                and MODES[entry["mode"]] == entry["type"], "Invalid snapshot leaf mode/type")
        parts = entry["path"].split("/")
        require(len(parts) <= 64, "Snapshot nesting limit")
        node = root
        for part in parts:
            # Reuse strict component validation and UTF-8 roundtrip.
            tree_payload(entry["mode"].encode() + b" " + part.encode("utf-8")
                         + b"\0" + bytes.fromhex(entry["sha"]))
        for part in parts[:-1]:
            require(part not in node or isinstance(node[part], dict), "Directory/file collision")
            node = node.setdefault(part, {})
        require(parts[-1] not in node, "Duplicate snapshot path")
        node[parts[-1]] = (entry["mode"], entry["type"], entry["sha"])
    objects = {}

    def build(node):
        entries = []
        for name, value in node.items():
            if isinstance(value, dict):
                mode, kind, key = "040000", "tree", build(value)
            else:
                mode, kind, key = value
            entries.append({"path": name, "mode": mode, "type": kind, "sha": key})
        entries.sort(key=lambda e: e["path"].encode("utf-8")
                     + (b"/" if e["mode"] == "040000" else b"\0"))
        raw = tree_raw({"tree": entries})
        key = oid("tree", raw)
        objects[key] = raw
        return key

    return build(root), objects


def api_payload(kind, raw):
    if kind == "blob":
        return {"encoding": "base64", "content": base64.b64encode(raw).decode("ascii")}
    if kind == "tree":
        return tree_payload(raw)
    if kind == "commit":
        return commit_payload(raw)
    raise Reject("Unsupported object type")


def dependencies(kind, payload):
    if kind == "commit":
        return [(payload["tree"], "tree")] + [(p, "commit") for p in payload["parents"]]
    if kind == "tree":
        # Gitlinks point into other repositories; never traverse/import their commits.
        return [(e["sha"], e["type"]) for e in payload["tree"] if e["mode"] != "160000"]
    return []


META = ("repository", "base", "base_tree", "tip", "tip_tree")


def validate_document(doc, contract):
    keys(doc, (*META, "version", "boundary", "objects", "snapshots"))
    require(type(doc["version"]) is int and doc["version"] == 1, "Unsupported transport version")
    for field in META:
        require(doc[field] == contract[field], "Pinned metadata mismatch: " + field)
    for field in META[1:]:
        sha(doc[field])
    require(type(doc["boundary"]) is dict and type(doc["objects"]) is list, "Invalid object graph")
    require(0 < len(doc["objects"]) <= 5000, "Invalid object count")
    available = dict(doc["boundary"])
    for key, kind in available.items():
        sha(key)
        require(kind in ("blob", "tree", "commit"), "Invalid boundary type")
    decoded, records, used_boundary, total = [], {}, set(), 0
    for record in doc["objects"]:
        keys(record, ("sha", "type", "size", "data"))
        key, kind = sha(record["sha"]), record["type"]
        require(key not in available, "Duplicate/imported base object")
        require(type(record["size"]) is int and 0 <= record["size"] <= 20_000_000,
                "Invalid object size")
        require(type(record["data"]) is str, "Invalid base64 field")
        raw = base64.b64decode(record["data"], validate=True)
        require(base64.b64encode(raw).decode("ascii") == record["data"], "Noncanonical base64")
        require(len(raw) == record["size"] and oid(kind, raw) == key, "Object size/hash mismatch")
        total += len(raw)
        require(total <= 50_000_000, "Object bytes limit exceeded")
        payload = api_payload(kind, raw)
        for dep, expected_kind in dependencies(kind, payload):
            require(available.get(dep) == expected_kind, "Missing, mistyped or unordered dependency")
            if dep in doc["boundary"]:
                used_boundary.add(dep)
        available[key] = kind
        records[key] = (kind, payload)
        decoded.append((key, kind, raw, payload))
    require(type(doc["snapshots"]) is dict, "Invalid snapshots")
    commit_roots = {p["tree"] for kind, p in records.values() if kind == "commit"}
    require(set(doc["snapshots"]) == commit_roots, "Missing/extra snapshot roots")
    for expected_root, entries in doc["snapshots"].items():
        actual_root, generated = snapshot_trees(entries)
        require(actual_root == expected_root, "Snapshot root hash mismatch")
        for key, raw in generated.items():
            if key in records:
                require(records[key][0] == "tree" and tree_raw(records[key][1]) == raw,
                        "Snapshot child tree mismatch")
        for e in entries:
            if e["type"] == "blob":
                require(available.get(e["sha"]) == "blob", "Unproven snapshot blob")
                if e["sha"] in doc["boundary"]:
                    used_boundary.add(e["sha"])
    require(used_boundary == set(doc["boundary"]), "Unused boundary objects")
    require(doc["tip"] in records and records[doc["tip"]][0] == "commit", "Missing tip commit")
    require(records[doc["tip"]][1]["tree"] == doc["tip_tree"], "Wrong tip tree")
    visited = set()
    pending = [doc["tip"]]
    while pending:
        key = pending.pop()
        if key in visited or key not in records:
            continue
        visited.add(key)
        pending.extend(k for k, _ in dependencies(*records[key]))
    require(visited == set(records), "Unreachable extra objects")
    return decoded


def load_transport(path, contract):
    with open(path, "rb") as stream:
        compressed = stream.read(contract["compressed_size"] + 1)
    require(len(compressed) == contract["compressed_size"], "Transport size mismatch")
    require(hashlib.sha256(compressed).hexdigest() == contract["transport_sha256"],
            "Transport digest mismatch")
    with gzip.GzipFile(fileobj=io.BytesIO(compressed)) as stream:
        raw = stream.read(contract["json_size"] + 1)
    require(len(raw) == contract["json_size"], "Transport expansion mismatch")
    require(hashlib.sha256(raw).hexdigest() == contract["json_sha256"], "JSON digest mismatch")
    doc = json_load(raw)
    decoded = validate_document(doc, contract)
    require(len(decoded) == contract["object_count"], "Pinned object count mismatch")
    return doc, decoded
