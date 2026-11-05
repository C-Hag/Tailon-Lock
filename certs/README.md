# Certificates

The audit certificates of `tools/certify` (see `docs/TEST_GUIDE.md`): one per system (`<commit>-linux.cert`,
`-macos.cert`, `-windows.cert`), one per pair of machines (`-cross-linux-macos.cert` ...), the ticked hands-on lists
(`<commit>-hands-on-<system>.md`) and the release certificate (`<commit>-all.cert`). `tools/certify verify FILE`
checks one. Drafts and failed runs stay in `build/certs/`, outside the repository.
