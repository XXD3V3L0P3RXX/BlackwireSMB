# Contributing to Blackwire

Useful contributions include reproducible bug reports, focused source fixes, controller/browser accessibility, performance measurements and documentation. Keep each pull request focused on one behavior. Discuss format, lifecycle or source-validation changes before expanding them.

## Report the observed result

Include the preview/source revision, resident and reader identity when known, runtime profile, a redacted reproduction, and the exact result. Separate preparation time, launch time, menu/scene loading and gameplay. A main/start menu is a startup pass, not a gameplay test; distinguish loading, controller prompts, user closure and crashes. Host tests and compiler success are separate from console compatibility.

Use the [standalone Blackwire repository](https://github.com/XXD3V3L0P3RXX/BlackwireSMB) for this product. The native Dashboard is a separate development project. Follow [security reporting](SECURITY.md) for sensitive issues.

## Preserve safety and source boundaries

Preserve bounds, cancellation, exact source/receipt identity, ownership, readback and per-request validation. Keep full Send separate from bounded unchanged-Ready launch. Do not silently retry an unknown mutation, adopt unknown folders or registrations, delete checkpoints to bypass an interruption, or replace global runtime libraries as a fallback.

Use synthetic fixtures and neutral project names. Do not submit game content, system modules, keys, saves, credential/config files, tokens, personal paths or addresses, raw captures or memory dumps. Redact reports before posting publicly.

Include focused checks for changed behavior, relevant documentation and a truthful list of checks run and not run. See [build instructions](docs/BUILD.md); keep the standalone resident and reader build usable from a clean checkout without private research folders or previous output.

Add user-visible fixes, additions and known limitations to [CHANGELOG.md](CHANGELOG.md). Attribute console results to their actual resident and reader versions; do not carry an earlier gameplay result forward as a new test of a later build. For loading or system-dialog reports, include the source format, launch route, background payload names and separately measured preparation/menu/gameplay stages, with private details removed.

## Publishing

Publish Blackwire source updates and releases to both GitHub and Etawen in the same task. Branch commits, release tags, notes and verified release downloads must match. See the [publication workflow](docs/PUBLISHING.md) and repository [agent instructions](AGENTS.md).

## Licensing and attribution

Original Blackwire integration is GPL-3.0-or-later. Preserve imported notices and record upstream URL/revision, local changes and required build transformations. Credit does not replace applicable corresponding-source and static-library relinking obligations. See [notices](THIRD_PARTY_NOTICES.md) and [credits](CREDITS.md).
