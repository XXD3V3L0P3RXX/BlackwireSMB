# Preview status

**v0.1.0-preview.1** is the standalone web-interface preview of Blackwire. It runs as one `Blackwire.elf`; a separate Blackwire PC service, native Dashboard installer and Media helper are not required.

The current resident (`c4897f00…`) includes the revised web layout, with game-operation messages and progress kept on the selected game card, and a focused modal-control repair. Live browser checks confirmed Details Back, Settings Cancel, Help Close and outside-click dismissal for all three dialogs. Uninstall opened the correctly named confirmation, and Cancel returned without removing the game. Exact artifact identities and check scope are recorded in [testing](TESTING.md#current-update).

A normal GTA **Finish setup** completed on the prior resident (`a59e5cda…`), reused the retained startup files with zero source-copy bytes, and reached Ready. The current interface repair leaves that backend and embedded reader unchanged. The earlier action establishes setup recovery on its recorded binary; it is not a GTA gameplay result.

The earlier working resident/reader pair recorded in [testing](TESTING.md#working-resident-and-reader) has two observed Hot Wheels direct home-screen launches and user-confirmed gameplay. The blue Blackwire Media entry opened its page after the explicit same-ELF SDK action. Fresh RE7 Send and direct-launch adoption were observed; the visible result for that run is not yet confirmed.

Preparation now includes reader and registration setup before Ready. An unchanged valid prepared source can be observed on Refresh without automatically resending its startup files. The resident and PC share remain required during streaming and direct Games-icon launches.

The focused recovery update distinguishes a new preparation from an older completed registration history. It continues only after independently confirming that the older registration is absent. Its ordinary Uninstall path and missing-copy status handling passed focused host checks; their updated hardware qualification remains pending. Source files, saved data and historical ownership evidence are preserved by the reconciliation itself.

## Remaining preview limits

- Automatic Media registration on fresh ELF startup is not integrated. Use the optional [authenticated setup action](SETUP.md#optional-media-shortcut-setup) while idle.
- The qualified Media fallback is a normal public SDK batch scan. It can refresh other registered applications' metadata.
- Extracted folders, the qualified process profile, stock ShadowMountPlus configuration and wired SMB are the observed path. Other titles and environments need their own results.
- Loading can be slow. Broader game coverage, missing-resident behavior and crash/restart recovery are not fully qualified.
- Exact current-build and earlier gameplay observations are attributed separately in the [test record](TESTING.md).

The native **Blackwire Dashboard** is in development as a separate product. See the [roadmap](ROADMAP.md#blackwire-dashboard). This preview contains the web interface and shared resident/reader functions, without the native cover frontend or its assets.

Build provenance and dependency records live under [metadata](../metadata/). Preserve them with the source and applicable relinking material. No game files, saved data, credentials, external console system modules or private research captures belong in the release.
