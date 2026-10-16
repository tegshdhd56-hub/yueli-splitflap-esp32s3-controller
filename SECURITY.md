# Security policy

## Supported version

Security reports should target the latest source on the default branch. R10 binary images are preserved for reproducibility but are not promised to receive security fixes.

## Deployment boundary

This controller is designed for a trusted local network. It serves plain HTTP on port 80 and a local TCP service on port 8888. Do not port-forward either service, place the device directly on the public Internet, or assume that Wi-Fi isolation alone is sufficient.

For remote operation, put an authenticated, TLS-enabled gateway or VPN in front of the device and restrict network access to the controller.

## Credentials

- Run `tools/generate-local-secrets.ps1` before compiling.
- Never commit `firmware/Master_C3/LocalSecrets.h`.
- Use a unique AP password and Developer password per deployment.
- Rotate credentials before handing a controller to a customer.
- A SHA-256 password digest embedded in firmware can still be attacked offline; use a long, random password.
- The archived R10 binary uses a legacy shared access-point credential for compatibility. Rebuild with unique credentials before customer deployment and never expose that access point beyond its intended local range.

## API guidance

- Existing general-purpose routes are not uniformly authenticated.
- New write endpoints should require authentication and enforce request-size limits.
- Do not log Wi-Fi passwords, authorization headers, session tokens or full request bodies containing secrets.
- Validate all lengths, numeric ranges, encodings and enum values before touching device state.
- Keep state-changing APIs unavailable until startup homing and calibration locks permit movement.
- Return acceptance separately from physical completion; the current hardware protocol does not prove that a flap finished moving.

## Reporting a vulnerability

Do not open a public issue containing credentials, exploit details or customer data. Contact YUELI through [the official website](https://www.yueli.hk/pages/contact-us) with the affected version, impact, reproduction steps and a safe contact address. Public disclosure should wait until a fix or mitigation is available.
