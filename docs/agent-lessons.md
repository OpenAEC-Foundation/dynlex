# Lessons From All DynLex Codex Chats

The history lives outside the repo in:

- `/home/johnheikens/.codex/sessions/**/*.jsonl`

The safe extractor for it is:

- [scripts/extract_codex_user_messages.py](/home/johnheikens/Documents/Github/DynLex/scripts/extract_codex_user_messages.py)

## Method

Analyze only filtered DynLex user messages:

- match sessions where `session_meta.payload.cwd == "/home/johnheikens/Documents/Github/DynLex"`
- keep only `event_msg` records with `payload.type == "user_message"`
- skip giant dump-like payloads

Corpus used for these lessons:

- `1354` kept user messages
- `7` skipped oversized messages
- size cap: `4000` characters per message

## Core Lessons

1. Find the exact cause, not just the visible blocker.

- John repeatedly asks for the actual cause or root cause.
- Reporting "I am blocked" is not enough if the reason for the block can still be traced.
- Hacky fixes are explicitly rejected when they avoid the real mechanism.

2. Reproduce first, patch second.

- Minimal reproducible examples are a recurring requirement.
- For compiler or shader failures, narrow the problem with a small repro before changing the compiler.
- Repros are considered cheap and useful. Agents should create them proactively unless told otherwise.

3. Debug with real tools, not noise.

- GDB is explicitly preferred for compiler debugging.
- Debug prints are often rejected.
- The expectation is understanding, not instrumentation spam.

4. Do not silently fix the wrong thing.

- If the user asked for diagnosis, report diagnosis.
- If the user asked for the culprit, find the culprit.
- If syntax or source code is wrong, do not "helpfully" repair it without permission.

5. Git actions must be interpreted conservatively.

- "Revert your edits" does not mean `git revert`.
- Casual language must not trigger destructive git commands.
- Multiple agents may be working concurrently, so restoring files to HEAD can damage someone else's work.

6. Tests must be genuinely green.

- Passing for the wrong reason is still failure.
- John repeatedly asks agents to verify why tests pass, not just whether they pass.
- Representative larger targets matter too: games, shaders, scripts, not only tiny unit tests.

7. Performance is a hard constraint, not an afterthought.

- Fixes should not introduce repeated unnecessary instantiation or slow compilation paths.
- Correctness is mandatory, but performance regressions are also unacceptable.

8. Diagnostics should help users, not just reject them.

- The user repeatedly pushes toward better diagnostics and LSP quick fixes.
- Errors should be accurate, general where needed, and actionable.
- Operand-regrouping failures must surface the real nested expression that failed type probing; generic caller-site errors hide the root cause.

9. Follow explicit workflow instructions exactly.

- If the user says use GDB, use GDB.
- If the user says do not patch, do not patch.
- If the user says report first, report first.
- When an agent violates stated workflow, John treats that as a serious failure.

10. Actions produce nothing; value-producing phrases must be consumed.

- Wrap a value-returning intrinsic in a plain-English action that produces nothing and consume its result inside that implementation.
- Raw `discard` is limited to replacement-level implementation code. Callers should say what action they intend instead of discarding the result of an action-shaped phrase.
- Library code should not rely on the compiler to implicitly drop return values from calls like `glfwInit`, `fseek`, `fread`, or `fclose`.

11. Stable callable ABI is a separate concern from normal DynLex calls.

- Internal DynLex functions can keep using whatever monomorphized ABI the compiler needs.
- Anything exposed outside the program, or referenced through a function-pointer feature, needs its own stable wrapper instead of reusing the internal call path.

## Repeated Operational Preferences

These showed up repeatedly across the corpus.

### Root Cause Over Surface Fixes

- "sounds like the actual root cause. fix it."
- "find the root cause of this new bug."
- "this seems hacky and ugly. you didn't find the actual root cause."
- "diagnose and report the causes of each respective fail. make sure to find the root cause."

### Repros Are Expected

- "you may create small reproducable examples to debug."
- "with debugging i mean: reproduce the faillure."
- "report the root cause. reproduce with a small file."

### GDB Over Prints

- early DynLex compiler debugging prompts repeatedly direct the agent to use GDB
- "please don't use prints. use gdb."

### Git Safety

- "did you just use git revert?"
- "'revert your edits' does not mean 'git revert your edits'."
- "multiple agents are working at the same time."

