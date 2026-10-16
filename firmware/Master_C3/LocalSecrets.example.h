#ifndef YUELI_LOCAL_SECRETS_H
#define YUELI_LOCAL_SECRETS_H

// Copy this file to LocalSecrets.h, then replace both placeholders.
// LocalSecrets.h is ignored by Git and must never be committed.

#define YUELI_SECRETS_CONFIGURED 0

// 8-63 characters. Do not ship the example value.
#define MASTER_AP_PASS "REPLACE_WITH_A_UNIQUE_AP_PASSWORD"

// SHA-256 of the Developer page password, emitted as 32 comma-separated bytes.
// Generate this definition with tools/generate-local-secrets.ps1.
#define YUELI_ADMIN_PASSWORD_SHA256_BYTES { \
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, \
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, \
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, \
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  \
}

#endif
