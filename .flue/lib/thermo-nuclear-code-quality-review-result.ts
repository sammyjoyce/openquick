import * as v from "valibot";

import {
  createTranscriptExtractor,
  diffLineSchema,
  nonEmptyStringSchema,
  normalizeFindingsCandidate,
} from "./flue-result";

const suggestedChangeSchema = v.string();

// The verdict drives the GitHub review event the posting script submits:
//   approve         -> APPROVE          (allowApprove from the agent config)
//   request_changes -> REQUEST_CHANGES  (blocking gatekeeper findings)
//   comment         -> COMMENT          (advisory only)
export const thermoNuclearVerdictSchema = v.picklist(["approve", "comment", "request_changes"]);

export type ThermoNuclearVerdict = v.InferOutput<typeof thermoNuclearVerdictSchema>;

export const thermoNuclearResultSchema = v.object({
  verdict: thermoNuclearVerdictSchema,
  findings: v.array(
    v.object({
      category: nonEmptyStringSchema,
      file: nonEmptyStringSchema,
      line: v.nullish(diffLineSchema),
      start_line: v.nullish(diffLineSchema),
      severity: nonEmptyStringSchema,
      explanation: nonEmptyStringSchema,
      suggested_fix: nonEmptyStringSchema,
      suggested_change: v.nullish(suggestedChangeSchema),
    }),
  ),
  summary: nonEmptyStringSchema,
});

export type ThermoNuclearResult = v.InferOutput<typeof thermoNuclearResultSchema>;

export const extractThermoNuclearResultFromTranscript = createTranscriptExtractor(
  thermoNuclearResultSchema,
  normalizeFindingsCandidate,
);

export function createThermoNuclearFallbackResult(
  error: unknown,
  options?: { detail?: string },
): ThermoNuclearResult {
  const detail = options?.detail === undefined ? "" : ` ${options.detail}`;

  // A tooling failure must never auto-approve or block; emit an advisory comment so
  // CI still posts a review artifact without changing the merge decision.
  return {
    verdict: "comment",
    findings: [
      {
        category: "tooling",
        file: ".flue/agents/thermo-nuclear-code-quality-review.ts",
        severity: "medium",
        explanation:
          "Thermo Nuclear Code Quality Review completed but Flue could not extract a schema-valid result from the agent transcript. The workflow emitted this fallback report so CI can still post a review artifact; inspect the flue-output group for the raw assessment.",
        suggested_fix:
          "Keep the fallback result path, then tighten the agent prompt/schema in a follow-up so transcript extraction succeeds without fallback.",
      },
    ],
    summary: `Thermo Nuclear Code Quality Review schema extraction failed; fallback report emitted. Error: ${
      error instanceof Error ? error.message : String(error)
    }.${detail}`,
  };
}
