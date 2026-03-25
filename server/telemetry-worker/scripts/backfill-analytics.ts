#!/usr/bin/env npx tsx
/**
 * Backfill Analytics Engine from raw R2 events.
 *
 * Re-processes stored events through the updated analytics mapper to populate
 * fields that were previously unmapped (fan/sensor/macro counts, capability
 * bits 7-12, AMS backend type, dark/light mode).
 *
 * Uses the worker's existing POST /v1/admin/backfill endpoint, which calls
 * mapEventToDataPoints() server-side and writes via the Analytics Engine
 * binding (the only supported write path — there is no external REST write API).
 *
 * Usage:
 *   WORKER_URL=https://telemetry.helixscreen.org \
 *   ADMIN_API_KEY=xxx \
 *   npx tsx scripts/backfill-analytics.ts [--prefix events/2026/03] [--dry-run] [--limit 100]
 *
 * Environment variables:
 *   WORKER_URL       - Base URL of the telemetry worker (default: https://telemetry.helixscreen.org)
 *   ADMIN_API_KEY    - Admin API key for the worker (required)
 *
 * Options:
 *   --prefix <path>  - R2 key prefix to process (default: "events/")
 *   --dry-run        - Process events but don't write to Analytics Engine
 *   --limit <n>      - Max events to process (default: unlimited)
 *   --batch-size <n> - Events per backfill POST request (default: 25)
 */

import { mapEventToDataPoints } from '../src/analytics';

const WORKER_URL = process.env.WORKER_URL ?? 'https://telemetry.helixscreen.org';
const ADMIN_API_KEY = process.env.ADMIN_API_KEY;

// Parse CLI args
const args = process.argv.slice(2);
function getArg(name: string, defaultVal: string): string {
  const idx = args.indexOf(name);
  return idx >= 0 && idx + 1 < args.length ? args[idx + 1] : defaultVal;
}
const dryRun = args.includes('--dry-run');
const prefix = getArg('--prefix', 'events/');
const limit = parseInt(getArg('--limit', '0'), 10) || 0;
const batchSize = parseInt(getArg('--batch-size', '25'), 10);

if (!ADMIN_API_KEY) {
  console.error('Error: ADMIN_API_KEY is required');
  process.exit(1);
}

interface R2ListResponse {
  keys: { key: string; size: number; uploaded: string }[];
  truncated: boolean;
  cursor?: string;
}

async function listEvents(cursor?: string): Promise<R2ListResponse> {
  const params = new URLSearchParams({ prefix, limit: '1000' });
  if (cursor) params.set('cursor', cursor);

  const res = await fetch(`${WORKER_URL}/v1/events/list?${params}`, {
    headers: { 'x-api-key': ADMIN_API_KEY! },
  });
  if (!res.ok) throw new Error(`List failed: ${res.status} ${await res.text()}`);
  return res.json() as Promise<R2ListResponse>;
}

async function getEvent(key: string): Promise<unknown> {
  const res = await fetch(`${WORKER_URL}/v1/events/get?key=${encodeURIComponent(key)}`, {
    headers: { 'x-api-key': ADMIN_API_KEY! },
  });
  if (!res.ok) throw new Error(`Get failed for ${key}: ${res.status}`);
  return res.json();
}

async function backfillBatch(events: unknown[]): Promise<number> {
  const res = await fetch(`${WORKER_URL}/v1/admin/backfill`, {
    method: 'POST',
    headers: {
      'x-api-key': ADMIN_API_KEY!,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ events }),
  });
  if (!res.ok) {
    const text = await res.text();
    console.error(`  Backfill POST failed: ${res.status} ${text}`);
    return 0;
  }
  const result = (await res.json()) as { written?: number };
  return result.written ?? 0;
}

async function main() {
  console.log(`Backfill Analytics Engine from R2 events`);
  console.log(`  Worker: ${WORKER_URL}`);
  console.log(`  Prefix: ${prefix}`);
  console.log(`  Dry run: ${dryRun}`);
  console.log(`  Batch size: ${batchSize}`);
  if (limit > 0) console.log(`  Limit: ${limit}`);
  console.log('');

  let processed = 0;
  let written = 0;
  let skipped = 0;
  let errors = 0;
  let cursor: string | undefined;
  let batch: unknown[] = [];

  do {
    const listing = await listEvents(cursor);

    for (const item of listing.keys) {
      if (limit > 0 && processed >= limit) break;

      try {
        const event = await getEvent(item.key);

        if (dryRun) {
          // Validate locally using the mapper to show what would happen
          const dataPoints = mapEventToDataPoints(event as Record<string, unknown>);
          const pointCount = dataPoints.length;

          processed++;
          written += pointCount;

          if (processed <= 5 || processed % 100 === 0) {
            const eventType = (event as Record<string, unknown>).event ?? 'unknown';
            console.log(`  [${processed}] ${item.key} -> ${eventType} -> ${pointCount} data point(s)`);
          }
          continue;
        }

        batch.push(event);

        if (batch.length >= batchSize) {
          const batchWritten = await backfillBatch(batch);
          written += batchWritten;
          processed += batch.length;
          batch = [];

          if (processed % 100 === 0) {
            console.log(`  Processed: ${processed}, written: ${written}, errors: ${errors}`);
          }
        }
      } catch (e) {
        errors++;
        console.error(`  Error processing ${item.key}: ${e}`);
      }
    }

    cursor = listing.truncated ? listing.cursor : undefined;
  } while (cursor && (limit === 0 || processed < limit));

  // Flush remaining batch
  if (batch.length > 0 && !dryRun) {
    const batchWritten = await backfillBatch(batch);
    written += batchWritten;
    processed += batch.length;
  }

  console.log('');
  console.log(`Done.`);
  console.log(`  Events processed: ${processed}`);
  console.log(`  Data points written: ${written}`);
  console.log(`  Skipped (unmappable): ${skipped}`);
  console.log(`  Errors: ${errors}`);
}

main().catch((e) => {
  console.error('Fatal error:', e);
  process.exit(1);
});
