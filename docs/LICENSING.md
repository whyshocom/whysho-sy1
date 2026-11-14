# whysho SY1 — Sign-in, Licensing & Copy Protection

This document describes how SY1 decides whether it is unlocked, what it stores
on the user's machine, what it needs from the servers, and an honest assessment
of what the protection does and does not guarantee.

## Overview

SY1 can be unlocked in two ways, plus a trial. They are checked in this order
by one function, `lic::resolveLicense()` (`src/licensing/Account.cpp`), which
the processor calls from `refreshLicense()`:

| # | Path | Talks to | Proof kept on the machine |
|---|------|----------|---------------------------|
| 1 | **License key** | `license.whysho.com` (`/api/activate`) | Ed25519-signed, machine-bound token, verified offline |
| 2 | **whysho account** (the main path) | `whysho.com` (`/api/plugin/*`) | Bearer token + last ownership answer, sealed to the machine |
| 3 | **Trial** | nothing | Machine-bound start date, 14 days |

The first one that says "licensed" wins. If neither a key nor the account
unlocks the plugin, the trial state is reported (running, or expired). The
result feeds the audio gate (`lic::Guard`): when it is not usable, sound is
interrupted every few seconds.

Everything is reachable from one panel in the plugin (`src/gui/LicenseDialog`):
status, account sign-in, license key, trial.

## Path 1 — whysho account (sign in)

Licensing on whysho.com is account-based: SY1 is unlocked while it is signed in
to an account that owns the product `sy1`. The client is `lic::AccountClient`
(`src/licensing/Account.h/.cpp`); the API is the website's "Plugin activation
API" (`README.md` and `app/api/plugin/**` in the whysho.com repository).

```
 plugin                                   whysho.com
   │  POST /api/plugin/login {email,password,device} ─────▶  { token, email }
   │        — or, for Google / Apple accounts —
   │  POST /api/plugin/login/start {device} ──────────────▶  { device_code, user_code,
   │                                                           verification_url, expires_in, interval }
   │  (shows user_code, opens verification_url in the browser; user approves at /activate)
   │  POST /api/plugin/login/poll {device_code}  every `interval` s ─▶ { token, email }
   │
   │  GET /api/plugin/license?product=sy1   Authorization: Bearer <token>
   │                                        ──────────────▶  { product, owned, email }
   │  POST /api/plugin/logout               Authorization: Bearer <token>   → 204
```

- **Email + password** — `login`. `device` is the computer's name; the site
  shows it on `/activate` and stores it with the session.
- **Sign in with browser** — for accounts that sign in with Google or Apple
  (the API answers `use_browser_login` if such an account tries a password),
  and for anyone who prefers not to type a password into a plugin. The code
  lasts 10 minutes; the plugin polls every `interval` seconds (5) and the
  token is handed out exactly once. The verification URL is only opened if it
  is an `https://` link.
- The bearer token is valid for a year on the server. Signing out in the
  plugin deletes it there and forgets it locally (locally first, so signing
  out works offline).

API errors are mapped to short messages (`friendlyError()` in `Account.cpp`):

| API answer | Shown to the user |
|------------|-------------------|
| 401 `invalid_credentials` | Wrong email or password. |
| 400 `use_browser_login` | This account signs in with Google or Apple. Use "Sign in with browser". |
| 429 `too_many_attempts` | Too many attempts. Wait a few minutes and try again. |
| 400 `authorization_pending` | (keeps polling) |
| 400 `expired_token` | That code expired before it was approved. Try again. |
| 401 `invalid_token` | Your sign-in is no longer valid. Please sign in again. — and the local session is cleared |
| 404 `unknown_product` | SY1 isn't listed on whysho.com yet. Try again later. |
| no connection | Can't reach whysho.com. Check your connection and try again. |
| 5xx / not JSON | whysho.com is having trouble right now / unexpected answer. |

### The local session and the offline grace window

The session lives in `<app data>/Whysho/SY1/.wsacct`
(`~/Library/Application Support`, `%APPDATA%`, or `~/.config`). It holds the
bearer token, the email, the last ownership answer and when it was given. The
password is never stored or logged; it exists only for the duration of the
login request.

The file is **sealed to the machine**: the JSON is XOR-encrypted with a SHA-256
keystream derived from the machine id and a random nonce, and authenticated
with HMAC-SHA256 keyed from the machine id. So it is not readable in plain
sight, a copy is rejected on another computer, and a hand-edited file
(`owned: true`) is rejected. A rejected file means "not signed in" — sign in
again and it is rewritten.

`AccountClient::cachedLicense()` answers without the network:

- **Licensed** (edition `account`) — the last successful check said `owned`,
  and it is at most **14 days** old (`AccountClient::kOfflineGraceDays`).
- **Expired** — it said `owned`, but more than 14 days ago. Going online and
  refreshing unlocks it again.
