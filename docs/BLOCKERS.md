# M5 QuantLib integration blocker

The QuantLib integration could not be completed within the M5 time-box because
QuantLib is not installed in the owner's configured vcpkg environment. The
`SCHD_WITH_QUANTLIB=ON` CMake probe reached FetchContent setup but
`find_package(QuantLib CONFIG REQUIRED)` cannot resolve a package because no
QuantLib package is currently present in vcpkg.

Instead of depending on an unavailable library, M5 ships the
`cpu_pricing` fallback. QuantLib integration remains optional and can be
revisited when QuantLib is installed and discoverable through the configured
toolchain.
