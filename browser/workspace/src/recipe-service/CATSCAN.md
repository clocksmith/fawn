# CATSCAN: Managed recipe graph service

Parent: [Fawn workspace](../../CATSCAN.md)

## Target

Compose structural browser recipes and serve reduced graphs on Cloudflare D1 and Cloud Run.

## Authority

- Owns structural recipe ingestion, quorum promotion, sub-recipe indexing, DAG detour compression and empirical edge-weight computation.
- Clients retain browser authority, credentials and raw values.
- Local workspace admission, account, disclosure and independent review govern execution.

## Scope

- Portable graph engine (`engine.js`), atomic graph revisions and receipts (`graph-store.js`), Web-standard HTTP router (`http-handler.js`), `node:sqlite` store (`store-sqlite.js` for Cloud Run / Node 22), Cloudflare D1 store (`store-d1.js`, `worker.js`), and zero-trust client (`../client/recipe-client.js`).

## Contracts

Inputs:
- [Recipe service specification](../../recipe-service.md).
- [Recipe DAG and DP specification](../../trace-dag.md).

Outputs:
- Signed, reduced `version: 3` `TraceDag` snapshots with empirical edge weights and compression statistics.

## Invariants

- Rejects raw input values, non-origin URLs (query strings, paths, fragments), browser `Origin` headers and forbidden control types (`submit`, `password`, `file`, `hidden`, `image`).
- Authenticated `workspaceId` isolates workspace recipes.
- Catalog visibility requires independent contributors reaching `catalogQuorum` or a `publisher` credential.
- Ingested traces run loop contraction (`compressTraceRecords`) and whole-graph reduction (`reduceTraceGraph`) before storage.
- Clients validate every served graph with `TraceDag.restore` and enforce local `verifyAccount`, `disclosurePolicy` and `verifyReview`.

## Acceptance

- Evidence: [routing, quorum and disclosure](../../test/unit/recipe-service.test.js) and [atomic retries and receipt isolation](../../test/unit/recipe-transactions.test.js).

## Non-goals

- Storing personal applicant/account data or replacing sealed private `WorkflowSync` when structural labels themselves are confidential.

## Freedom

Optimize storage and routing within client trust boundaries.
