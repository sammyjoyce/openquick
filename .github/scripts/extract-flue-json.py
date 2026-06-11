#!/usr/bin/env python3
"""Extract and validate an Entropy Scout JSON object from flue output.

Flue/model output can contain unmatched diagnostic braces or prose quotes,
markdown-fenced JSON, GitHub log prefixes, ANSI sequences, plain JSON lines, or
stringified JSON. Prefer marked result blocks, then fall back to the whole log
while validating the Entropy Scout schema and normalizing minor model slips (for
example using only start_line). Keep this broad workflow-side fallback in sync
with the narrower agent-side recovery in .flue/lib/entropy-scout-result.ts; the
agent helper intentionally trusts only explicit result markers or final assistant
tail JSON.

Usage: python3 extract-flue-json.py <input-file> <output-file> [--profile entropy|thermo-nuclear]

The profile selects the result schema (top-level enum field + allowed values); it
defaults to `entropy` so existing Entropy Scout callers need no flag.
"""
import json
import re
import sys
from typing import Any, Iterable


ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
GITHUB_LOG_PREFIX_RE = re.compile(
    r"^[^\t\n]+\t[^\t\n]+\t\ufeff?\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d+Z ?",
    re.MULTILINE,
)
FENCE_LINE_RE = re.compile(r"^\s*```(?:json)?\s*$", re.MULTILINE)
# Each Flue review agent shares the findings shape but uses a different top-level
# enum field. The default profile preserves the original Entropy Scout CLI behavior
# so callers that pass no --profile keep working unchanged.
PROFILES = {
    "entropy": {
        "label": "Entropy Scout",
        "enum_field": "entropy_score",
        "enum_values": {"low", "medium", "high", "critical"},
    },
    "thermo-nuclear": {
        "label": "Thermo Nuclear Code Quality Review",
        "enum_field": "verdict",
        "enum_values": {"approve", "comment", "request_changes"},
    },
}
DEFAULT_PROFILE = "entropy"
REQUIRED_FINDING_FIELDS = {
    "category",
    "file",
    "severity",
    "explanation",
    "suggested_fix",
}


def normalize_log_text(text: str) -> str:
    """Strip wrappers that are useful for humans but not part of JSON."""
    text = ANSI_RE.sub("", text)
    text = GITHUB_LOG_PREFIX_RE.sub("", text)
    return FENCE_LINE_RE.sub("", text)


def normalize_result(candidate: dict[str, Any]) -> dict[str, Any]:
    findings = candidate.get("findings")
    if not isinstance(findings, list):
        return candidate

    for finding in findings:
        if not isinstance(finding, dict):
            continue
        if finding.get("line") is None and finding.get("start_line") is not None:
            finding["line"] = finding["start_line"]
    return candidate


def validate_result(candidate: Any, profile: dict[str, Any]) -> dict[str, Any]:
    if not isinstance(candidate, dict):
        raise ValueError("result is not a JSON object")

    candidate = normalize_result(candidate)

    enum_field = profile["enum_field"]
    if candidate.get(enum_field) not in profile["enum_values"]:
        raise ValueError(f"result.{enum_field} is invalid")

    summary = candidate.get("summary")
    if not isinstance(summary, str) or len(summary) == 0:
        raise ValueError("result.summary must be a non-empty string")

    findings = candidate.get("findings")
    if not isinstance(findings, list):
        raise ValueError("result.findings must be an array")

    for index, finding in enumerate(findings):
        if not isinstance(finding, dict):
            raise ValueError(f"result.findings[{index}] is not an object")
        missing = REQUIRED_FINDING_FIELDS - set(finding)
        if missing:
            raise ValueError(f"result.findings[{index}] is missing {sorted(missing)}")
        for field in REQUIRED_FINDING_FIELDS:
            value = finding[field]
            if not isinstance(value, str) or len(value) == 0:
                raise ValueError(f"result.findings[{index}].{field} must be a non-empty string")
        for field in ("line", "start_line"):
            if (
                field in finding
                and finding[field] is not None
                and not isinstance(finding[field], (str, int))
            ):
                raise ValueError(f"result.findings[{index}].{field} must be a string or number")
        if (
            "suggested_change" in finding
            and finding["suggested_change"] is not None
            and (
                not isinstance(finding["suggested_change"], str)
                or len(finding["suggested_change"]) == 0
            )
        ):
            if finding["suggested_change"] == "":
                finding.pop("suggested_change", None)
            else:
                raise ValueError(
                    f"result.findings[{index}].suggested_change must be a non-empty string"
                )

    return candidate


def decode_json_candidate(candidate: Any) -> Any:
    if isinstance(candidate, str):
        return json.loads(candidate)
    return candidate


