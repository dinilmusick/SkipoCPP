# Wave 4 native acceptance and parity

## Acceptance gates

1. Every native dependency is checked out at the exact Git SHA recorded
   in `docs/dependency-lock.md`; no version range or branch floats in CI.
2. The public CMake `SkipoCPP::core` target is installed with its package
   configuration and transitive installed native dependencies.
3. Six strict C++17 consumer builds (GCC Linux Debug/Release, Clang macOS
   Debug/Release, MSVC Windows Debug/Release) run **outside** the SkipoCPP
   source checkout. They use the installed prefix and no source include path.
4. Every platform runs a negative CMake consumer configuration without an
   installed SkipoCPP package and must observe a nonzero result.
5. Ubuntu AddressSanitizer and UndefinedBehaviorSanitizer run native unit
   tests and a separate installed external consumer.
6. Runtime tests cover service dependency cycles, partial lifecycle rollback,
   capability provider resolution, typed host errors, DAG ordering and value
   mapping, authorization failures, bounded retries, cancellation, in-memory
   idempotency, semantic UI events, state changes and disposal.
7. A pull-request head must pass all jobs before fenced merge; the exact new
   `main` commit must independently pass the same matrix. Persist run IDs,
   exact SHAs and source readback in ATAX, never assume merge implies success.

## Intentionally unsupported or upstream-owned parity

- No embedded AI agents, scheduler, daemon, broker, browser or Node.js; hosts
  drive the native event loop. SkipoCPP only composes finite native work.
- Workflow idempotency cache is in-process and volatile. Distributed
  durability, cross-process deduplication and asynchronous interruption
  require explicit host contracts. A retry-safe flag does not make an
  arbitrary external side effect idempotent.
- KloksCPP at the pinned revision catches `on_stop` exceptions internally.
  SkipoCPP cannot reliably surface those errors without changing the
  authoritative KloksCPP teardown contract. This is a recorded upstream
  parity gap, not an invented success or a SkipoCPP-local service clone.
- C++ native semantics are not React or browser renderers. FrnlCPP and
  FloksCPP provide the reusable UI/state/event vocabulary.
- Storage path confinement, cryptographic authorization and real remote
  transport implementations remain the injected host's responsibility.
  SkipoCPP validates invocation inputs and normalizes errors but never
  fabricates a network, disk, authentication or clock service.

## Audit references

The authoritative acceptance ledger is Supabase project
`vcaerawuwffziufvpbvw`, goal
`b4aaaf37-230a-438b-8179-9013cf73c315`, including its four ordered
Wave 4 tasks. Do not mark a task completed without evidence, separately
recorded verification and exact-main source reconciliation.
