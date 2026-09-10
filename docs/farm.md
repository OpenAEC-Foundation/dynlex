# Programmable farm

The second homepage challenge is a 20×20, depth-buffered Three.js farm. It opens
after solving the river puzzle, or immediately through the challenge skip button.
The farmer is controlled directly; only workers receive programs. A new farm
starts with one worker, Ada, two chickens, and an empty cow pasture.

Click a worker or their name, choose a role, and apply its code. Every worker in
a role shares that program, but has an independent instruction pointer, loop
state, remembered subject, inventory, position and facing. Edits take effect when applied. Lumberjack
is the single working example. Players create named roles with empty editors and
write the other jobs themselves, using the command reference for guidance.

**Copy role** creates an independent program; **Delete role** frees its workers.
Rename a selected worker with the name field. **Pause worker** freezes only that
worker. **One instruction** advances its next condition, loop operation, or action
and highlights the authored code, including code inside helper functions. A walk
or chop can require several steps. The status explains which condition was true
or false. Unapplied edits clear execution highlighting until applied.

Click ground to walk, or a bed/tree/building to approach it. Selecting **You**
opens manual actions and supply controls. WASD/arrows work while the canvas has
focus; hold a key to keep walking at the selected farm pace. Drag workers or chickens onto free land, and drag flags to new destinations.
Workers and chickens pause while held. Right-drag orbits, scroll/pinch zooms, and the
camera buttons frame the whole farm or the selected person.

Workers and the farmer carry two items or bags. Tools occupy a hand; seeds,
produce and resources occupy bounded bags or bundles. A bucket holds four
waterings. The barn has finite shared supplies. Items can be deposited, dropped,
collected from piles, or passed to an adjacent person, including filled buckets.
Carried objects attach at their handles to the animated hands. Either hand can
hold and swing an axe; buckets, bags and tied log bundles hang from their grips.

Commands take typed arguments. For example, `store 2 logs` leaves the axe and
remaining logs in hand, while `store everything` empties both hands. Use
`take fertilizer from the compost heap`, `give my bucket to a nearby worker`,
`water the nearest thirsty crop`, or `plant wheat in the nearest empty plot`.
Tools and available supplies still determine whether an action succeeds.
`grab`, `take`, `collect` and `pick up` are synonyms. Without `from`, they use
reachable ground piles, barn supplies, finished compost and cow milk, nearest
first. Fences block reach. With `from`, only the named source is used, including
another person's inventory; an empty or unreachable source does not switch to
nearby items. For example, `grab fertilizer` works beside the compost heap, and
`collect 2 fertilizer from the compost heap` takes at most two from that heap.
A named item without a number takes up to one bag's worth, a number sets the total
amount (which can span both hands), and `everything` fills available hands.
Unqualified collection leaves other people's equipment alone. Buckets retain
their water when passed between people or collected from the ground.
`the nearest {kind}` is one shared function: `walk to the nearest cow`,
`walk to the nearest chicken`, and `walk to the nearest tree` pass different
entity kinds to it. The resulting target can also be used in conditions and
other target commands. Accepting a completion with Tab opens suggestions for
the next argument or literal, using the same language server as VS Code.

Target commands remember their chosen target for that worker. After
`walk to the nearest tree`, `if i can reach it:` checks that tree, and
`chop it down` chops it. `if it's still there:` tests whether the remembered
entity still exists. A dragged chicken remains the same target; a removed flag
or chopped tree does not become valid again when a replacement appears.
`set the subject to the nearest chicken` remembers a target without walking.
Item commands likewise remember their selection: `grab an axe` followed by
`put down it` puts down the axe. The shared `lib/subject.dl` supplies DynLex’s
subject reader; the farm library decides which commands set the subject.

Use a hoe to plow, seeds to plant carrots or wheat, and water to grow crops.
Harvesting creates produce, scraps and saved seeds; wheat also leaves hay.
Chopped trees leave logs and stumps which regrow. Digging with a shovel makes
channels; only channels connected to the pond below its surface carry water.
Neighboring beds stay wet. Rain arrives for the final 25 turns of each 100-turn
day, while dry weather gradually dries soil. Fertilized crops grow twice as fast.

