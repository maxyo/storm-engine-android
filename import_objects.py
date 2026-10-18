"""Exact GitData courier. No ref, workflow, release, download or shell capability."""
import argparse
import base64
import hashlib
import os
from pathlib import Path
import ssl
import time
import urllib.error
import urllib.request
from codec import (Reject, api_payload, commit_raw, json_bytes, json_load, load_transport,
                   oid, require, sha, snapshot_trees)

HERE = Path(__file__).resolve().parent
REPOSITORIES = {"engine": "maxyo/storm-engine-android", "app": "maxyo/seawolves-3-android"}
POST_INTERVAL = 8.0  # at most 450/hour, below documented general 500 content writes/hour
MAX_RESPONSE = 8_000_000
MAX_REQUEST = 6_000_000


def contracts():
    result = json_load((HERE / "contracts.json").read_bytes())
    require(set(result) == set(REPOSITORIES), "Unexpected contract jobs")
    for job, repository in REPOSITORIES.items():
        require(result[job]["repository"] == repository, "Wrong pinned repository")
    return result


def plan(doc, decoded):
    """Full root snapshots avoid base_tree deletion/rename/pruning ambiguity."""
    records = {key: (kind, raw, payload) for key, kind, raw, payload in decoded}
    known = set(doc["boundary"])
    known.add(doc["base_tree"])
    steps = []
    for key, kind, raw, payload in decoded:
        if kind == "blob":
            try:
                raw.decode("utf-8", errors="strict")
            except UnicodeDecodeError:
                steps.append(("blob", key, payload))
                known.add(key)
    for key, kind, raw, payload in decoded:
        if kind != "commit":
            continue
        root = payload["tree"]
        if root not in known:
            flat = []
            for entry in doc["snapshots"][root]:
                outgoing = dict(entry)
                object_id = entry["sha"]
                if entry["type"] == "blob" and object_id not in known:
                    require(object_id in records and records[object_id][0] == "blob",
                            "Missing inline blob")
                    content = records[object_id][1].decode("utf-8", errors="strict")
                    require(oid("blob", content.encode("utf-8")) == object_id, "Inline bytes changed")
                    outgoing.pop("sha")
                    outgoing["content"] = content
                flat.append(outgoing)
            tree_payload = {"tree": flat}  # deliberately NO base_tree
            steps.append(("tree", root, tree_payload))
            actual_root, generated = snapshot_trees(doc["snapshots"][root])
            require(actual_root == root, "Plan root mismatch")
            known.update(generated)
            known.update(e["sha"] for e in doc["snapshots"][root] if e["type"] == "blob")
        require(commit_raw(payload) == raw and oid("commit", raw) == key, "Commit plan mismatch")
        steps.append(("commit", key, payload))
        known.add(key)
    for kind, key, payload in steps:
        require(len(json_bytes(payload)) <= MAX_REQUEST, "GitData request exceeds courier size limit")
    require(steps and steps[-1][0:2] == ("commit", doc["tip"]), "Tip is not final operation")
    return steps


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class GitData:
    def __init__(self, repository, token, opener=None, sleep=time.sleep, clock=time.monotonic):
        require(repository in REPOSITORIES.values(), "Repository not allowed")
        require(type(token) is str and token and "\n" not in token and "\r" not in token,
                "Missing/invalid ordinary job token")
        self.prefix = "https://api.github.com/repos/" + repository + "/git/"
        self.token = token
        self.opener = opener or urllib.request.build_opener(
            urllib.request.ProxyHandler({}), NoRedirect(),
            urllib.request.HTTPSHandler(context=ssl.create_default_context()))
        self.sleep, self.clock, self.last_post = sleep, clock, None

    def request(self, method, kind, key=None, payload=None):
        require(kind in ("blob", "tree", "commit"), "Forbidden endpoint")
        require((method == "POST" and key is None and payload is not None)
                or (method == "GET" and kind == "commit" and payload is None), "Forbidden operation")
        endpoint = kind + "s"
        if method == "GET":
            endpoint += "/" + sha(key)
        data = None if payload is None else json_bytes(payload)
        require(data is None or len(data) <= MAX_REQUEST, "Request limit")
        if method == "POST":
            if self.last_post is not None:
                self.sleep(max(0.0, POST_INTERVAL - (self.clock() - self.last_post)))
            self.last_post = self.clock()
        request = urllib.request.Request(self.prefix + endpoint, data=data, method=method,
            headers={"Authorization": "Bearer " + self.token, "Accept": "application/vnd.github+json",
                     "Content-Type": "application/json", "X-GitHub-Api-Version": "2022-11-28",
                     "User-Agent": "approved-exact-source-object-courier-1158"})
        try:
            with self.opener.open(request, timeout=60) as response:
                require(response.status == (201 if method == "POST" else 200), "Unexpected HTTP status")
                require(response.geturl() == request.full_url, "Response URL changed")
                body = response.read(MAX_RESPONSE + 1)
                require(len(body) <= MAX_RESPONSE, "Response size limit")
        except urllib.error.HTTPError as error:
            # Never retry a denial/rate limit, dump a response body, or forward credentials.
            info = []
            for name in ("Retry-After", "X-RateLimit-Remaining", "X-RateLimit-Reset"):
                value = error.headers.get(name, "") if error.headers else ""
                if value.isascii() and value.isdigit():
                    info.append(name + "=" + value)
            error.close()
            raise Reject("GitData HTTP " + str(error.code) + "; stop without retry; " + ",".join(info)) from None
        except (urllib.error.URLError, TimeoutError, OSError):
            raise Reject("GitData network failure; stop without retry (write outcome may be unknown)") from None
        result = json_load(body)
        require(type(result) is dict, "Invalid API response")
        return result


