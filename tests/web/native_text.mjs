// C runtime text streams expand LF to CRLF on Windows. Compare their logical
// lines while retaining every other character, including lone carriage returns.
export function nativeText(output) {
  return output.replaceAll('\r\n', '\n');
}
