/* oxlint-disable */
// GitHub Action `github-script` injects dynamic github/context/core objects.
import {
  buildDiffLinesByFile,
  buildReviewBody,
  buildSuggestionComments,
  errorMessage,
  toText,
  validateReviewResult,
} from "./flue-review-post.mjs";

const THERMO_ENUM = { enumField: "verdict", enumValues: ["approve", "comment", "request_changes"] };
// Hidden marker lets a later run find and supersede this run's verdict review.
const REVIEW_MARKER = "<!-- flue:thermo-nuclear-code-quality-review -->";

const VERDICT_EVENT = {
  approve: "APPROVE",
  request_changes: "REQUEST_CHANGES",
  comment: "COMMENT",
};

const VERDICT_PRESENTATION = {
  approve: { emoji: "✅", emptyLine: "✅ Approved — no blocking findings in this PR." },
  request_changes: { emoji: "🔴", emptyLine: "No itemised findings; see the summary above." },
  comment: { emoji: "🟡", emptyLine: "No blocking findings; advisory notes only." },
};

export function isBlockingFinding(finding) {
  return toText(finding.severity).trim().toLowerCase() === "blocking";
}

// Never auto-approve a PR that still carries a blocking finding, even if the model
// returned `approve`; downgrade to an advisory comment instead.
export function resolveEffectiveVerdict(verdict, findings) {
  if (verdict === "approve" && findings.some(isBlockingFinding)) {
    return "comment";
  }
  return verdict;
}

export function mapVerdictToEvent(verdict) {
  return VERDICT_EVENT[verdict] ?? "COMMENT";
}

async function dismissStalePriorReviews({
  github,
  core,
  owner,
  repo,
  pullNumber,
  currentReviewId,
}) {
  let reviews;
  try {
    reviews = await github.paginate(github.rest.pulls.listReviews, {
      owner,
      repo,
      pull_number: pullNumber,
      per_page: 100,
    });
  } catch (error) {
    core.warning(`Could not list prior reviews to supersede: ${errorMessage(error)}`);
    return;
  }

  // Only APPROVED / CHANGES_REQUESTED reviews gate merge and can be dismissed; clearing
  // ours leaves only the current run's verdict active.
  for (const review of reviews) {
    if (currentReviewId && review.id === currentReviewId) {
      continue;
    }
    if (!review.body || !review.body.includes(REVIEW_MARKER)) {
      continue;
    }
    if (review.state !== "APPROVED" && review.state !== "CHANGES_REQUESTED") {
      continue;
    }
    try {
      await github.rest.pulls.dismissReview({
        owner,
        repo,
        pull_number: pullNumber,
        review_id: review.id,
        message: "Superseded by a newer Thermo Nuclear Code Quality Review run.",
      });
    } catch (error) {
      core.warning(`Could not dismiss prior review ${review.id}: ${errorMessage(error)}`);
    }
  }
}

async function submitVerdictReview({
  github,
  core,
  owner,
  repo,
  pullNumber,
  event,
  body,
  comments,
  skippedSuggestions,
}) {
  try {
    const response = await github.rest.pulls.createReview({
      owner,
      repo,
      pull_number: pullNumber,
      event,
      body,
      comments,
    });
    return { event, reviewId: response.data?.id ?? null };
  } catch (error) {
    core.warning(
      `Could not submit ${event} review with ${comments.length} inline comment(s): ${errorMessage(error)}`,
    );
  }

  // A bad inline anchor must not drop the verdict; retry without comments.
  if (comments.length > 0) {
    for (const comment of comments) {
      const location = comment.start_line
        ? `${comment.path}:${comment.start_line}-${comment.line}`
        : `${comment.path}:${comment.line}`;
      skippedSuggestions.push(`${location}: dropped after the review submission was rejected`);
    }
    try {
      const response = await github.rest.pulls.createReview({
        owner,
        repo,
        pull_number: pullNumber,
        event,
        body,
      });
      return { event, reviewId: response.data?.id ?? null };
    } catch (error) {
      core.warning(`Could not submit ${event} review: ${errorMessage(error)}`);
    }
  }

  // Last resort: a COMMENT review is never rejected for permissions/self-approval, so
  // the assessment still reaches the PR even when APPROVE/REQUEST_CHANGES is refused.
  if (event !== "COMMENT") {
    try {
      const response = await github.rest.pulls.createReview({
        owner,
        repo,
        pull_number: pullNumber,
        event: "COMMENT",
        body,
      });
      core.warning(`Submitted assessment as a COMMENT review after ${event} was rejected.`);
      return { event: "COMMENT", reviewId: response.data?.id ?? null };
    } catch (error) {
      core.warning(`Could not submit COMMENT fallback review: ${errorMessage(error)}`);
    }
  }

  return null;
}

export async function postThermoNuclearReview({ github, context, core, thermoResult, prNumber }) {
  const owner = context.repo.owner;
  const repo = context.repo.repo;
  const pullNumber = Number(prNumber);

  if (!Number.isInteger(pullNumber) || pullNumber <= 0) {
    core.setFailed(`Invalid PR number for Thermo Nuclear Code Quality Review: ${prNumber}`);
    return;
  }

  let result;
  try {
    result = validateReviewResult(JSON.parse(thermoResult), THERMO_ENUM);
  } catch (error) {
    core.setFailed(`Invalid Thermo Nuclear Code Quality Review result: ${errorMessage(error)}`);
    return;
  }

  const findings = result.findings;
  const summary = result.summary;
  const verdict = resolveEffectiveVerdict(result.enumValue, findings);
  const event = mapVerdictToEvent(verdict);
  const presentation = VERDICT_PRESENTATION[verdict] ?? VERDICT_PRESENTATION.comment;

  const diffLinesByFile = await buildDiffLinesByFile({ github, owner, repo, pullNumber });
  const skippedSuggestions = [];
  const suggestionComments = buildSuggestionComments({
    findings,
    diffLinesByFile,
    skippedSuggestions,
  });

  const statusLine =
    verdict === result.enumValue
      ? `**Verdict:** \`${verdict}\``
      : `**Verdict:** \`${verdict}\` _(downgraded from \`${result.enumValue}\`: blocking findings present)_`;

  const body = buildReviewBody({
    marker: REVIEW_MARKER,
    heading: `${presentation.emoji} Thermo Nuclear Code Quality Review`,
    statusLine,
    summary,
    findings,
    // Inline suggestions ride along in this same review, so no "Posted N" footer.
    postedSuggestionCount: 0,
    skippedSuggestions,
    emptyLine: presentation.emptyLine,
  });

  const submittedReview = await submitVerdictReview({
    github,
    core,
    owner,
    repo,
    pullNumber,
    event,
    body,
    comments: suggestionComments,
    skippedSuggestions,
  });

  if (submittedReview === null) {
    core.setFailed("Thermo Nuclear Code Quality Review could not post its verdict to the PR.");
    return { verdict, event, submittedEvent: null, reviewPosted: false, skippedSuggestions };
  }

  await dismissStalePriorReviews({
    github,
    core,
    owner,
    repo,
    pullNumber,
    currentReviewId: submittedReview.reviewId,
  });

  return {
    verdict,
    event,
    submittedEvent: submittedReview.event,
    reviewPosted: true,
    skippedSuggestions,
  };
}
