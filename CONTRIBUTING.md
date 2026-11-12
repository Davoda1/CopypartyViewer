# Contributing to Copyparty Viewer

Copyparty Viewer is a small C++17 desktop client for browsing Copyparty servers. Contributions should preserve its read-only server operations, responsive interface and straightforward Windows/Linux packaging.

## Report an issue

Use the repository's [issue tracker](https://github.com/Davoda1/CopypartyViewer/issues). Include the application version, operating system and architecture, the steps needed to reproduce the problem, and the expected and actual results. For display issues, include the desktop environment and DPI scale when relevant.

Provide a minimal synthetic example or a public file when possible. Remove credentials, tokens, server identifiers and personal paths from logs and screenshots. Do not upload your `settings.json`. Follow [SECURITY.md](SECURITY.md) for a suspected vulnerability.

## Build and test

Read [BUILDING.md](BUILDING.md) for native Linux and Windows prerequisites, the pinned dependencies and compiler overrides. The ordinary Linux entry points are:

```sh
make JOBS=4
make test
```

On Windows use `mingw32-make`. The HTTP fixture and optional display-backed Retro harness are documented in the build guide. Use controlled local fixtures to test authentication, errors, cancellation and downloads; tests should not depend on an account or write to a public server.

Test the behavior affected by your change. Core/network work needs meaningful regression coverage; UI work also needs an interactive check of navigation, keyboard focus, resizing and light/dark appearance where relevant. State clearly which operating systems and tools you actually used.

## Submit a change

Open pull requests against **`master`**. Explain the problem, the resulting behavior and how the change was checked. Keep each pull request focused, and include documentation updates when behavior or build requirements change.

Maintain these project constraints:

- Shared C++17 application code for Windows x64 and Linux X11 amd64.
- Background network and preview work, with cancellation and bounded resource use.
- Read-only server interactions and verified URL/path boundaries.
- One real application executable in runtime packages; no launcher scripts or companion application DLLs.
- Pinned dependencies, verified source hashes and project-local dependency builds.
- The official demo URL and empty embedded credentials in release builds.
- Existing third-party copyright notices, license texts and provenance.

Do not commit build output, local profiles, private server settings, credentials, tokens or generated diagnostic logs. Keep contributions compatible with the project's [MIT License](LICENSE), and identify the license and source of any proposed third-party material.

Hosted checks are reported through [GitHub Actions](https://github.com/Davoda1/CopypartyViewer/actions). Refer to the checks for the exact pull-request commit; a previous passing run does not validate a later change.
