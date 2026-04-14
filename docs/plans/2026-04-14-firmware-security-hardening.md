---
ticket: "fw-security"
title: "Firmware Security Hardening Plan"
date: "2026-04-14"
source: "quality-gate"
---

# Firmware Security Hardening Plan

Findings from end-to-end quality gate on the ESP32-S3 firmware (3,169 lines,
10 modules). 3 Fatal and 6 Significant issues identified.

## Fatal Issues

### F-01: ISR/handler race condition on shared cmd_data_buf
**File:** main.c, cmd_handler.c
**Problem:** Single shared `cmd_data_buf` between ISR and handler task. If two
ISA bus commands arrive before the handler processes the first, the second ISR
overwrites the buffer, corrupting the first command's data.
**Fix:** Embed the data buffer inside `cmd_request_t` (increase queue entry
size), or use a pool of buffers indexed by queue slot. Alternatively, keep
CMD_READY cleared until the handler has consumed the buffer.
**Priority:** Critical — data corruption on the ISA bus.

### F-02: Open WiFi AP with full admin access
**File:** wifi_mgr.c, web_config.c
**Problem:** `start_ap_mode()` creates an open AP ("NetISA-Setup") on every
boot, even after station mode connects. Combined with the web config server
always starting on port 80, and `check_admin_auth()` allowing all requests
when no OTA key is configured (default state), anyone in WiFi range can
access the admin panel.
**Fix:** (1) Disable AP once station mode connects, or (2) require WPA2 on
AP with device-unique passphrase, or (3) require OTA key set during first
boot before any admin actions.
**Priority:** Critical — complete device takeover with zero auth.

### F-03: Timing attack on admin key verification
**File:** web_config.c
**Problem:** All admin key comparisons use `strcmp()`, vulnerable to timing
side-channel attacks.
**Fix:** Use constant-time comparison (compare all bytes, accumulate
differences into a flag). Consider HMAC-based auth instead of raw key.
**Priority:** High — enables brute-forcing the admin key.

## Significant Issues

### S-01: HTTP session array not thread-safe
**File:** http_client.c
**Problem:** `sessions[]` accessed from multiple tasks without mutex.
**Fix:** Add mutex around session state modifications.

### S-02: drain_request_body reads wrong count
**File:** web_config.c
**Problem:** After partial read, drain attempts to read total content_length
again instead of remaining bytes.
**Fix:** Track bytes already read, subtract from remaining.

### S-03: No HTTP redirect following
**File:** http_client.c
**Problem:** ESP-IDF manual read mode doesn't follow redirects by default.
Many real sites require 301/302 redirect following.
**Fix:** Set `.max_redirection_count = 5` in client config.

### S-04: ISR data port read without full memory barrier
**File:** main.c
**Problem:** Memory barrier only executed after checking `cmd_response_ready`,
but the check itself can be reordered.
**Fix:** Move barrier before the ready flag check.

### S-05: WiFi password in plaintext NVS
**File:** nv_config.c
**Problem:** Password stored via `nvs_set_str` in plaintext.
**Fix:** Enable NVS encryption (ESP-IDF supports it).

### S-06: No OTA firmware signature verification
**File:** web_config.c
**Problem:** Any valid ESP32 binary accepted for OTA update.
**Fix:** Enable ESP-IDF secure boot v2 for signature verification.

## Implementation Priority

1. **F-02** (open AP) — highest impact, simplest fix
2. **F-01** (ISR race) — data corruption risk
3. **F-03** (timing attack) — security hardening
4. **S-03** (redirects) — feature correctness
5. **S-06** (OTA signing) — supply chain security
6. **S-05** (NVS encryption) — credential protection
7. Remaining items — correctness improvements
