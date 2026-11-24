# SkipoCPP: end-to-end native package release

**Canonical version:** `version.txt` is read by CMake and updated by Release Please's `simple` release strategy. `.release-please-manifest.json` must agree. Immutable `vX.Y.Z` GitHub source tags drive downstream publication.

## Fully automatic flow after one-time authorization

1. Native CI and `Native package recipe lint` run on incoming pull requests and main. Successful exact-main CI permits `.github/workflows/release-native.yml` to create a semantic-version Release Please PR using a short-lived GitHub App token (not GITHUB_TOKEN).
2. `.github/workflows/native-release-pr-automerge.yml` automatically merges only bot-authored, release-metadata-only version PRs whose independent PR-triggered CI workflows all succeeded at an unchanged SHA.
3. Release Please tags the merged version and publishes a GitHub Release. `.github/workflows/publish-native-packages.yml` verifies immutable tag, canonical version and main ancestry, then publishes Conan 2 recipes/binaries and proposes an SHA512-pinned vcpkg port to `dinilmusick/forge-registry`.
4. The shared vcpkg Git registry updates its immutable Git-tree version index/baseline and runs clean consumer installation CI. Its own GitHub App merges the registry update after exact-head checks.

## One-time configuration; never use a local publishing token

- Install a GitHub App with **Actions read, Contents write, Pull requests write** for each source repo and `forge-registry`. Set repository variable `RELEASE_PLZ_APP_ID`, secret `RELEASE_PLZ_APP_PRIVATE_KEY` and variable `RELEASE_PLZ_APP_READY=true` in each.
- To turn on vcpkg proposals, set `NATIVE_VCPKG_PUBLISH_READY=true` once the GitHub App has write access to the currently private `forge-registry`; make the registry public if packages should be fetchable anonymously.
- Configure the protected `native-packages` environment for tagged releases. Add `CONAN_REMOTE_URL` as a repo variable and `CONAN_REMOTE_USER`, `CONAN_REMOTE_PASSWORD` as secrets. After verifying remote permissions, set `NATIVE_CONAN_PUBLISH_READY=true`.
- **Dependency gate:** Keep `NATIVE_UPSTREAM_PACKAGES_READY=false` until gutcpp/0.1.0, krnlcpp/0.3.0, klokscpp/0.1.0, frnlcpp/0.1.0, flokscpp/0.1.0 are published and independently resolved from the Conan and vcpkg registries. Then set it true.

After the first setup, library changes, version PRs, tags, Conan packages and vcpkg registry updates are automated. A missed older tag can be replayed using `Native package distribution` workflow_dispatch with that exact tag and `publish=true`.

**Certification:** Check actual GitHub source tag, Conan remote availability, vcpkg baseline/port Git tree and independent external CMake consumers; skipped jobs do not count as registry publication.
