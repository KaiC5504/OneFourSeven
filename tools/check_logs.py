"""Checks JSONL log files (or folders of them, recursively) against core/data/log_schema.json.

Prints "ok N lines" per good file and every bad line with its reason; exits 1 on any bad
line, an empty file, a path that does not exist, or when there are no files to check.
"""
import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_SCHEMA = ROOT / "core" / "data" / "log_schema.json"
ENVELOPE = ("ts", "session", "build", "platform", "subsystem", "level", "event", "data")
LEVELS = ("debug", "info", "warn", "error")
# Seconds since 1970; 1e11 is the year 5138, so anything at or past it is a broken clock or writer.
TS_LIMIT = 1e11
# devlink.py keeps the lines it refused here, beside the session logs; they are not log lines.
REJECTED = "rejected.jsonl"


def _reject_constant(name):
    # Python accepts NaN and Infinity by default; no JSON writer we care about emits them.
    raise ValueError(f"{name} is not JSON")


def loads_strict(text):
    return json.loads(text, parse_constant=_reject_constant)


def load_schema(path=DEFAULT_SCHEMA):
    with open(path, encoding="utf-8") as handle:
        return loads_strict(handle.read())


def _type_name(value):
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "boolean"
    if isinstance(value, int):
        return "integer"
    if isinstance(value, float):
        return "number"
    if isinstance(value, str):
        return "string"
    if isinstance(value, list):
        return "array"
    return "object"


def _matches(value, expected):
    if expected == "any":
        return True
    if expected == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if expected == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if expected == "string":
        return isinstance(value, str)
    if expected == "boolean":
        return isinstance(value, bool)
    if expected == "object":
        return isinstance(value, dict)
    if expected == "array":
        return isinstance(value, list)
    return False


def validate_event(event, schema):
    if not isinstance(event, dict):
        return f"line is a JSON {_type_name(event)}, not an object"
    missing = [key for key in ENVELOPE if key not in event]
    if missing:
        return f"missing key {', '.join(missing)}"
    extra = sorted(key for key in event if key not in ENVELOPE)
    if extra:
        return f"unexpected key {', '.join(extra)}"
    if not _matches(event["ts"], "number"):
        return f"ts is {_type_name(event['ts'])}, not a number"
    if not 0 <= event["ts"] < TS_LIMIT:
        return f"ts {event['ts']} is outside [0, {TS_LIMIT:g})"
    for key in ("session", "build", "platform", "subsystem", "level", "event"):
        if not isinstance(event[key], str):
            return f"{key} is {_type_name(event[key])}, not a string"
    if not isinstance(event["data"], dict):
        return f"data is {_type_name(event['data'])}, not an object"
    level = event["level"]
    if level not in LEVELS:
        return f"level {level!r} is not one of {', '.join(LEVELS)}"
    name = event["event"]
    spec = schema.get("events", {}).get(name)
    if spec is None:
        return f"unknown event {name!r}"
    if event["subsystem"] != spec.get("subsystem"):
        return f"subsystem {event['subsystem']!r} does not match {name}'s {spec.get('subsystem')!r}"
    levels = spec.get("levels", [])
    if level not in levels:
        return f"level {level!r} not allowed for {name} (allowed: {', '.join(levels)})"
    data = event["data"]
    for field, rule in spec.get("fields", {}).items():
        if field not in data:
            if rule.get("required"):
                return f"data missing required field {field!r}"
            continue
        expected = rule.get("type", "any")
        if not _matches(data[field], expected):
            return f"data.{field} is {_type_name(data[field])}, not {expected}"
    return None


def validate_line(line, schema):
    """Returns None for a valid log line, else why it is not."""
    try:
        event = loads_strict(line)
    except (ValueError, RecursionError) as error:
        return f"not JSON: {error}"
    return validate_event(event, schema)


def collect(paths):
    """Returns (files to check, paths that do not exist)."""
    files = []
    missing = []
    for path in map(pathlib.Path, paths):
        if path.is_dir():
            files.extend(sorted(p for p in path.rglob("*.jsonl") if p.is_file() and p.name != REJECTED))
        elif path.is_file():
            files.append(path)
        else:
            missing.append(path)
    return files, missing


def check_file(path, schema):
    """Returns (line count, [(1-based line number, reason)])."""
    bad = []
    count = 0
    with open(path, "rb") as handle:
        raw_lines = handle.read().split(b"\n")
    if raw_lines and raw_lines[-1] == b"":
        raw_lines.pop()
    for number, raw in enumerate(raw_lines, start=1):
        count += 1
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError as error:
            bad.append((number, f"not UTF-8: {error}"))
            continue
        if not text.strip():
            bad.append((number, "empty line"))
            continue
        reason = validate_line(text, schema)
        if reason:
            bad.append((number, reason))
    return count, bad


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", help="JSONL files or folders")
    parser.add_argument("--schema", type=pathlib.Path, default=DEFAULT_SCHEMA)
    args = parser.parse_args(argv)
    schema = load_schema(args.schema)
    files, missing = collect(args.paths)
    for path in missing:
        print(f"{path}: no such file or folder")
    if not files:
        print("no log files to check")
        return 1
    failed = bool(missing)
    for path in files:
        try:
            count, bad = check_file(path, schema)
        except OSError as error:
            print(f"{path}: cannot read: {error}")
            failed = True
            continue
        if count == 0:
            failed = True
            print(f"{path}: empty file, no log lines")
        elif bad:
            failed = True
            for number, reason in bad:
                print(f"{path}:{number}: {reason}")
        else:
            print(f"{path}: ok {count} lines")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
