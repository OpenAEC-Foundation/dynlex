# LEXI-8

A complete fantasy-console stack, four languages deep, written entirely in
DynLex:

1. **`assembler.dl`** — a natural-language CHIP-8 assembler. Every mnemonic is
   an ordinary DynLex pattern that emits bytecode:
   `load value 28 into register 3`,
   `skip when key in register 0 is down`, `jump to label "main loop"`.
   Labels resolve forward
   and backward through a fixup table, and sprites are authored as pictures:

   ```text
   label "paddle sprite"
   emit a data row "XXXXXX.."
   ```

2. **`breakout.dl`** — a full Breakout game written in that assembly dialect:
   sixty four bricks, paddle, ball physics with directional wall bounces,
   score keeping via BCD and the built in hex font, three lives, and GAME
   OVER / YOU WIN screens drawn with custom letter sprites. It assembles to a
   424 byte ROM that would run on any real CHIP-8 interpreter.

3. **`chip8.dl`** — the console itself: a complete CHIP-8 virtual machine
   with 4096 bytes of memory, sixteen registers, a call stack, delay and
   sound timers, a sixteen key pad and XOR sprite drawing with collision
   detection in register 15. Unknown opcodes halt the machine with a fault
   report.

4. **`main.dl`** — the CRT. Every emulated pixel becomes an amber phosphor
   dot with a glow halo that keeps fading for a few frames after it turns
   off, so the ball drags a light trail behind it. The HUD reads the score
   and lives live out of the emulated registers while the game runs.

## Build and run

From the repository root:

```bash
./build/dynlex tests/games/lexi8/main.dl -o tests/games/lexi8/lexi8
./tests/games/lexi8/lexi8
```

Left/right arrows (or A and D) move the paddle. R reboots the console.
Escape quits.

## Test

`test.dl` assembles the ROM and runs the machine headlessly with seeded
randomness: it checks the ROM size and entry opcode, counts lit pixels after
boot (64 bricks x 3 + 6 paddle + 1 ball = 199), drives the paddle with
simulated key presses to both walls, lets the game play itself, and verifies
the invariant `score + remaining bricks = 64` all the way to the game over
screen (whose 136 lit pixels are the letter sprites plus the two score
digits).

```bash
./build/dynlex tests/games/lexi8/test.dl -o tests/games/lexi8/test.out
./tests/games/lexi8/test.out | diff - tests/games/lexi8/expected.txt
```
