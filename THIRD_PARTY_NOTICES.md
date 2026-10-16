# Third-party dependencies

The controller source depends on libraries that are installed separately and
are not vendored in this repository. The recorded R10 build used:

| Dependency | Version | Recorded license | Upstream |
|---|---:|---|---|
| AsyncTCP | 1.1.4 | GNU LGPL v3 | https://github.com/dvarrel/AsyncTCP |
| ESPAsyncWebServer | 3.1.0 | source headers state GNU LGPL v2.1 or later | https://github.com/lacamera/ESPAsyncWebServer |
| ArduinoJson | 7.4.3 | MIT | https://github.com/bblanchon/ArduinoJson |
| ezTime | 0.8.3 | MIT | https://github.com/ropg/ezTime |
| LinkedList | 1.3.3 | MIT | https://github.com/ivanseidel/LinkedList |

The build also uses an ESP32 Arduino core recorded as
`esp32:esp32@2.0.10-cn`. Its exact mirror artifact, source commit and complete
component-license inventory must be captured before publishing a reproducible
combined binary.

The repository's Apache-2.0 license applies to YUELI-authored material only; it
does not replace the licenses above. Anyone distributing a compiled firmware
image must review and satisfy the applicable dependency terms, including
copyright notices, license copies, source availability and any relinking or
replacement requirements. That review is one reason this repository currently
publishes source only.
