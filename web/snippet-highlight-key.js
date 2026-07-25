export async function semanticHighlightKey(sourceText) {
  if (!globalThis.crypto?.subtle) {
    throw new Error("SHA-256 support is required for semantic-highlight cache keys");
  }
  const sourceBytes = new TextEncoder().encode(sourceText);
  const digest = await globalThis.crypto.subtle.digest("SHA-256", sourceBytes);
  return [...new Uint8Array(digest)]
    .map((byte) => byte.toString(16).padStart(2, "0"))
    .join("");
}
