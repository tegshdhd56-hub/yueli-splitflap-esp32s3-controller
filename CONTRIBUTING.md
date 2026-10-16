# Contributing

Thank you for helping improve the YUELI Split-Flap controller.

## Before opening a change

1. Search existing issues and pull requests.
2. Keep credentials and customer data out of commits, screenshots and logs.
3. Describe which controller board, unit firmware and display dimensions you tested.
4. State whether the change was compile-tested, bench-tested or tested on a complete mechanical display.

## Development workflow

1. Fork the repository and create a focused branch.
2. Copy `LocalSecrets.example.h` to `LocalSecrets.h` or run the local secret generator.
3. Build with the versions in `docs/BUILDING.md`.
4. Test HTTP error paths as well as successful requests.
5. For movement changes, test startup locks, calibration locks and the absence of a hardware completion acknowledgement.
6. Update `docs/API.md` for every route or response change.

## Pull request checklist

- [ ] No passwords, keys, tokens, customer Wi-Fi names or device history are included.
- [ ] The firmware compiles with the documented board settings.
- [ ] New request bodies have an explicit maximum size.
- [ ] Inputs are validated before filesystem, Wi-Fi or RS485 operations.
- [ ] State-changing routes have an authentication and network-exposure decision documented.
- [ ] Mechanical commands do not claim completion without evidence.
- [ ] User-facing behavior and upgrade impact are documented.

## Coding notes

- Prefer `/api/v1/` for new JSON APIs.
- Preserve current UI routes unless a migration path is included.
- Keep HTTP handlers short; queue long-running work for `loop()` where possible.
- Avoid dynamic allocation in long-lived or frequently called paths.
- Preserve the single-outstanding-query rule for PY32 offset reads.