Cows eat hay and chickens eat wheat. Each feeding produces milk or eggs after
20 turns. Animals start in separate closed pens and stop beside a person
for feeding. Fences block movement; people enter through gates that open as they
approach. A chicken placed outside its pen wanders there, respecting fences. Workers can `walk to the nearest chicken` or `walk to the nearest egg`.
Eggs appear on the ground where a chicken lays them; collect them with `collect
eggs` or `pick up nearby items`. The cow's position also follows its movement.
Crop scraps mature into fertilizer in the compost heap after 20 turns.
Village orders consume barn stock. The first order rewards a second worker;
the second brings a cow and opens the east field, before the third asks for milk.
Later orders reward more workers (up to six) and seeds. There are no carts.

Use **Add flag** to place a flag near the farmer, then drag it to its destination.
Each of the nine named colors can be used once. Click a flag's × button to remove
it and free that color. Code such as `walk to the purple flag` follows that flag's
current position; if the flag is removed, workers report that it is unavailable.

Open **Work areas**, enter a name, and drag a rectangle over available land.
**Redraw** moves its boundaries while keeping the name; **Delete** removes it.
Use `walk to the nearest thirsty crop in the area "Vegetable patch"` to limit a
search. Areas may overlap. The selected worker's route shows its current target
and follows the same path and fence rules as movement. A missing area or an empty
search reports no available destination.

Use `if there are fewer than 6 logs in the barn:` or
`if there are at least 2 buckets in the barn:` to make roles respond to shared
supplies. Conditions read the stock at the moment that worker reaches them.

Optional coding goals offer hints for watering four distinct planted beds,
planting four distinct beds, and collecting six eggs. Only successful worker
actions advance these goals; manual farmer actions do not. Repeating the same
bed does not increase the distinct-bed goals. Progress is saved.

All editable code on the site uses `src/web/ide/src/editor.js` and its shared
language service. Tab accepts a suggestion; Enter inserts a newline and indents
one level after a section-opening colon. Ordinary statements keep the current
indentation; colons inside comments and strings do not open a section. Monaco
preserves existing semantic colors while newer analysis is pending. The build
produces `web/editor.js` and shared assets for both games, examples, and full IDE.
Nested calls use six repeating colors based on their resolved nesting depth.
The language server emits the depth modifiers; `shared/call-colors.json` supplies
the palette for the website and VS Code extension. Run
`node scripts/generate_call_colors.mjs` after editing the palette. Both river and
farm games share the instruction-highlighting and coding-goal interfaces.

## Execution and assets

`web/farm-vocabulary.js` defines the command catalog. Run
`node scripts/generate_farm_vocabulary.mjs` to generate
`lib/farm_challenge.dl`. This domain library defines DynLex patterns for actions,
conditions and control flow. It emits a structured cooperative plan through the
real compiler, including nested branches, loops and reusable `to ...:` functions.
It defines its own control-flow vocabulary rather than importing `std.dl`.

The homepage compiler provides the compiled WASM artifact. A dedicated short-lived
worker executes it to build the plan, with output and execution limits. It first
confirms that its runtime imports and message handler are ready, then receives
the role artifact. Loading the runtime and instantiating WASM have a separate
30-second startup deadline;
the five-second execution limit starts when the worker enters the role's main
function. Editing, replacing, or resetting an assignment cancels its preparation.
Preparing a recursive or excessive role cannot block the farm's main thread or
its compiler worker. `farm-program.js` reads the emitted instructions, never the
user's source.
The compiler's `--trace-execution` option emits authored source events, including
evaluated branch outcomes, in UTF-16 editor coordinates. The browser compiler
exposes the same tracing option. `web/execution-trace.js` reads these events for
both games, so loops and helper calls retain their actual source locations.
During play the simulation evaluates live conditions and advances each worker by
one action per turn. Movement commands take multiple turns and recalculate routes
to the same chosen entity, including moving animals and flags. Empty loops have
a per-turn instruction budget.

Action/test records carry `key`, `argument`, `amount`, `target`, `area`, and the
authored source `range`. The library emits
`DO|actionId|argument|amount|target|area` and
`IF|conditionId|argument|amount|target|area`; item selections, target references
and transfer operations are shared across commands. Source syntax is defined by
the DynLex patterns, so the language server exposes each nested argument to both
the website editor and VS Code.