- **Unlicensed** — not signed in, never checked, or the account does not own
  SY1 (the reason says which).

Why 14 days: long enough for a tour or a studio without internet, short enough
that a refund or a revoked token stops working within two weeks. While the
machine is online the window never runs out, because the answer is refreshed:

- when the user signs in or presses **Refresh**;
- when the license panel is opened and the answer is over 10 minutes old;
- by a background thread inside `AccountClient` — 5 seconds after the first
  instance loads (so plugin scanners cause no traffic), and whenever the answer
  is over 6 hours old. A failed attempt is retried no sooner than 30 minutes
  later and never changes the cached answer.

A background re-check updates the stored answer; the running plugin picks it
up the next time the processor calls `refreshLicense()` (on load, and whenever
the license panel acts). It does not reach into the audio gate on its own.

Clock games: the newest time the plugin has seen is stored with the session
and the grace window is judged against it, so winding the clock back does not
extend the window. A successful online check resets it.

All plugin instances in one process share one session; other processes pick up
a sign-in or sign-out from the file within a couple of seconds.

## Path 2 — license key

Unchanged from whysho EQ1.

```
 ┌────────────┐   license key    ┌──────────────────┐   signed token   ┌─────────────┐
 │  Customer  │ ───────────────▶ │  License Server  │ ───────────────▶ │   Plugin    │
 │ (activate) │                  │  (Ed25519 priv)  │                  │ (Ed25519    │
 └────────────┘                  └──────────────────┘                  │  pub, offl.)│
                                                                        └─────────────┘
```

1. The customer has a key `WSHO-XXXXX-XXXXX-XXXXX-XXXXX`.
2. The plugin computes a **machine id** (hashed hardware fingerprint, salted
   per product) and POSTs `{licenseKey, machineId, productId: "whysho-sy1"}`
   to the license server's `/api/activate` (`lic::ActivationClient`).
3. The server checks seats, binds the machine and returns a **signed token**,
   stored at `<app data>/Whysho/SY1/license.wslic`.
4. The plugin **verifies the token offline** on every load with the embedded
   Ed25519 **public** key (`src/licensing/LicensePublicKey.h`) — product,
   machine binding and expiry included. No network is needed after activation.
5. **Deactivate** frees the seat (`/api/deactivate`, needs the key) and removes
   the token.

`ActivationClient::validateOnline()` (`/api/validate`) exists, but nothing in
SY1 calls it yet: a key token is currently trusted until it expires.

### Token format

```
token = base64url(canonicalJSON) + "." + base64url(ed25519_signature)
```

- **canonicalJSON** — the license object with keys sorted ascending, compact,
  UTF-8. The signature is computed over these exact bytes.
- The plugin **does not re-serialize**: it base64url-decodes the first segment,
  verifies the signature over those raw bytes, then parses the JSON.

| field         | meaning                                               |
|---------------|-------------------------------------------------------|
| `product`     | `whysho-sy1`                                          |
| `licenseId`   | server license id (uuid)                              |
| `email`       | customer email                                        |
| `edition`     | `standard` \| `pro`                                   |
| `machineId`   | hex SHA-256 of the client fingerprint (binds the seat)|
| `features`    | capability flags                                      |
| `issuedAt`    | unix seconds                                          |
| `expiresAt`   | unix seconds, `0` = perpetual                         |
| `activationId`| per-seat activation id                                |

Crypto: **Ed25519 (RFC 8032, SHA-512)**. The server signs with
`@noble/ed25519`; the plugin verifies with **Monocypher**
`crypto_ed25519_check`. The two are byte-compatible.

## Trial

A 14-day trial starts by itself the first time SY1 loads without a license
(`LicenseManager::refresh()` → `startOrCheckTrial()`), exactly as in EQ1. The
start date is kept in `<app data>/Whysho/SY1/.wstr` with a machine-keyed hash,
so editing or copying the file invalidates it. During the trial SY1 is fully
working; afterwards the audio gate applies until an account or a key unlocks
it. The "Start 14-day trial" button only matters where no trial file exists
yet.

## What the server side needs

SY1's client code is complete, but neither server knows the product yet.

**Website (`whysho.com` repository)**

1. `lib/products.ts` lists only `eq1` and `re1`. Until an entry with
   `slug: "sy1"` is added:
   - `GET /api/plugin/license?product=sy1` answers **404 `unknown_product`**
     (`app/api/plugin/license/route.ts` calls `getProduct(slug)`), so a signed-in
     account can never be reported as owning SY1;
   - `npm run license:grant -- <email> sy1` refuses with "Unknown product"
     (`scripts/grant-license.ts`);
   - the panel's "Get SY1" link, `https://whysho.com/plugins/sy1`, is a 404.
