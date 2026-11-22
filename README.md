# SkipoCPP

Portable C++17 application composition for ULTRST. Capabilities and dependencies are validated, service invocation delegates to installed KloksCPP and optional KrnlCPP adapters, workflows execute caller-driven DAGs, and FloksCPP/FrnlCPP integrate semantic controls and state. No JavaScript runtime, cloud worker, daemon or scheduler is required.

## Build

Install the exact pinned native packages listed in [docs/dependency-lock.md](docs/dependency-lock.md) into a CMake prefix, then:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/prefix -DCMAKE_INSTALL_PREFIX=/path/to/prefix
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build
cmake -S consumer -B consumer/build -DCMAKE_PREFIX_PATH=/path/to/prefix
cmake --build consumer/build
ctest --test-dir consumer/build --output-on-failure
```

Headers: `skipo/application.hpp` (lifecycle, capability composition), `skipo/workflow.hpp` (typed DAG execution), `skipo/host.hpp` (injected host interfaces), `skipo/ui.hpp` (Floks controls → Frnl semantic events).

**Contract limitations**: single-threaded application lifecycle, workflow and UI; explicit borrowed host and Krnl lifetimes. Retrying operations requires explicit `retry_safe=true` and still depends on the service's idempotence. KloksCPP currently ignores service stop hook errors. Execution cache is process-local, not durable persistence. See dependency-lock for upstream constraints.
