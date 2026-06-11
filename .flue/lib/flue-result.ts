import * as v from "valibot";

// Schema-agnostic recovery for Flue review agents. Each agent (entropy-scout,
// thermo-nuclear-code-quality-review, ...) supplies its own result schema; this
// module owns the single, well-tested implementation of pulling a schema-valid
// JSON object back out of a noisy Flue transcript. It is intentionally narrower
// than the workflow-side Python extractor in `.github/scripts/extract-flue-json.py`:
// it only trusts explicit result markers or final assistant-tail JSON so quoted
// PR content cannot be mistaken for the agent result.
// eslint-disable-next-line no-control-regex -- ANSI escape (ESC) is intentional: strip terminal color codes from Flue output
const ANSI_RE = /\x1b\[[0-?]*[ -/]*[@-~]/g;
const GITHUB_LOG_PREFIX_RE =
  /^[^\t\n]+\t[^\t\n]+\t\ufeff?\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d+Z ?/gm;
const FENCE_LINE_RE = /^\s*```(?:json)?\s*$/gm;
const START_MARKER = "---RESULT_START---";
const END_MARKER = "---RESULT_END---";

const trimmedStringSchema = v.pipe(v.string(), v.trim());
export const nonEmptyStringSchema = v.pipe(trimmedStringSchema, v.nonEmpty());
export const optionalStringSchema = v.pipe(
  trimmedStringSchema,
  v.transform((value) => (value.length === 0 ? null : value)),
);
export const diffLineSchema = v.union([v.number(), optionalStringSchema]);

type RawOutputFromErrorResult =
  | { rawOutput: string; reason: null }
  | { rawOutput: null; reason: string };

type JsonObjectSlice = { value: string; end: number };

const NO_JSON_OBJECT = Symbol("NO_JSON_OBJECT");

function isRecord(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function normalizeLogText(text: string, options?: { preserveFences?: boolean }): string {
  const cleaned = text.replace(ANSI_RE, "").replace(GITHUB_LOG_PREFIX_RE, "");
  return options?.preserveFences === true ? cleaned : cleaned.replace(FENCE_LINE_RE, "");
}

function hasOnlyTrustedTrailingLogLines(text: string): boolean {
  const trailingLines = text.trim().split(/\r?\n/).filter(Boolean);
  return trailingLines.every((line) => /^\s*\[flue(?::|\])/.test(line));
}

function stripTrustedTrailingLogLines(text: string): string {
  const lines = text.split(/\r?\n/);
  let end = lines.length;
  while (end > 0) {
    const line = lines[end - 1]?.trim() ?? "";
    if (line.length === 0 || /^\[flue(?::|\])/.test(line)) {
      end -= 1;
      continue;
    }
    break;
  }
  return lines.slice(0, end).join("\n");
}

const SAFE_ERROR_DETAIL_KEYS = ["code", "kind", "name", "status", "statusCode", "type"];

function describeValueType(value: unknown): string {
  if (value === null) {
    return "null";
  }
  if (Array.isArray(value)) {
    return "array";
  }
  return typeof value;
}

function describeSafeErrorDetails(error: Record<string, unknown>): string {
  const details = SAFE_ERROR_DETAIL_KEYS.flatMap((key) => {
    if (!(key in error)) {
      return [];
    }
    const value = error[key];
    if (typeof value !== "string" && typeof value !== "number" && typeof value !== "boolean") {
      return [];
    }
    return [`${key}=${String(value).slice(0, 80)}`];
  });
  return details.length === 0 ? "" : `; ${details.join(", ")}`;
}

// Shared finding normalizer: models occasionally emit only `start_line`, so mirror
// it onto `line` before validation. Both review schemas share the findings shape.
export function normalizeFindingsCandidate(candidate: unknown): unknown {
  if (!isRecord(candidate)) {
    return candidate;
  }

  const findings = candidate["findings"];
  if (!Array.isArray(findings)) {
    return candidate;
  }

  return {
    ...candidate,
    findings: findings.map((finding) => {
      if (!isRecord(finding)) {
        return finding;
      }
      const normalized: Record<string, unknown> =
        finding["line"] == null && finding["start_line"] != null
          ? { ...finding, line: finding["start_line"] }
          : { ...finding };
      if (normalized["suggested_change"] === "") {
        Reflect.deleteProperty(normalized, "suggested_change");
      }
      return normalized;
    }),
  };
}

function decodeJsonCandidate(candidate: unknown): unknown {
  if (typeof candidate !== "string") {
    return candidate;
  }

  try {
    return JSON.parse(candidate);
  } catch (error: unknown) {
    if (!(error instanceof SyntaxError)) {
      throw error;
    }
    return candidate;
  }
}

function getFinalTrustedMarkerSection(text: string): string | null {
  let trustedSection: string | null = null;
  let offset = 0;
  while (offset < text.length) {
    const start = text.indexOf(START_MARKER, offset);
    if (start === -1) {
      break;
    }

    const contentStart = start + START_MARKER.length;
    const end = text.indexOf(END_MARKER, contentStart);
    if (end === -1) {
      break;
    }

    const sectionEnd = end + END_MARKER.length;
    if (hasOnlyTrustedTrailingLogLines(text.slice(sectionEnd))) {
      trustedSection = text.slice(contentStart, end).trim();
    }
    offset = sectionEnd;
  }
  return trustedSection;
}

function* iterJsonLineCandidates(text: string): Generator<unknown> {
  const lines = text.split(/\r?\n/).reverse();
  for (const line of lines) {
    const stripped = line.trim();
    if (stripped.length === 0) {
      continue;
    }

    try {
      yield decodeJsonCandidate(JSON.parse(stripped));
    } catch (error: unknown) {
      if (!(error instanceof SyntaxError)) {
        throw error;
      }
      // Best-effort extraction: malformed diagnostic lines should not prevent a
      // later schema-valid result from being discovered.
    }
  }
}

function* iterJsonObjectSlices(text: string): Generator<JsonObjectSlice> {
  // Flue transcripts are small; bounded brace scans are acceptable here and
  // make extraction resilient to diagnostic text before the final JSON object.
  const starts: number[] = [];
  for (let index = 0; index < text.length; index += 1) {
    if (text[index] === "{") {
      starts.push(index);
    }
  }

  for (const start of starts) {
    let depth = 0;
    let inString = false;
    let escape = false;

    for (let index = start; index < text.length; index += 1) {
      const char = text[index];

      if (inString) {
        if (escape) {
          escape = false;
        } else if (char === "\\") {
          escape = true;
        } else if (char === '"') {
          inString = false;
        }
        continue;
      }

      if (char === '"') {
        inString = true;
      } else if (char === "{") {
        depth += 1;
      } else if (char === "}") {
        depth -= 1;
        if (depth === 0) {
          yield { value: text.slice(start, index + 1), end: index + 1 };
          break;
        }
        if (depth < 0) {
          break;
        }
      }
    }
  }
}

function getFinalJsonObjectCandidate(text: string): unknown | typeof NO_JSON_OBJECT {
  let finalSlice: JsonObjectSlice | null = null;
  for (const slice of iterJsonObjectSlices(text)) {
    if (finalSlice === null || slice.end > finalSlice.end) {
      finalSlice = slice;
    }
  }

  if (finalSlice === null) {
    return NO_JSON_OBJECT;
  }

  if (text.slice(finalSlice.end).trim().length > 0) {
    return null;
  }

  try {
    return decodeJsonCandidate(JSON.parse(finalSlice.value));
  } catch (error: unknown) {
    if (!(error instanceof SyntaxError)) {
      throw error;
    }
    return null;
  }
}

function* iterTailResultSections(text: string): Generator<string> {
  const unwrapped = normalizeLogText(text, { preserveFences: true });
  const fencedMatches = Array.from(unwrapped.matchAll(/```(?:json)?\s*\n([\s\S]*?)\n```/g));
  const lastFence = fencedMatches.at(-1);
  if (lastFence?.index !== undefined) {
    const fenceEnd = lastFence.index + lastFence[0].length;
    if (hasOnlyTrustedTrailingLogLines(unwrapped.slice(fenceEnd))) {
      const fencedContent = lastFence[1]?.trim();
      if (fencedContent) {
        yield fencedContent;
      }
    }
  }

  const tailText = stripTrustedTrailingLogLines(unwrapped);
  const lines = tailText.split(/\r?\n/);
  const lastNonEmptyLine = [...lines]
    .reverse()
    .find((line) => line.trim().length > 0)
    ?.trim();
  if (lastNonEmptyLine) {
    yield lastNonEmptyLine;
  }

  const objectStartIndents = lines.map((line) => line.match(/^(\s*)\{/s)?.[1]?.length ?? null);
  const minimumObjectIndent = Math.min(
    ...objectStartIndents.filter((indent): indent is number => indent !== null),
  );
  if (!Number.isFinite(minimumObjectIndent)) {
    return;
  }

  for (let index = lines.length - 1; index >= 0; index -= 1) {
    if (objectStartIndents[index] === minimumObjectIndent) {
      const candidate = lines.slice(index).join("\n").trim();
      if (candidate.endsWith("}")) {
        yield candidate;
      }
      return;
    }
  }
}

// Build a transcript extractor bound to a specific result schema. `normalizeCandidate`
// runs before validation (e.g. to mirror `start_line` onto `line`).
export function createTranscriptExtractor<TSchema extends v.GenericSchema>(
  schema: TSchema,
  normalizeCandidate: (candidate: unknown) => unknown = (candidate) => candidate,
): (transcript: string) => v.InferOutput<TSchema> | null {
  type Result = v.InferOutput<TSchema>;

  const parseCandidate = (candidate: unknown): Result | null => {
    const parsed = v.safeParse(schema, normalizeCandidate(decodeJsonCandidate(candidate)));
    return parsed.success ? parsed.output : null;
  };

  const firstValid = (candidates: Iterable<unknown>): Result | null => {
    for (const candidate of candidates) {
      const result = parseCandidate(candidate);
      if (result !== null) {
        return result;
      }
    }
    return null;
  };

  const extractFromResultSection = (section: string): Result | null => {
    const cleaned = normalizeLogText(section);

    const finalObjectCandidate = getFinalJsonObjectCandidate(cleaned);
    if (finalObjectCandidate !== NO_JSON_OBJECT) {
      return parseCandidate(finalObjectCandidate);
    }

    return firstValid(iterJsonLineCandidates(cleaned));
  };

  return function extractFromTranscript(transcript: string): Result | null {
    // Agent-side recovery is intentionally stricter than the workflow log parser:
    // explicit result markers are tried first, then the assistant's final tail
    // JSON. Invalid marker-like text can appear inside tool output, so it must not
    // prevent the actual final assistant result from being recovered.
    const cleaned = normalizeLogText(transcript);
    const finalMarkedSection = getFinalTrustedMarkerSection(cleaned);
    if (finalMarkedSection !== null) {
      return extractFromResultSection(finalMarkedSection);
    }

    for (const section of iterTailResultSections(transcript)) {
      const tailResult = extractFromResultSection(section);
      if (tailResult !== null) {
        return tailResult;
      }
    }

    return null;
  };
}

export function getRawOutputFromError(error: unknown): RawOutputFromErrorResult {
  if (!isRecord(error)) {
    return { rawOutput: null, reason: "error was not an object" };
  }

  const details = describeSafeErrorDetails(error);
  if (!("rawOutput" in error)) {
    return { rawOutput: null, reason: `error did not expose rawOutput${details}` };
  }

  const rawOutput = error["rawOutput"];
  if (typeof rawOutput !== "string") {
    return { rawOutput: null, reason: `rawOutput was ${describeValueType(rawOutput)}${details}` };
  }
  if (rawOutput.trim().length === 0) {
    return { rawOutput: null, reason: `rawOutput was empty${details}` };
  }
  return { rawOutput, reason: null };
}

// Only Flue `ResultExtractionError` (the agent ran, but Flue could not extract or
// validate the final schema block) is advisory; everything else is infrastructure.
export function isSchemaExtractionFailure(error: unknown, seen = new Set<unknown>()): boolean {
  if (seen.has(error)) {
    return false;
  }
  seen.add(error);

  if (!(error instanceof Error)) {
    return false;
  }

  if (error.name === "ResultExtractionError") {
    return true;
  }

  if (error instanceof AggregateError) {
    return error.errors.some((nestedError) => isSchemaExtractionFailure(nestedError, seen));
  }

  return error.cause !== undefined && isSchemaExtractionFailure(error.cause, seen);
}
