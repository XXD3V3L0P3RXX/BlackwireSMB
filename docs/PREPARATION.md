# Preparation, launch and storage

## Send files and Ready

**Send files** plans the selected extracted folder or supported compressed image, copies and verifies real startup files, creates sparse streamed entries with the original logical lengths, and publishes the preparation index and receipt. It then sets up the per-game reader and ordinary PS5 registration before reporting Ready. Executables, modules and required startup data use real console blocks; bulk assets remain on the PC.

The first preparation can take several minutes or longer. Game size, file count, source storage and network conditions all matter. Record file/byte progress and preparation time separately from the game's own loading. Unknown totals remain indeterminate rather than implying zero work.

Ready means the selected preparation and registration passed their checks. It is not a universal compatibility result. For an unchanged Ready source, launch uses bounded receipt, index and required-startup checks rather than repeating the full source-tree walk. Per-read source and file identity checks remain active.

If files are still valid but reader/icon setup is incomplete, Blackwire keeps those files and can offer **Finish setup** or **Finish setup and play**. A failed startup-data check can require actual repair. Refresh observes the current exact selection and registration; it does not blindly install a game or clear uncertain ownership.

Progress, session updates and errors appear with the selected game card. If setup fails, keep its detailed stage and reason when reporting the issue; an error for one game does not mean another game's preparation failed.

An image appearing in the share's listing does not establish that Blackwire can read its contents. A connected share can still refuse the exact image's file access. Check both Windows permission layers for the same SMB account as described in [file Read access](SETUP.md#a-listed-image-still-needs-file-read-access). Preserve the first error before retrying; repacking an image or deleting console libraries does not repair a Windows access denial.

Setup step 8 checks registration ownership and retained history; step 9 requests and independently verifies registration. Existing same-title copies, incomplete app metadata, database observations or missing manual-source membership can stop those stages. Their failure alone does not establish a firmware cause. Keep the exact stage and reason; valid retained startup files may allow **Finish setup**, while unknown outcomes still require observation rather than repeated registration.

While a game is running, Send files remains blocked and the selected card names the session that must be closed. Close the named game normally before starting preparation. **Refresh library** rescans the configured shares after source changes; it does not automatically repeat a failed Send or resolve uncertain registration ownership.

Two shares can be combined in one library, with each copy bound to its own server/share/path identity. The normal guarded reader upgrade recognizes exact known previous reader binaries and retains valid startup files. It does not accept unknown predecessor bytes or reconnect a moved source by title alone. A prepared compressed image uses its sealed generation and logical map for bounded launch validation; required local startup checks remain active.

## Launch from the Games area

A completed setup retains the game's ordinary PS5 icon after normal exit. Select that icon with **Blackwire running and the PC share available**. The reader asks the resident over console loopback for that game's startup inputs. No earlier web Play request is needed.

The resident validates the game process and exact prepared copy, adopts the session, and removes its private session inputs after confirmed exit and idle observations. SMB password bytes are not returned through the web page. Direct home launch and repeat launch were observed for Hot Wheels on the [working build](TESTING.md#working-resident-and-reader).

An interrupted reader update or uncertain descriptor/lock release blocks the affected operation. Blackwire must not fabricate a successful release or silently repeat a mutation. Crash/restart recovery is not fully qualified; restarting the payload alone does not prove reconciliation of an interrupted session.

## Recovery after a deleted preparation

When status checks positively establish that the exact prepared folder is gone, the update stops using its cached Ready state. A new full Send can prepare that source again when the remaining checks allow it. An unreadable or uncertain folder is not treated as missing.

An older completed registration history can otherwise block a fresh copy's setup or Uninstall. The focused recovery verifies the same source and fresh preparation, confirms that the old app registration and manual membership are absent, and records completion of that old event while retaining its history. Active games, unresolved operations, uncertain observations and foreign sources continue to refuse the action.

The earlier folder-only recovery resident completed one ordinary GTA **Finish setup** on console using retained files, with no source-file recopy or manual registration repair. Its later modal repair retained that backend and reader; those are historical artifact-specific results. The current combined-library reader also completed known-reader Finish setup with startup files retained. Ordinary Uninstall recovery has host coverage; updated hardware deletion qualification remains pending. See the [earlier GTA recovery result](TESTING.md#gta-recovery-on-the-prior-resident) and [current observations](TESTING.md#compressed-and-two-share-preview).

## Sparse storage

Three figures describe different things:

| Figure | Meaning |
| --- | --- |
| Logical size | The full lengths expected by the game, including sparse holes. |
| Allocated console storage | Filesystem blocks actually used by startup data and written ranges. |
| Network bytes | Data actually delivered from the PC during a measured interval. |

A folder can have a huge logical size while using much less console space. Check allocated blocks or console free space. A normal file-manager download can materialize the placeholder's full length with zeros from unwritten holes; that download is not the original backup. Preserve the complete PC files.

Reader counters are unavailable in some resident status paths. An unavailable counter is unknown, not zero bytes and not a speed measurement. See [testing](TESTING.md).

## Uninstall and Restore

Explicit streamed-copy **Uninstall** removes only the exact admitted console preparation and owned registration, and marks that source Removed in the library. PC originals, saved data, unrelated full console games and preexisting registrations are separate ownership domains.

**Restore** returns the source to library visibility; it does not recover deleted console files. Send files prepares a new copy when allowed. For partial or unknown outcomes, inspect the current status instead of repeating a delete. Manually removing a folder is not equivalent to a completed Blackwire Uninstall, and may leave registration or ownership work unresolved.

### Existing console metadata and incomplete setup

An empty ShadowMount library does not prove that a title is uninstalled. The console can retain its app metadata after the original game folder has been removed. A new Blackwire data folder does not remove that separate installation. Step 8 must distinguish it from an absent registration; creating a replacement `mount.lnk`, copying library files into place or deleting an ownership journal is not a repair.

In the unreleased revision, Details keeps the removal refusal visible. When a Blackwire startup copy can be verified, **Remove startup files only** offers a separate confirmation. It removes that exact startup copy and its preparation record while preserving the existing console installation, its metadata, saved games and the PC source. It does not uninstall the existing title, repair its registration or move the PC source into Removed. Execution rechecks the prepared inventory, inactive reader and source exclusion; a displayed button alone is not removal authority.

To replace an existing console installation, use its normal console uninstall before preparing a new copy. If the title is absent from the visible library but setup still reports existing or unreadable registration metadata, keep the exact error for diagnosis. Do not edit the console database or manufacture the missing files. The preparation-only action and revised registration errors still require console qualification before release.
