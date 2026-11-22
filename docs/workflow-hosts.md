# Native workflow and host contract

This is the C++17 application-level counterpart of the Skipo operation graph, not an
agent scheduler, background worker, queue broker, HTTP server, or persistence engine.

## Application-bounded execution

`Workflow::run(Application&, input, options)` requires the app to be running.
It checks the lifecycle before returning an idempotency-cache hit. The legacy
`Workflow::run(KloksCPP::ServiceRegistry&, ...)` is a low-level native
escape hatch for explicitly managed services; it does not check app lifecycle.
Use the application-bound overload from application orchestrations.

A DAG step can access only its *declared* upstream step results through
`make_input`; application input remains available separately. Step and
capability dependencies must be resolvable and acyclic. Unknown upstream
references, duplicates and cycles are errors.

## Retry, cancellation and idempotency

- Every retrying step must opt in with `retry_safe=true`, supply
  `max_attempts > 1`, and the execution must have an `idempotency_key`.
- The application developer must certify that the underlying operation is
  idempotent; this portable layer cannot force external side effects to
  deduplicate. Callers must not retry unsafe writes or payments.
- Only Kloks host failures, Skipo host failures, or generic operation
  exceptions are candidates for retry. Authorization and validation
  errors terminate without retry.
- Cancellation is sampled before the cache and between steps/attempts.
  Cancellation cannot interrupt a synchronous foreign operation already
  executing; hosts own asynchronous cancellation semantics.
- Only completed runs are cached locally. The cache is volatile, process-local,
  single-threaded and not shared between processes. Runtime restart loses it.
- Correlation identifies the original execution. A cache replay returns the
  original execution/traces with `from_idempotency_cache=true`.

## Injected host capabilities

`HostBindings` borrows the host's transport, workspace, configuration, clock
and remote-capability interfaces. No default provider is fabricated. Missing
providers raise `host_unavailable`. Invalid keys/targets raise
`invalid_definition`. Host exceptions are converted to `host_failed`;
explicit typed `skipo::Error` codes are preserved. HTTP responses outside
2xx are failures; 401/403 are unauthorized. Authentication, serialization,
workspace path confinement and storage durability belong to the actual host
adapter and its authoritative shared native libraries.

Applications must retain source Kloks/Krnl objects and host bindings for the
entire application lifetime. The native runtime is caller-driven and does
not create transient AI agents or background execution.
