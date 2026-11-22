# Wave 4 native dependency baseline
Source-of-truth: exact GitHub main revisions verified 2026-11-22.
Skipo TypeScript reference: 0dee40fcb6d2babb067f9924bacb85d215a410b0 (reference only).

| Native dependency | SHA | Package target |
| --- | --- | --- |
| GutCPP | 443ad9397c081dd596e784c50cff14c1ecad95c6 | gutcpp::gutcpp |
| KrnlCPP | b2b9814e2fb7034a72d5f9c3b5fd6a3b005ed883 | krnl::krnlcpp |
| FrnlCPP | 536a46228a8fde46fddc95447ca264fa2ed5fd6b | FrnlCPP::semantic |
| KloksCPP | ed99c35c34a16fb1ff77084ec936f44c8f967ed5 | KloksCPP::klokscpp |
| FloksCPP | 32e8088630e415718d35f30fd49e456727c1fadc | FloksCPP::core |

The package cannot compile against arbitrary newer upstream revisions without a contract audit. CI explicitly checks each pinned Git SHA before installing packages and compiles a separate consumer against exported installed targets.

Constraints: portable core does not read any environment variable or database credential. In Skipo TypeScript's current checked-in environment contract SYSTEM_DATABASE_URL remains required, conflicting with the active ordinary-cloud-application APP_URL-only invariant in ATAX; it is not ported into C++. Host configuration must be injected.

Ownership: KrnlCPP services borrowed by kloks::bind_krnl_service MUST outlive the application ServiceRegistry. The caller should declare Krnl objects before Application. KloksCPP ServiceRegistry currently catches on_stop exceptions in stop_one; this upstream limitation is not reimplemented in SkipoCPP and prevents certified failure reporting for teardown hooks until fixed upstream. Register and invoke operations on one caller thread; do not concurrently mutate Application, Workflow or UI runtime.

Checked teardown release (KloksCPP PR #8): canonical provider main `ed99c35c34a16fb1ff77084ec936f44c8f967ed5`. Consumer requires fresh exact-head and post-main CI with negative teardown tests.
