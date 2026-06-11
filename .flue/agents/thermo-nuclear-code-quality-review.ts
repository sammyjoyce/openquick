/* oxlint-disable */
import { createAgent, type FlueContext } from "@flue/runtime";
import { local } from "@flue/runtime/node";
import * as v from "valibot";
import "../lib/gateway-provider";

import { errorMessage, runFlueReviewAgent, writeFlueResult } from "../lib/flue-agent";
import { isSchemaExtractionFailure } from "../lib/flue-result";
import {
  createThermoNuclearFallbackResult,
  extractThermoNuclearResultFromTranscript,
  thermoNuclearResultSchema,
  type ThermoNuclearResult,
} from "../lib/thermo-nuclear-code-quality-review-result";

export const triggers = {};
export const thermoNuclearReviewAgent = createAgent(() => ({
  sandbox: local({ env: { GH_TOKEN: process.env.GH_TOKEN } }),
  model: "gateway/gpt-5.5",
}));

// Re-exported so importers (agent tests, workflow helpers) keep their entrypoints.
export { errorMessage, isSchemaExtractionFailure };

const payloadSchema = v.object({
  prNumber: v.number(),
  repository: v.string(),
  prWorktree: v.optional(v.string()),
});

const REVIEW_HELPER_FAILURE_POLICY = {
  advisory:
    "Only Flue ResultExtractionError failures are advisory: the agent ran, but Flue could not extract or validate the final schema block.",
  fatal:
    "Agent initialization, session execution, scoped tool/command failures, provider errors, and result-file write failures are infrastructure failures and must fail CI.",
} as const;

export function getThermoNuclearResultPath(): string {
  return process.env.THERMO_NUCLEAR_RESULT_PATH || "/tmp/thermo-nuclear-result.json";
}

export function writeThermoNuclearResult(
  result: ThermoNuclearResult,
  outputPath = getThermoNuclearResultPath(),
) {
  writeFlueResult(result, outputPath, "Thermo Nuclear Code Quality Review");
}

export async function runThermoNuclearReview({
  init,
  payload,
}: FlueContext): Promise<ThermoNuclearResult> {
  const { prNumber, repository, prWorktree } = v.parse(payloadSchema, payload);

  return runFlueReviewAgent({
    init,
    skillName: "thermo-nuclear-code-quality-review",
    model: "gateway/gpt-5.5",
    args: { prNumber, repository, prWorktree },
    resultSchema: thermoNuclearResultSchema,
    extractFromTranscript: extractThermoNuclearResultFromTranscript,
    createFallbackResult: createThermoNuclearFallbackResult,
    resultPath: getThermoNuclearResultPath(),
    label: "Thermo Nuclear Code Quality Review",
    fatalPolicy: REVIEW_HELPER_FAILURE_POLICY.fatal,
    sandboxEnv: {
      GH_TOKEN: process.env.GH_TOKEN,
      PR_WORKTREE: prWorktree ?? process.env.PR_WORKTREE,
    },
  });
}

export default thermoNuclearReviewAgent;
