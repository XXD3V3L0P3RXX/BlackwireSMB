# Publish to GitHub and Etawen

Blackwire has two public publication targets:

| Host | Repository |
| --- | --- |
| GitHub | [XXD3V3L0P3RXX/BlackwireSMB](https://github.com/XXD3V3L0P3RXX/BlackwireSMB) |
| Etawen | [XD3V3LOP3RX/BlackwireSMB](https://git.etawen.dev/XD3V3LOP3RX/BlackwireSMB) |

Update both hosts whenever publishing a GitHub update. This applies to ordinary source/documentation commits as well as releases. A publication is complete only after the relevant refs and files have been verified at both destinations.

## Source updates

1. Preserve the current source and verified release artifacts before changing them. Run checks appropriate to the change.
2. Update the README, changelog, setup instructions and catalogs when behavior or release links change. Keep public source inventories current.
3. Read the remote refs on both hosts and confirm the intended update is a fast-forward. Preserve unrelated branches and existing release tags.
4. Push the same public branch commit to both repositories. Verify the remote branch hashes match the intended commit.

A documentation-only commit can follow a release tag on the main branch. It does not change the tagged source or justify replacing already published release files.

## Releases

1. Freeze the tested executable and reader identities. Keep actual hardware observations separate from host tests and earlier-build gameplay.
2. Prepare corresponding source, applicable relinking material, notices and SHA-256 checksums. Check the source archive and omit credentials, private captures, game content and external console libraries.
3. Create one immutable version tag. Publish the same tag object and target commit on both hosts.
4. Publish matching release titles, notes and prerelease status. Upload the same verified executable, source archive, relinking archive and checksum file to both hosts; do not rebuild a different executable for the mirror.
5. Download every asset from each host and compare its length and SHA-256 with the prepared original. Check the public release is visible.
6. Publish any catalog update only after its download URL is live. Verify README, changelog and catalog contents and the intended main-branch commit at both hosts.

Authentication belongs in the host's normal credential flow or an ephemeral private publishing process. Never commit tokens, put passwords in remote URLs, or include private build logs in a release. If a request has an uncertain result, inspect the destination before repeating a mutation.

If one host is unavailable or authentication fails, preserve the verified package and report the exact outstanding destination. Do not label a one-host update as fully synchronized.

## Present one current download

Feature one current version and one primary runtime ELF at the top of the README. State when that ELF already includes logging. Keep source/relinking/test files distinct from the ELF users load, and check both link labels and targets against the actual build ID. Label previous release pages clearly and retain their tags/downloads for rollback. Keep older README downloads in a collapsed history section. A test beta remains marked as a prerelease; do not claim stable qualification merely to change GitHub's Latest badge. Point the existing single Payload Manager entry at the same current tested runtime, using its verified URL and SHA-256 while retaining the beta label and stable payload ID/installed filename. Refresh instructions must identify the expected version and build ID.