2. `APP_URL` must be the public `https://` origin in production. It defaults to
   `http://localhost:3000`, and `verification_url` is built from it; the plugin
   refuses to open a non-`https` link unless it was itself pointed at an
   `http://` server for development.
3. Nothing else: sign-in, the browser flow, sessions and `/activate` are
   product-agnostic. Ownership is a row in `licenses (user_id, product_slug)`.

**License server (`server/` in the whysho EQ1 repository)** — it serves exactly
one product today:

1. `server/src/crypto.ts:26` has `export const PRODUCT_ID = 'whysho-eq1'`, and
   the `License` type pins `product: 'whysho-eq1'`.
2. `server/src/service.ts:70` rejects any other `productId` with
   **400 `wrong-product`**. So activating a key from SY1 fails today; the panel
   shows "This license server doesn't issue SY1 licenses yet."
3. Tokens are always signed with `product: PRODUCT_ID` (`service.ts:119`), and
   SY1 rejects a token whose product is not `whysho-sy1` — so simply relaxing
   check 2 is not enough; the server must sign the product it was asked for.
4. `verifyLicense()` (`crypto.ts:170`, behind `/api/validate`) also rejects
   other products; `routes/health.ts` reports `whysho-eq1`.
5. The `licenses` table has **no product column** (`server/src/db.ts`), so a
   key is not tied to a product. A multi-product server needs one (and
   `activate` must check it), or an EQ1 key would unlock SY1.
6. SY1 currently embeds the **same development public key as EQ1**
   (`LicensePublicKey.h` is byte-identical, `kLicensePublicKeyIsReal = true`).
   That works with a shared server, but the key must be rotated before
   production either way: run the server's `npm run keygen`, paste the printed
   snippet into `src/licensing/LicensePublicKey.h`, rebuild, and deploy the
   server with the matching private key.

There is no `server/` directory in this repository; SY1 relies on the shared
one.

## The layered deterrent (what "protected" really means)

No software that runs on a user's machine can be made uncrackable — the CPU
must eventually run the audio path, so a determined attacker can patch it. SY1
aims to make cracking **expensive and casual sharing pointless**:

1. **Account ownership checked by the server** — the answer comes from
   whysho.com for a specific account, and a shared login is visible and
   revocable there (changing the password signs every plugin out).
2. **Signed, machine-bound key licenses** — a copied token file is useless on
   another machine; forging one needs the server's private key.
3. **Machine-sealed local state** — the account session and the trial date are
   bound to the machine id and integrity-checked, so they can't be copied
   between computers or edited by hand.
4. **Offline verification** — after activation or a successful check there is
   no network call to intercept on every load.
5. **String obfuscation** — URLs, product ids and security strings are XOR-
   encrypted at rest (`Obfuscate.h`), so `strings` reveals nothing useful.
6. **Redundant audio-path gate** (`AntiTamper.h`) — the "licensed" state is
   held as two atomics that must agree; flipping one boolean doesn't unlock
   it. When unlicensed, audio is periodically muted and noised, so the plugin
   can be auditioned but not used in production.
7. **Anti-debug probe** — `isDebuggerAttached()` is available as a reversing
   deterrent; it never blocks a legitimate DAW.

Honest limits:

- A skilled reverse engineer can defeat any of these given time. They raise
  the cost; they are not a guarantee. Treat them as a lock on a door, not a
  vault.
- The account session is sealed with a key derived from the machine itself.
  That stops copying and casual editing; someone who reverse-engineers the
  derivation can forge the file on their own machine. The server-side check
  and the 14-day window bound what that is worth.
- The account path trusts the TLS connection to whysho.com; the ownership
  answer is not separately signed. Someone who controls the machine's trust
  store can fake it — as they could patch the binary.
- The machine id depends on what the OS reports; major hardware or OS changes
  can change it, which signs the account out (sign in again) and invalidates a
  key token (activate again; the old seat must be freed).
- The rollback guard only knows times the plugin has seen. A machine kept
  offline with its clock frozen keeps its last answer.

## Developer notes

| Environment variable | Effect |
|----------------------|--------|
| `WHYSHO_ACCOUNT_SERVER` | Base URL of the website API (default `https://whysho.com`), e.g. `http://localhost:3000` for a local `npm run dev`. |
| `WHYSHO_LICENSE_SERVER` | Base URL of the license-key server (default `https://license.whysho.com`). |

To try the account path locally: run the website, add an `sy1` product to
`lib/products.ts`, create an account, `npm run license:grant -- you@example.com sy1`,
and start the host with `WHYSHO_ACCOUNT_SERVER=http://localhost:3000`.

Blocking calls: every `AccountClient` / `ActivationClient` network method
blocks and must be called off the message thread. `beginAbort()` /
`endAbort()` on either client cut an in-flight request short, so a worker
thread can be joined promptly when its owner is destroyed.
