# macOS gdb setup for DynLex

`lldb` works out of the box on macOS, but if you want `gdb` you need a one-time code-sign step.

## 1) Install gdb

```bash
brew install gdb
```

## 2) Create a signing certificate (one-time, manual in Keychain)

1. Open `Keychain Access`
2. `Certificate Assistant` -> `Create a Certificate...`
3. Name: `gdb-cert`
4. Identity Type: `Self Signed Root`
5. Certificate Type: `Code Signing`
6. Check `Let me override defaults` and finish
7. Double-click the certificate and set `Trust` -> `Code Signing` -> `Always Trust`

## 3) Sign gdb

```bash
codesign --entitlements scripts/macos/gdb-entitlements.plist -fs gdb-cert "$(command -v gdb)"
```

## 4) Allow terminal as Developer Tool

Open `System Settings` -> `Privacy & Security` -> `Developer Tools` and enable your terminal app.

## 5) Debug specificity quickly

```bash
./scripts/debug_specificity_gdb.sh
```

That script builds (debug) and starts:

```text
gdb --args ./build/dynlex tests/required/specificity/main.dl -o /tmp/specificity.out
```