def iter_json_objects(text: str):
    """Yield JSON objects found by scanning from every opening brace.

    A single global brace depth fails when flue logs an unmatched diagnostic
    "{" before printing the actual JSON result. Starting a bounded scan at each
    brace is more tolerant and the logs are small enough that the extra work is
    negligible.
    """
    starts = [index for index, char in enumerate(text) if char == "{"]
    for start in starts:
        depth = 0
        in_string = False
        escape = False
        for index in range(start, len(text)):
            char = text[index]
            if in_string:
                if escape:
                    escape = False
                elif char == "\\":
                    escape = True
                elif char == '"':
                    in_string = False
                continue

            if char == '"':
                in_string = True
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    try:
                        yield decode_json_candidate(json.loads(text[start : index + 1]))
                    # Candidate scanning is intentionally best-effort: malformed
                    # JSON fragments are skipped so later valid flue results can
                    # still be discovered, while final absence remains fatal.
                    except json.JSONDecodeError:
                        pass
                    break
                if depth < 0:
                    break


def iter_raw_decode_objects(text: str):
    decoder = json.JSONDecoder()
    for start in reversed([index for index, char in enumerate(text) if char == "{"]):
        try:
            parsed, _ = decoder.raw_decode(text[start:])
            yield decode_json_candidate(parsed)
        # Candidate scanning is intentionally best-effort: malformed JSON
        # fragments are skipped so earlier valid candidates can still be checked.
        except json.JSONDecodeError:
            continue


def iter_json_lines(text: str):
    for line in reversed(text.splitlines()):
        stripped = line.strip()
        if not stripped:
            continue
        try:
            yield decode_json_candidate(json.loads(stripped))
        # Candidate scanning is intentionally best-effort: malformed JSON lines
        # are skipped so later valid flue results can still be discovered, while
        # final absence of a valid result remains fatal.
        except json.JSONDecodeError:
            continue


def iter_marker_sections(text: str):
    start_marker = "---RESULT_START---"
    end_marker = "---RESULT_END---"
    offset = 0
    while True:
        start = text.find(start_marker, offset)
        if start == -1:
            return
        start += len(start_marker)
        end = text.find(end_marker, start)
        if end == -1:
            return
        yield text[start:end]
        offset = end + len(end_marker)


def valid_result_or_none(candidate: Any, profile: dict[str, Any]) -> dict[str, Any] | None:
    try:
        return validate_result(candidate, profile)
    except ValueError:
        return None


def first_valid(candidates: Iterable[Any], profile: dict[str, Any]) -> dict[str, Any] | None:
    for candidate in candidates:
        result = valid_result_or_none(candidate, profile)
        if result is not None:
            return result
    return None


def last_valid(candidates: Iterable[Any], profile: dict[str, Any]) -> dict[str, Any] | None:
    result = None
    for candidate in candidates:
        result = valid_result_or_none(candidate, profile) or result
    return result


def extract_result(text: str, profile: dict[str, Any] | None = None) -> dict[str, Any] | None:
    profile = profile or PROFILES[DEFAULT_PROFILE]
    cleaned = normalize_log_text(text)
    sections = list(iter_marker_sections(cleaned))

    for section in reversed(sections):
        result = first_valid(iter_raw_decode_objects(section), profile)
        if result is not None:
            return result
        result = first_valid(iter_json_lines(section), profile)
        if result is not None:
            return result
        result = last_valid(iter_json_objects(section), profile)
        if result is not None:
            return result

    result = first_valid(iter_raw_decode_objects(cleaned), profile)
    if result is not None:
        return result
    result = first_valid(iter_json_lines(cleaned), profile)
    if result is not None:
        return result
    return last_valid(iter_json_objects(cleaned), profile)


def write_result(output_file: str, result: dict[str, Any], profile: dict[str, Any]) -> None:
    try:
        with open(output_file, "w") as f:
            json.dump(result, f)
            f.write("\n")
    except OSError as error:
        print(
            f"Failed to write {profile['label']} JSON result to {output_file}: {error}",
            file=sys.stderr,
        )
        sys.exit(1)


def parse_args(argv: list[str]) -> tuple[str, str, dict[str, Any]]:
    profile_name = DEFAULT_PROFILE
    positional: list[str] = []
    index = 0
    while index < len(argv):
        arg = argv[index]
        if arg == "--profile":
            if index + 1 >= len(argv):
                print("--profile requires a value", file=sys.stderr)
                sys.exit(2)
            profile_name = argv[index + 1]
            index += 2
            continue
        if arg.startswith("--profile="):
            profile_name = arg.split("=", 1)[1]
            index += 1
            continue
        positional.append(arg)
        index += 1

    if len(positional) != 2:
        print(
            f"Usage: extract-flue-json.py <input-file> <output-file> [--profile {'|'.join(PROFILES)}]",
            file=sys.stderr,
        )
        sys.exit(2)
    if profile_name not in PROFILES:
        print(
            f"Unknown profile {profile_name!r}; expected one of {sorted(PROFILES)}",
            file=sys.stderr,
        )
        sys.exit(2)

    return positional[0], positional[1], PROFILES[profile_name]


def main():
    input_file, output_file, profile = parse_args(sys.argv[1:])

    with open(input_file) as f:
        text = f.read()

    result = extract_result(text, profile)
    if result is None:
        print(
            f"No valid {profile['label']} JSON result found in flue output",
            file=sys.stderr,
        )
        sys.exit(1)

    write_result(output_file, result, profile)
    print(f"Extracted {profile['label']} JSON ({len(json.dumps(result))} bytes)", file=sys.stderr)


if __name__ == "__main__":
    main()
