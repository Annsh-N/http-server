# Linux Qualification

The server targets Linux/POSIX networking and must pass more than a successful
release compile before it is called Linux-qualified.

## Qualification gates

1. GCC Debug and Release builds complete with warnings enabled.
2. Clang Debug and Release builds complete with warnings enabled.
3. All CTest suites pass in each ordinary build.
4. Clang ASan/UBSan passes with leak detection and fail-fast behavior.
5. GCC TSan passes in a separate build.

GitHub Actions runs these gates on Ubuntu 24.04. They can be reproduced on a
Linux workstation with:

```sh
./scripts/qualify_linux.sh
```

The script requires CMake 3.20 or newer, Ninja, GCC, and Clang. Sanitizers are
separate because AddressSanitizer and ThreadSanitizer cannot instrument the
same executable meaningfully.

Leak detection is enabled in the Linux ASan job. Apple's bundled ASan runtime
does not support that option, so a macOS sanitizer smoke test must omit
`detect_leaks=1`; it does not replace the Linux gate.

## Evidence policy

Adding the workflow is not itself proof that the server works on Linux. Record
the workflow URL and commit only after every matrix job passes. A sanitizer
failure is a correctness failure; it must not be dismissed as benchmark noise.
