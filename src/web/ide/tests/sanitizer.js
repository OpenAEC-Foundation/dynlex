import purify from "monaco-editor/base/browser/dompurify/dompurify.js";
import { sanitizeHtml } from "monaco-editor/base/browser/domSanitize.js";

globalThis.sanitizer = { purify, sanitizeHtml };
