# Security reporting

Blackwire handles SMB credentials, console file ownership and game launch state. Its resident serves an HTTP page and actions on the local network. Use it on a trusted LAN; do not expose its port or the SMB share to the internet.

Do not put passwords, share configuration, action tokens, saves, game assets, personal paths/addresses or unredacted console logs in public issues or pull requests.

For a sensitive issue, check the standalone repository's [Security tab](https://github.com/XXD3V3L0P3RXX/BlackwireSMB/security) for an available private reporting option. This document does not assert that GitHub private reporting is enabled. If no private route is offered, post only a redacted description asking a maintainer for a private channel; keep exploit details and affected data private.

Useful reports identify the source/preview revision, the affected boundary and a minimal synthetic reproduction. Test only on systems and data you are authorized to use. The [requirements](docs/REQUIREMENTS.md) and [test record](docs/TESTING.md) define the current supported scope; they do not claim that every runtime or network environment is secure or compatible.