The model, actions, plan interpreter, renderer, example role and editor are separate
modules. No game behavior is added to the C++ compiler. The Three.js modules are
self-hosted; see `web/vendor/three/README.md`. All art uses native 3D geometry,
materials, shadows, and a real WebGL depth buffer.
Walking follows timed waypoints with gradual turns and a distance-driven stride.
Animals use the same continuous movement. One boundary model drives the visible
fences and movement checks. The outer fence and locked-field divider join at
their endpoints. Rain opacity and sunlight ease into each weather change.
Crop geometry includes feathery carrot leaves and wheat stalks, leaves, ears and
awns, with seedlings, green growth and mature plants. Each bed uses a single
colored mesh to keep the detailed foliage economical to render.

After changing the generated library, run `scripts/build_web.sh` followed by
`scripts/build_web_root.sh` to refresh the browser compiler's embedded libraries.
Reload an already-open preview after rebuilding: its running compiler worker
retains the libraries from the build it loaded.
Use `scripts/view_web.sh 8880` for the local preview. Its server prevents asset
caching and clears previously cached files on navigation, including modules loaded
after the page. Game saves and account cookies are retained. The cached-module
upgrade is covered by `tests/web/preview_cache_browser.mjs`.

## Verification

```sh
node tests/web/farm.mjs
node tests/web/farm_programs.mjs
node tests/web/farm_arguments.mjs
node tests/web/farm_language_upgrade.mjs
node tests/web/farm_motion.mjs
node tests/web/farm_equipment.mjs
node tests/web/farm_livestock.mjs
node tests/web/farm_management.mjs
node tests/web/farm_areas_supplies.mjs
node tests/web/coding_goals.mjs
node tests/web/execution_trace.mjs
node tests/web/game_progress.mjs
```

The second command requires the existing `build/dynlex` native compiler. It checks
the fixture in `tests/required/farm_challenge`, compiles the example plus programs
from `tests/web/farm_team_fixture.mjs`, and runs that team through 900 turns and
the cow quest. Those test programs are outside the served website.

Browser tests use the existing launcher:

```sh
DYNLEX_TEST_GRAPHICS=webgl \
DYNLEX_BROWSER_TEST_ENTRY="$PWD/tests/web/farm_browser.mjs" \
./scripts/test_web_browser.sh
```

The other entries are `farm_dragging.mjs` for actual mouse dragging and ground
clicks, `farm_progression.mjs` for river completion and skipping during loading,
`farm_visuals.mjs` for roof angles, crop closeups and rendered movement, and
`farm_preparation.mjs` for delayed worker startup, the example and custom code, execution
limits, cancellation and recovery.
`farm_equipment_browser.mjs` checks the carried models, animal enclosures and
rain transitions. `game_progress_browser.mjs` checks reloads for both games and
compiles the individual chicken/egg commands through the browser compiler.
Set `DYNLEX_SCREENSHOT_DIRECTORY` to save review images.
`execution_trace_browser.mjs` exercises stepping in both games;
`farm_features_browser.mjs` checks area drawing, scoped commands, stock conditions,
hints, reloads, and source-map upgrades from old saves.
`farm_arguments_browser.mjs` checks argument colors, target existence, and
remembered targets across a reload.
`farm_collection_browser.mjs` resumes version 6 roles, exercises all collection
aliases with and without a source, and accepts chained item/source completions.

## Saved games and accounts

The site saves river code/completion and the complete farm (world, roles,
applied programs, routine positions, inventories, orders, selection and pace) in
browser storage. Continue Game restores the most recently played challenge;
the farm resumes paused. Reset replaces the saved farm. Save format versioning
lives in `web/game-progress.js`; unreadable saves are retained without overwriting.

Google sign-in and account saves use Firebase Authentication and Firestore.
See [game-accounts.md](game-accounts.md) for project setup and emulator tests.
Guest and account saves use separate browser profiles. Signing into an empty
account imports guest progress. Cloud writes run every ten seconds while play
changes, and when the page is hidden; each local edit is saved immediately.
Concurrent cloud edits stop sync and offer an explicit choice of which game to
keep. Network errors leave the browser copy intact and sync retries.
