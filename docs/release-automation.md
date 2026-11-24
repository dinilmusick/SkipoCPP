# Automated native package release

main -> exact-head native CI -> Release Please version PR -> independent PR CI -> bot merge -> immutable vX.Y.Z GitHub Release -> package distribution.

## Conan 2
This repository owns its Conan recipe. A GitHub Release independently verifies the tag, canonical version, clean CMake install and recipe. When NATIVE_CONAN_PUBLISH_READY=true, the protected native-packages environment uploads the exact package to the configured Conan 2 remote.

## vcpkg
There is no shared vcpkg registry repository. Each GitHub Release contains a self-contained <package>-<version>-vcpkg-port.tar.gz and SHA512 file containing the port metadata. The port pins the immutable source release and can be consumed as a vcpkg overlay.

## Authorization
Normal release PRs use a short-lived GitHub App installation token:
- RELEASE_PLZ_APP_ID variable
- RELEASE_PLZ_APP_PRIVATE_KEY secret
- RELEASE_PLZ_APP_READY=true

Conan additionally requires CONAN_REMOTE_URL, CONAN_REMOTE_USER, CONAN_REMOTE_PASSWORD and NATIVE_CONAN_PUBLISH_READY=true. No publication occurs before its readiness gate.

## Verification
Certification requires exact-head CI success, immutable tag/version agreement, a GitHub Release, successful Conan resolution when enabled, and a matching SHA512 vcpkg release asset. No shared registry or developer-machine publishing credential is required.
