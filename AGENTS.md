# Blackwire publication instructions

- GitHub and Etawen are paired publication targets. Whenever publishing a Blackwire update to GitHub, also update `https://git.etawen.dev/XD3V3LOP3RX/BlackwireSMB` in the same task.
- Keep the corresponding public branch commits and release tags identical on both hosts. Use ordinary fast-forward pushes; preserve unrelated refs and do not rewrite existing release tags.
- For a release, publish the same title, notes, prerelease status and downloadable assets on both hosts. Copy the verified assets rather than rebuilding them for the mirror, and verify each host's downloads against the same SHA-256 values.
- README, changelog, setup documentation and payload-catalog changes travel with the source commit to both hosts. Documentation-only commits do not require replacing an existing tagged release's assets.
- Verify both destinations before reporting publication complete. If one host cannot be updated, state which host remains out of date and what is blocking it.
- Keep authentication material out of source, release assets and public logs. Follow [the publication workflow](docs/PUBLISHING.md).