def verify_commit_response(result, expected, tree, parents=None):
    require(result.get("sha") == expected, "API commit SHA mismatch")
    require(type(result.get("tree")) is dict and result["tree"].get("sha") == tree,
            "API commit tree mismatch")
    if parents is not None:
        require(type(result.get("parents")) is list
                and [p.get("sha") for p in result["parents"]] == parents, "API parent order mismatch")


def execute(contract, doc, decoded, api, log=print):
    # Planning and all payload/hash/graph validation complete before the first request.
    steps = plan(doc, decoded)
    base = api.request("GET", "commit", key=contract["base"])
    verify_commit_response(base, contract["base"], contract["base_tree"])
    receipts = []
    for index, (kind, key, payload) in enumerate(steps, 1):
        result = api.request("POST", kind, payload=payload)
        require(result.get("sha") == key, "API " + kind + " SHA mismatch; stop without refs")
        if kind == "commit":
            verify_commit_response(result, key, payload["tree"], payload["parents"])
        receipts.append({"type": kind, "sha": key})
        log(f"verified {index}/{len(steps)} {kind} {key}")
    tip = api.request("GET", "commit", key=contract["tip"])
    tip_payload = next(p for k, t, r, p in decoded if k == contract["tip"])
    verify_commit_response(tip, contract["tip"], contract["tip_tree"], tip_payload["parents"])
    return {"repository": contract["repository"], "base": contract["base"],
            "tip": contract["tip"], "tip_tree": contract["tip_tree"],
            "transport_sha256": contract["transport_sha256"], "verified_posts": receipts,
            "refs_changed": False, "result": "exact-object-import-complete"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("job", choices=tuple(REPOSITORIES))
    parser.add_argument("transport", type=Path)
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()
    contract = contracts()[args.job]
    doc, decoded = load_transport(args.transport, contract)
    steps = plan(doc, decoded)
    summary = {"job": args.job, "post_count": len(steps), "get_count": 2,
               "max_post_bytes": max(len(json_bytes(p)) for _, _, p in steps),
               "total_post_bytes": sum(len(json_bytes(p)) for _, _, p in steps),
               "minimum_post_spacing_seconds": POST_INTERVAL,
               "transport_sha256": contract["transport_sha256"]}
    print(json_bytes(summary).decode(), flush=True)
    if args.validate_only:
        return
    require(os.environ.get("GITHUB_ACTIONS") == "true"
            and os.environ.get("GITHUB_REPOSITORY") == contract["repository"], "Wrong CI context")
    api = GitData(contract["repository"], os.environ.get("GITHUB_TOKEN"))
    receipt = execute(contract, doc, decoded, api, log=lambda value: print(value, flush=True))
    # stdout only: root chooses an artifact location, never a payload-specified path.
    print(json_bytes(receipt).decode(), flush=True)


if __name__ == "__main__":
    try:
        main()
    except Reject as error:
        raise SystemExit("STOP: " + str(error)) from None
    except Exception as error:
        # No exception dumps containing token/request/body/source data.
        raise SystemExit("STOP: invalid data or I/O failure (" + type(error).__name__ + ")") from None
