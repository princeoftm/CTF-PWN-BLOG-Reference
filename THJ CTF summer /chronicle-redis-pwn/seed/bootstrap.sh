#!/bin/sh
set -eu

redis-cli -p 6379 CHRONICLE.NEW 3600000 "atlas-month-close" "pending reconciliation from warehouse 3" >/dev/null
redis-cli -p 6379 CHRONICLE.NEW 5400000 "ember-retention" "hold export until review queue clears" >/dev/null
redis-cli -p 6379 CHRONICLE.NEW 7200000 "orbit-backfill" "replay batch 2026-07-31 after partner confirmation" >/dev/null
