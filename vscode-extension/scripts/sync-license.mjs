import { copyFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const extensionDir = path.resolve(scriptDir, "..");
const rootDir = path.resolve(extensionDir, "..");

copyFileSync(
    path.join(rootDir, "LICENSE.md"),
    path.join(extensionDir, "LICENSE.md"),
);
