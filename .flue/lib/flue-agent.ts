import { createAgent, type FlueContext } from "@flue/runtime";
import { local } from "@flue/runtime/node";
import { writeFileSync } from "node:fs";
import type { GenericSchema, InferOutput } from "valibot";

import { getRawOutputFromError, isSchemaExtractionFailure } from "./flue-result";

// Shared run machinery for Flue review agents. Each agent supplies its skill name,
// model, result schema, transcript extractor, and a fallback factory; this module
// owns the single init → session → skill → recover → write-artifact flow and the
// advisory-vs-fatal failure policy described in docs/agents/review-automation-failures.md.

export function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

class FlueResultSerializationError extends Error {
  constructor(label: string, cause: unknown) {
    super(`Failed to serialize ${label} result`, { cause });
    this.name = "FlueResultSerializationError";
  }
}

class FlueResultWriteError extends Error {
  constructor(label: string, outputPath: string, cause: unknown) {
    super(`Failed to write ${label} result to ${outputPath}: ${errorMessage(cause)}`, { cause });
    this.name = "FlueResultWriteError";
  }
}

function getSkillResultData<T>(response: T | { data: T }): T {
  return response !== null &&
    typeof response === "object" &&
    "data" in response &&
    (response as { data?: unknown }).data !== undefined
    ? (response as { data: T }).data
    : (response as T);
}

// Write the result to a known file so the CI step can read it directly without
// parsing noisy flue stdout (which includes tool output).
export function writeFlueResult(result: unknown, outputPath: string, label: string): void {
  let serialized: string;
  try {
    serialized = JSON.stringify(result);
  } catch (error) {
    throw new FlueResultSerializationError(label, error);
  }
  try {
    writeFileSync(outputPath, `${serialized}\n`);
  } catch (error) {
    throw new FlueResultWriteError(label, outputPath, error);
  }
}

type RunFlueReviewAgentOptions<TSchema extends GenericSchema> = {
  init: FlueContext["init"];
  skillName: string;
  model: string;
  args: Record<string, unknown>;
  resultSchema: TSchema;
  extractFromTranscript: (transcript: string) => InferOutput<TSchema> | null;
  createFallbackResult: (error: unknown, options?: { detail?: string }) => InferOutput<TSchema>;
  resultPath: string;
  label: string;
  fatalPolicy: string;
  sandboxEnv?: Record<string, string | undefined>;
};

export async function runFlueReviewAgent<TSchema extends GenericSchema>(
  options: RunFlueReviewAgentOptions<TSchema>,
): Promise<InferOutput<TSchema>> {
  type Result = InferOutput<TSchema>;

  let result: Result;
  try {
    // Flue 0.8 takes a `createAgent(...)` value rather than a config literal.
    const agent = createAgent(() => ({
      sandbox: local({
        env: options.sandboxEnv ?? {
          GH_TOKEN: process.env.GH_TOKEN,
          PR_WORKTREE: process.env.PR_WORKTREE,
        },
      }),
      model: options.model,
    }));
    const harness = await options.init(agent);
    const session = await harness.session();
    result = getSkillResultData(
      await session.skill(options.skillName, {
        args: options.args,
        // Validate the model payload before the workflow writes the result file.
        // The posting script re-validates the JSON before creating any GitHub comments.
        result: options.resultSchema,
      }),
    );
  } catch (error) {
    if (!isSchemaExtractionFailure(error)) {
      throw new Error(
        `${options.label} infrastructure failure: ${errorMessage(error)}. ${options.fatalPolicy}`,
        { cause: error },
      );
    }

    const rawOutputResult = getRawOutputFromError(error);
    const recoveredResult =
      rawOutputResult.rawOutput === null
        ? null
        : options.extractFromTranscript(rawOutputResult.rawOutput);

    if (recoveredResult === null) {
      const detail =
        rawOutputResult.reason === null
          ? undefined
          : `Raw output recovery skipped: ${rawOutputResult.reason}.`;
      result = options.createFallbackResult(error, { detail });
    } else {
      result = recoveredResult;
      console.warn(
        `Recovered ${options.label} result from Flue transcript after result block extraction failed`,
      );
    }
  }

  writeFlueResult(result, options.resultPath, options.label);

  return result;
}
