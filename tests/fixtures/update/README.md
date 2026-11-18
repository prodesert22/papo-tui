# GitHub release answers

`github_releases_observed.json` and `github_latest_observed.json` are real
answers of the public GitHub REST API for `prodesert22/papo-tui`, observed on
2026-11-17 (`/releases?per_page=5` and `/releases/latest`). Each release keeps
only the fields the client reads: `tag_name`, `draft`, `prerelease` and, for
each asset, `name`, `size`, `digest` and `browser_download_url`.

`sha256sums_observed.txt` and `sha256sums_observed.asc` are the real
`SHA256SUMS` and `SHA256SUMS.asc` of the 1.0.0-beta.2 release, byte for byte:
the manifest of every asset and its detached OpenPGP signature, made with the
project's release key.

Synthetic answers used by the tests are written inside the tests themselves.
