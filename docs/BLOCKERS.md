# M5 QuantLib integration blocker

The vcpkg `quantlib` port exists. The install had not completed on the owner's
machine: its WSL environment ran out of memory while building QuantLib.

This records an incomplete local dependency installation, not a missing vcpkg
port. QuantLib integration should not be considered available until the
dependency installation succeeds and the enabled build and tests pass.
