# Saved profiles and recovery

The game continues to read and write the native PC 1.3 profile under `Documents\NFS Most Wanted\<profile>\<profile>`. Unlimited stages manual saves and autosaves in a temporary file, validates the resulting native container, and publishes it with an atomic replacement. It checks whether another process changed the profile before the new save is committed.

## Recover a profile

1. Close the game and copy the profile directory and the recovery directory listed below to a safe location before any manual file edits.
2. Start the game and load the affected profile. If its file fails integrity checks, Unlimited attempts to use an intact backup that matches the current layout. It retains the damaged bytes for recovery.
3. If an intact profile is rejected after a change in game data, try loading it a second time in the same session. This lets the game try a previously confirmed copy for the current database and profile size, when one exists.
4. Review the loaded career, garage and settings before saving. Saving a recovered session displaces the previous file and preserves a copy under `displaced`.

## Recovery directory

The location is `Documents\Unlimited Save Recovery\profiles\<profile>`.

| Folder | Contents |
| --- | --- |
| `history` | Up to three recent revisions that pass the native integrity checks |
| `loaded\<database>-<size>` | The first and most recent copies actually decoded under the matching database and layout |
| `displaced` | A prior version preserved when a recovered session is saved |
| `conflicts` | A pending version preserved when an external edit prevents a save, when the recovery write succeeds |

A conflict means that the byte content on disk changed after the game read it or last saved it. Close the game, back up the on-disk file and the preserved pending copy, and decide which version to keep before replacing anything.

The bundled PC and Xbox 360 visual databases are supported. A mod that changes career events, car identifiers or the save layout can need a separate migration. Keep your mod configuration with your backup. Copy both profile directories to a separate drive to protect against loss of the current disk. Deleting a profile through the game is intentional. A new profile with the same name starts a new recovery history.

See the [release change log](../CHANGELOG.md) and [player guide](player-guide.md).