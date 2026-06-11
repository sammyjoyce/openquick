/* oxlint-disable */
// Shared, agent-agnostic helpers for Flue review posting scripts (entropy-scout,
// thermo-nuclear-code-quality-review, ...). Each agent owns its own validation enum,
// report header, and posting flow; everything below — diff-line math, GitHub Suggested
// Change building, and the findings markdown — is identical across agents and lives here.

export function toText(value) {
  return value == null ? "" : String(value);
}

export function tableText(value) {
  return toText(value).replace(/\r?\n/g, "<br>").replace(/\|/g, "\\|").replace(/`/g, "&#96;");
}

export function errorMessage(error) {
  return error instanceof Error ? error.message : toText(error);
}

export function isNonEmptyString(value) {
  return typeof value === "string" && value.length > 0;
}

export function parsePositiveInteger(value) {
  if (typeof value === "number" && Number.isInteger(value) && value > 0) {
    return value;
  }

  if (typeof value === "string") {
    const trimmed = value.trim();
    if (/^\d+$/.test(trimmed)) {
      return Number(trimmed);
    }
  }

  return undefined;
}

const REQUIRED_FINDING_FIELDS = ["category", "file", "severity", "explanation", "suggested_fix"];

// Validate a Flue review result. `enumField`/`enumValues` are the agent-specific
// top-level enum (entropy: entropy_score; thermo-nuclear: verdict).
export function validateReviewResult(candidate, { enumField, enumValues }) {
  if (candidate == null || typeof candidate !== "object" || Array.isArray(candidate)) {
    throw new Error("result is not a JSON object");
  }

  const enumValue = candidate[enumField];
  if (!enumValues.includes(enumValue)) {
    throw new Error(`result.${enumField} is invalid`);
  }

  const summary = candidate.summary;
  if (!isNonEmptyString(summary)) {
    throw new Error("result.summary must be a non-empty string");
  }

  const findings = candidate.findings;
  if (!Array.isArray(findings)) {
    throw new Error("result.findings must be an array");
  }

  const normalizedFindings = findings.map((finding) => {
    if (
      finding != null &&
      typeof finding === "object" &&
      !Array.isArray(finding) &&
      finding.suggested_change === ""
    ) {
      const normalized = { ...finding };
      delete normalized.suggested_change;
      return normalized;
    }
    return finding;
  });

  for (const [index, finding] of normalizedFindings.entries()) {
    if (finding == null || typeof finding !== "object" || Array.isArray(finding)) {
      throw new Error(`result.findings[${index}] is not an object`);
    }

    for (const field of REQUIRED_FINDING_FIELDS) {
      if (!isNonEmptyString(finding[field])) {
        throw new Error(`result.findings[${index}].${field} must be a non-empty string`);
      }
    }

    for (const field of ["line", "start_line"]) {
      if (
        field in finding &&
        finding[field] != null &&
        typeof finding[field] !== "string" &&
        typeof finding[field] !== "number"
      ) {
        throw new Error(`result.findings[${index}].${field} must be a string or number`);
      }
    }

    if (
      "suggested_change" in finding &&
      finding.suggested_change != null &&
      !isNonEmptyString(finding.suggested_change)
    ) {
      throw new Error(`result.findings[${index}].suggested_change must be a non-empty string`);
    }
  }

  return { enumValue, findings: normalizedFindings, summary };
}

export function stripSuggestionFence(value) {
  const text = toText(value).trimEnd();
  const fenced = text.trim().match(/^```(?:suggestion|[A-Za-z0-9_-]+)?\n([\s\S]*?)\n```$/);
  return fenced ? fenced[1].trimEnd() : text;
}

export function collectRightSideLines(patch) {
  const lines = new Set();
  if (!patch) {
    return lines;
  }

  let rightLine;
  for (const rawLine of patch.split("\n")) {
    const hunk = rawLine.match(/^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@/);
    if (hunk) {
      rightLine = Number(hunk[1]);
      continue;
    }

    if (rightLine === undefined || rawLine.startsWith("\\")) {
      continue;
    }

    const prefix = rawLine[0];
    if (prefix === " " || prefix === "+") {
      lines.add(rightLine);
      rightLine += 1;
    }
  }

  return lines;
}

export function hasDiffRange(diffLinesByFile, path, startLine, line) {
  const diffLines = diffLinesByFile.get(path);
  if (!diffLines || diffLines.size === 0) {
    return false;
  }

  for (let current = startLine; current <= line; current += 1) {
    if (!diffLines.has(current)) {
      return false;
    }
  }

  return true;
}

export function buildSuggestionComments({ findings, diffLinesByFile, skippedSuggestions }) {
  const suggestionComments = [];

  for (const [index, finding] of findings.entries()) {
    const file = toText(finding.file).trim();
    if (!file || finding.suggested_change == null) {
      continue;
    }
    const suggestedChange = stripSuggestionFence(finding.suggested_change);

    const line = parsePositiveInteger(finding.line);
    if (!line) {
      skippedSuggestions.push(`${file || `finding ${index + 1}`}: missing numeric diff line`);
      continue;
    }

    const startLine = parsePositiveInteger(finding.start_line) ?? line;
    const location = startLine === line ? `${file}:${line}` : `${file}:${startLine}-${line}`;

    if (startLine > line) {
      skippedSuggestions.push(`${location}: start_line must be <= line`);
      continue;
    }

    if (suggestedChange.includes("```")) {
      skippedSuggestions.push(`${location}: suggested_change contains a markdown fence`);
      continue;
    }

    if (!hasDiffRange(diffLinesByFile, file, startLine, line)) {
      skippedSuggestions.push(`${location}: line range is not present in the PR diff`);
      continue;
    }

    const category = toText(finding.category) || "review";
    const severity = toText(finding.severity);
    const comment = {
      path: file,
      line,
      side: "RIGHT",
      body: [
        `**${category}${severity ? ` (${severity})` : ""}**`,
        "",
        toText(finding.explanation),
        "",
        `Suggested fix: ${toText(finding.suggested_fix)}`,
        "",
        "```suggestion",
        suggestedChange,
        "```",
      ].join("\n"),
    };

    if (startLine !== line) {
      comment.start_line = startLine;
      comment.start_side = "RIGHT";
    }

    suggestionComments.push(comment);
  }

  return suggestionComments;
}

