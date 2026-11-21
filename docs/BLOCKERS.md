# M5 QuantLib blocker (resolved)

The vcpkg `quantlib` port exists. The original install attempt on the owner's
machine did not complete because its WSL environment ran out of memory while
building QuantLib.

The blocker was resolved through the system `libquantlib0-dev` package. The
optional `SCHD_WITH_QUANTLIB=ON` build and its tests have been verified against
that installation.