### Report Before Fixing When Requested

- "find the culprit and report to me."
- "do NOT 'fix' dl code. if you found incorrect syntax, report to me. do NOT hide errors."
- "investigate the snake.dl fail and report to me."

### Performance Awareness

- "how can we resolve this problem without hurting performance and instantiating each function call 20 times with the same incorrect types?"
- "as long as performance is good ... and the tests pass and the code is clean, you may apply any fix you think is necessary."

### Better Diagnostics

- "the error diagnostic should include a quickfix to add '.0'."
- "i am talking about lsp diagnostics."
- "add quick fix support to the lsp and the diagnostic struct."

## What Future Agents Should Actually Do

1. Use the extractor first, not raw session files.

```bash
python3 scripts/extract_codex_user_messages.py --reverse --limit 100 --max-chars 4000 --preview-chars 180
```

2. If you need broader coverage, raise `--limit`, not `--max-chars`.

```bash
python3 scripts/extract_codex_user_messages.py --limit 5000 --max-chars 4000 --format jsonl
```

3. Derive themes from the filtered user messages only.

- root-cause expectations
- repro expectations
- debugging-method expectations
- git-safety expectations
- test-verification expectations
- diagnostic-quality expectations

4. Do not flood context with giant prompts or IDE dumps.

- only `7` oversized messages were skipped in this full-history pass
- keep doing that

## Bottom Line

Across DynLex chats, the strongest repeated instruction is this:

Act like a careful compiler debugger, not a patch bot.

Find the real cause. Make a small repro. Use the right debugging tools. Be conservative with git. Verify that tests are genuinely meaningful. Improve diagnostics when they are weak. And follow the explicit workflow the user gave you, not the workflow you wish they had given you.

For native framebuffer resizing, let the GLFW callback mark the Vulkan swapchain dirty and rebuild its dependent resources before the next drawable frame. Do not poll window size in draw code.

When a plain pattern word like `width` is implicitly promoted into a parameter because the function body uses that name, failed calls should keep the normal call-site undeduced-argument error and add related info pointing at the first body use that caused the promotion. That distinguishes accidental implicit parameters from truly missing caller variables.

SPIR-V shader uniform bindings must be explicit compile-time descriptor-set-zero locations supplied by the intrinsic. Independently compiled stages can then share stable resource locations without depending on source or code-generation order; reject one name at multiple bindings and multiple names at one binding.

Repeated plain `VariableLike` words inside one pattern definition must be tracked per concrete pattern path, not by flat leaf order. Choice alternatives like `[a|] bits bit integer` contain parallel copies of the same parameter and must keep both promotable. Only later same-path occurrences should be forced to stay literal.

One-line `section: body` syntax should be desugared before `analyzeSections`, not implemented as a second execution path. The splitter must ignore `:` inside strings and nested `()`, `[]`, and `{}`. Real directives like `alignment:` and `padding:` should be promoted into actual sections instead of keeping inline special cases. Chained one-liners represent nested sections only; sibling sections still need separate physical lines.

Avoid defining a parameter with the same plain token as a required literal word in the same pattern (for example `absolute value of value`). In larger imports this can collapse back to a literal-only path (`... of value`) and reject real arguments. Use a distinct parameter token (for example `magnitude`) or an explicit typed capture.

Wiki example actions are injected by `web/wiki/wiki-actions.js` on `.code-block pre code` snippets and target `/ide/` with `code64` (+ optional `autorun=1`) query params. Keep this shared mechanism instead of duplicating per-page button markup.

Native debugger rendering should come from the GDB pretty-printers in `scripts/gdb/dynlex_pretty_printers.py`, loaded by `.vscode/launch.json`. Do not add NatVis files or cached debug-only fields to feed debugger views.

## Agent Messaging MCP Setup

- Shared messaging MCP server path: `/home/johnheikens/Documents/Github/mcp-servers/agent_messaging/server.py`.
- Codex config must include `mcp_servers.agent_messaging` using:
  `bash -lc "cd /home/johnheikens/Documents/Github/mcp-servers/agent_messaging && exec uv run server.py"`.
- Workspace `.mcp.json` should include matching `agent_messaging` entry so project-local agents inherit the same server config.
- Runtime note: existing Codex sessions may not pick up newly-added MCP servers. Restart the session to load new server registrations.
