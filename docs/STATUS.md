# Preview status

## Current release — v0.3.0-beta.3

The current standalone browser beta uses one normal **Blackwire.elf** for direct downloads and Payload Manager. It adds automatic Media setup, startup/preparation and guarded removal repairs, companion preservation, clearer SMB access errors and preparation/read optimizations. Private startup tracing, timing and verbose Send diagnostics are disabled. See [changelog](../CHANGELOG.md), [setup](SETUP.md) and [upgrade instructions](BETA-TESTING.md).

The operator manually loaded the exact release ELF, prepared two extracted games, reached their menus/setup screens and uninstalled one. Automatic Media registration reported verified; physical tile visibility/opening and controllable gameplay remain unconfirmed for this exact artifact. The 146-command normal SDK build, focused current-source host checks, 92 UI tests and byte-identical relink check passed. Exact identities and limits are in [testing](TESTING.md#v030-beta3).

Earlier private Alan Wake 2 copy-phase measurements improved by about 73% on one workload. This is not a measured current-artifact whole-preparation or gameplay speed increase. Earlier poweroffs, intermittent removal uncertainty and the reporter's firmware 10.01 failures remain unresolved qualification limits. The recorded console environment is the 13.00 family with Kstuff-lite and stock ShadowMountPlus.

## Installation and source layout

Load the stored `Blackwire.elf` through Payload Manager after stopping the prior resident. Let automatic Media setup finish, configure the same-PC SMB shares, then use Send files → Ready → the game's PS5 Games icon. Keep the resident and share available. The [README folder diagrams](../README.md#pc-folder-structure) cover separate extracted/compressed shares and flat compressed layouts. No external global reader pack or PC server is required.

## Separate native Dashboard

Blackwire Dashboard remains under development and is not included in this standalone release. Historical preview and development results are retained in [testing](TESTING.md), with their exact artifact scope; old releases remain available for rollback.
