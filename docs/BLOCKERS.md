# M5 QuantLib dependency note

The vcpkg `quantlib` port exists. The original install attempt on the owner's
machine did not complete because its WSL environment ran out of memory while
building QuantLib.

The dependency is now available through the system `libquantlib0-dev` package.
The optional `SCHD_WITH_QUANTLIB=ON` build and its tests have been verified
against that installation.
