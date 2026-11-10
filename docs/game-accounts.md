# Game accounts

Browser saves work without any external service. Enabling Google accounts needs
a Firebase project; `site/account/configuration.dl` is deliberately unconfigured until
the project's public web app configuration is supplied.

1. Register a web app in the Firebase project. Copy its web configuration object
   into the JSON object returned by `the website account configuration` in
   `site/account/configuration.dl`, replacing its JSON null value. Use `JSON text
   "apiKey" is "..."`, `JSON text "projectId" is "..."`, and the other public
   configuration fields in a `JSON object containing [...]`. Rebuild the site.
2. Enable the Google provider in Firebase Authentication and select the project's
   support email. Add `dynlex.com` and `localhost` to authorized domains; add
   `127.0.0.1` too when testing using that hostname.
3. Create a Firestore database, then publish the supplied `firestore.rules` to
   that project: `firebase deploy --only firestore:rules --project PROJECT_ID`.
4. Serve the site, sign in with Google, play, then verify Continue Game in another
   browser. Never put a service-account key or OAuth client secret in the site.

The integration uses Google's [Firebase web SDK](https://firebase.google.com/docs/web/alt-setup)
and [Google sign-in flow](https://firebase.google.com/docs/auth/web/google-signin).
The Firestore document is `players/{uid}/games/progress`; the rules require an
authenticated UID matching the path for every read and write. Google credentials
are handled by the Firebase SDK. Saved game state contains no credentials.

Local profiles are `dynlex.games.guest` and `dynlex.games.user:{uid}`. Switching
accounts resets the retained game with the selected profile while keeping IDE
drafts and unrelated program state. Signing out returns to the
guest save. A nonempty account keeps its own games when someone signs in from a
shared browser; guest games import only for challenges missing from the account.

The browser keeps the latest local copy even while signed in. Firestore writes
use transactions and compare the document with the last version read, so a
concurrent change on another device raises a save conflict. Loading the account
save retains the displaced local copy as `dynlex.games.backup:{uid}`. Keeping
the current game reads the latest revision before another comparison. If another
device changes it again, the conflict remains explicit.

Save version 7 unifies collection commands and includes parameterized role
instructions, each worker’s remembered subject and ongoing target, and identities
for trees, crops, flags and item piles.
It also retains worker names and pauses, work areas, coding goals and source maps.
`site/games/progress` upgrades every local guest,
account and backup profile and applies the same migration when reading cloud
data. Old fixed-phrase commands and literal pronouns migrate to explicit arguments;
drafts and applied sources both migrate. Compiled roles rebuild once when the farm opens,
preserving each worker's instruction and loop counters. Schema upgrades stay in
these migration modules rather than in the running game.

## Local verification

The standard Node checks need no Firebase dependency. Cloud tests use Firebase's
local Auth and Firestore emulators, with a `demo-` project that cannot contact
production services. Java 21 and Node 22 are needed for the emulator tools.

```sh
npm install --prefix /tmp/dynlex-firebase-tests --no-audit --no-fund \
  firebase-tools@15.29.0 @firebase/rules-unit-testing@5.0.2 firebase@12.18.0
DYNLEX_FIREBASE_TEST_DEPS=/tmp/dynlex-firebase-tests \
  /tmp/dynlex-firebase-tests/node_modules/.bin/firebase emulators:exec \
  --project demo-dynlex-games --only firestore 'node tests/web/game_cloud_rules.mjs'
DYNLEX_TEST_GRAPHICS=webgl \
  DYNLEX_BROWSER_TEST_ENTRY="$PWD/tests/web/game_cloud_browser.mjs" \
  /tmp/dynlex-firebase-tests/node_modules/.bin/firebase emulators:exec \
  --project demo-dynlex-games --only auth,firestore './scripts/test_web_browser.sh'
```

The DynLex account controller and the new provider can be checked independently:

```sh
python3 tests/web/site_runtime.py --case site_account_controller --case site_account_recovery --case site_account_profiles
node tests/web/firebase_provider.mjs
DYNLEX_FIREBASE_TEST_DEPS=/tmp/dynlex-firebase-tests \
  /tmp/dynlex-firebase-tests/node_modules/.bin/firebase emulators:exec \
  --project demo-dynlex-games \
  --only auth,firestore 'node tests/web/firebase_emulator.mjs'
python3 scripts/build_site.py
python3 tests/web/account_browser_build.py build/site .cache/account-browser-site
DYNLEX_FIREBASE_TEST_DEPS=/tmp/dynlex-firebase-tests \
DYNLEX_TEST_WEB_ROOT="$PWD/.cache/account-browser-site" \
DYNLEX_TEST_WEB_SERVER=static DYNLEX_TEST_GRAPHICS=webgl \
DYNLEX_TEST_SERVER_PORT=18765 DYNLEX_TEST_DEBUG_PORT=19222 \
DYNLEX_BROWSER_TEST_ENTRY="$PWD/tests/web/site_account_browser.mjs" \
  /tmp/dynlex-firebase-tests/node_modules/.bin/firebase emulators:exec \
  --project demo-dynlex-games \
  --only auth,firestore './scripts/test_web_browser.sh'
```

The configured test build injects an SDK loader and emulated Google credentials
from test fixtures. The production configuration stays null and the production
provider/controller code handles every operation in these checks.

The rule checks exercise owner access, cross-account denial, guest denial and
document constraints. The browser test uses the real Firebase SDK with
[emulated Google credentials](https://firebase.google.com/docs/emulator-suite/connect_auth)
to exercise cloud reads/writes, conflicts and sign-out. A real Google popup and
cross-device verification still require the configured project above.
