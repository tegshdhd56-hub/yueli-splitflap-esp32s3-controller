# Release policy

This public repository currently distributes source only. The historical R10
source and full-image archives are intentionally not published here because:

- R10 used legacy shared device credentials that must not be reused;
- the archived image was not flashed to or validated on physical hardware;
- third-party binary-license and attribution obligations need a separate
  release review; and
- PY32 reference-material redistribution rights have not been established.

Build the current `R10.1-dev` source with unique credentials and validate it on
a non-production controller before deployment. A future binary release should
include fresh checksums, exact dependency provenance, complete third-party
notices, and an explicit hardware-test report.
