# Security policy

## Report a vulnerability

For a suspected security issue, use GitHub's [private vulnerability reporting form](https://github.com/Davoda1/CopypartyViewer/security/advisories/new) when it is available. If the repository does not offer that form, open an issue asking how to establish a private reporting channel without including exploit details or sensitive data.

Include the affected version or commit, operating system, reproduction steps, expected impact and a minimal synthetic input when possible. Never include real passwords, tokens, personal server data or a saved `settings.json`. Ordinary non-sensitive bugs can be reported in the [issue tracker](https://github.com/Davoda1/CopypartyViewer/issues).

Please check whether the issue is reproducible with the latest release and identify any local modifications or custom compiled defaults. This project does not define a guaranteed response time or a long-term support schedule.

## Security-relevant behavior

- Official releases start at `https://a.ocv.me/pub/` with no embedded username or password.
- The client has no upload, deletion, rename or server-editing controls. Server permissions still determine what an account may read and download.
- HTTPS certificates are checked using Schannel on Windows and OpenSSL with the system CA bundle on Linux. URL and redirect handling enforce the configured server boundary.
- Listings, previews and decoded image dimensions have explicit limits. Markdown raw HTML is escaped, RTF embedded resources and field instructions are ignored, and CSV cells remain literal text. The native previews do not execute HTML/SVG scripts, code blocks or spreadsheet formulas.
- Document links activate only after a click. Links within the configured server use its normal path checks; external HTTP/HTTPS and query links open in the browser without attaching application credentials.
- Downloads use a temporary file and replace the selected destination only after successful completion. A cancelled or failed transfer preserves an existing destination.
- Credentials entered in the main window apply to the current session. Only an explicit Settings Save changes the persistent account.

Saved credentials use **reversible obfuscation**, not strong encryption or an operating-system credential vault. Anyone who can read the JSON file and application code can recover them. Protect the settings file and your operating-system account. Custom compiled credentials can also be recovered from an executable and must never be included in a public build.

The Windows executable is unsigned. Check downloaded release assets against the SHA-256 digest in GitHub's release metadata. Bundled dependencies keep their own licenses and may have independent security advisories; their exact versions and source hashes are documented in [BUILDING.md](BUILDING.md) and `vendor/dependencies.json`.
