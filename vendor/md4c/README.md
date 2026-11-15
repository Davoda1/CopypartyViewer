# MD4C 0.5.2

Unmodified upstream parser and entity decoder, pinned to tag `release-0.5.2`.

Source: https://github.com/mity/md4c/tree/release-0.5.2

Retrieved files: `src/md4c.c`, `src/md4c.h`, `src/entity.c`, `src/entity.h`, and `LICENSE.md`.
The original MIT license and copyright notices are preserved. `SHA256SUMS` records the vendored bytes.

Compile `md4c.c` and `entity.c` as C. CopyParty Viewer uses a restricted native
HTML renderer; it does not use MD4C's HTML renderer or a browser engine.
Raw HTML is disabled, images are represented by alt-text links, and links use
an inert index registry resolved by the host only after a user click.
