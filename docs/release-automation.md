# SkipoCPP automated release

Successful exact-SHA `SkipoCPP pinned native build and external consumer` GitHub Actions CI is mandatory before a
version PR or release tag is created. `version.txt` is the shared
authority for Release Please and CMake project version. The existing
CI verifies the pinned native dependencies and installed consumer,
so a standalone release job must not claim those dependencies are
implicitly available in a clean runner.

GitHub source archives from semantic tags are suitable for pinned CMake
FetchContent. No local release token is needed. For zero-touch
release-PR merging with CI, configure a GitHub App token, required status
checks and auto-merge; GITHUB_TOKEN-created PRs do not trigger CI.

vcpkg and Conan registry publication and consumer resolution remain
separate work; GitHub Releases are not a C++ package registry.
