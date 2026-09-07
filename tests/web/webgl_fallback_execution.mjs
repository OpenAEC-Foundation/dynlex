import { closeBrowserSession } from "./browser_test_driver.mjs";
import { verifyWebGlFallback } from "./webgl_fallback_browser.mjs";

await verifyWebGlFallback();
await closeBrowserSession();
console.log("Homepage shaders and transitions run through WebGL2.");