// Render the shared part of a review body: status line, summary, findings table,
// suggested-fix details, and suggestion footers. `heading`/`statusLine`/`emptyLine`
// are agent-specific; `marker` is an optional hidden HTML comment used to supersede
// prior runs (omit it to preserve a script that does not supersede).
export function buildReviewBody({
  marker,
  heading,
  statusLine,
  summary,
  findings,
  postedSuggestionCount,
  skippedSuggestions,
  emptyLine,
}) {
  let body = marker ? `${marker}\n` : "";
  body += `## ${heading}\n\n`;
  body += `${statusLine}\n\n`;
  body += `${summary}\n\n`;

  if (findings.length > 0) {
    body += `### Findings (${findings.length})\n\n`;
    body += `| Category | File | Severity | Explanation |\n`;
    body += `|----------|------|----------|-------------|\n`;
    for (const finding of findings) {
      const location = finding.line ? `${finding.file}:${finding.line}` : finding.file;
      body += `| \`${tableText(finding.category)}\` | \`${tableText(location)}\` | \`${tableText(finding.severity)}\` | ${tableText(finding.explanation)} |\n`;
    }

    body += `\n<details><summary>Suggested fixes</summary>\n\n`;
    for (const finding of findings) {
      body += `- **${toText(finding.file)}** (\`${tableText(finding.category)}\`): ${toText(finding.suggested_fix)}\n`;
    }
    body += `\n</details>\n`;

    if (postedSuggestionCount > 0) {
      body += `\n💡 Posted ${postedSuggestionCount} inline GitHub Suggested Change${postedSuggestionCount === 1 ? "" : "s"} for directly applicable fixes.\n`;
    }

    if (skippedSuggestions.length > 0) {
      body += `\n<details><summary>Skipped suggested changes (${skippedSuggestions.length})</summary>\n\n`;
      for (const skipped of skippedSuggestions) {
        body += `- ${skipped}\n`;
      }
      body += `\n</details>\n`;
    }
  } else {
    body += `${emptyLine}\n`;
  }

  return body;
}

export async function buildDiffLinesByFile({ github, owner, repo, pullNumber }) {
  const prFiles = await github.paginate(github.rest.pulls.listFiles, {
    owner,
    repo,
    pull_number: pullNumber,
    per_page: 100,
  });
  return new Map(prFiles.map((file) => [file.filename, collectRightSideLines(file.patch)]));
}
