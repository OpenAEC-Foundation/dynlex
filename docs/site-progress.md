# Website game progress

`site/games/progress` owns the save format, upgrades, timestamps, profile loading,
guest import and per-game merge policy in DynLex. The browser supplies storage and
the epoch-millisecond clock through explicit synchronous UI capabilities. Local
edits finish saving during the edit event, before a reload can interrupt them.

The persisted contract remains version 7: `version`, `games`, and `lastGame`;
each game entry contains an exact millisecond `updatedAt` and its `data`.
On opening the store, all `dynlex.games.guest`, `dynlex.games.user:*` and
`dynlex.games.backup:*` files pass through the same version-1-through-7 upgrades
used for imported saves. Upgrades include farm source phrases, instruction
operands, source-map rebuild markers, actor state and entity identities. Current
game loops contain no old-format compatibility branches. A corrupt file or
failed storage write blocks further writes and preserves its source. The UI
reports a generic error; details go to the log.

Merging picks each game's greatest timestamp, with local entries winning ties.
The latest game becomes `lastGame`, with River first on a timestamp tie. Guest
games fill only missing account games, retaining the guest's original file.
`site/account` uses these same operations when an authenticated identity arrives.
The complete target profile is persisted before its metadata or in-memory state
changes. Switching profiles resets the retained game and leaves IDE drafts intact.

The DynLex controller owns authentication state, request generations, ten-second
save deadlines, connectivity, cloud revision comparisons, conflicts and logout.
It retains the exact raw server payload as the expected transaction revision,
including when an older save upgrades. Edits made during a write remain dirty.
An explicit Load Account choice backs up displaced local progress; Keep Game reads
a fresh revision before another comparison, so another device can still win a race.
Sign-out flushes the snapshot taken when requested, without waiting indefinitely
for later game activity. Failed auth restoration hides the previous account's game.
The JavaScript provider only calls Firebase SDK primitives and owns SDK resources.
Cancelled replies retain their request purpose through the application input loop.
Only compiler artifacts, graphics creation and decoded audio buffers require host
resource disposal; null-valued auth/timer replies and language-service arrays do
not get interpreted as object envelopes.

River persists source text, solved state, and every goal's count and seen keys.
Counts and completion state remain separate: an upgraded solved save may have
zero rescue progress. Unknown seen keys and counts above the visible target are
preserved, and replaying an already seen event cannot count it again. Resetting
or editing the scene retains the player's progress.

Verification includes `tests/integration/site_progress`, the storage-controller
fixture in `tests/web/site_runtime.py`, and `tests/web/progress_model.mjs`. The
latter compares native and shipped-browser-compiler output with the replaced
implementation until final migration cleanup freezes those fixtures.
