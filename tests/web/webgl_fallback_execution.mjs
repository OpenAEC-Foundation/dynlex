import { closeBrowserSession } from "./browser_test_driver.mjs";
import { verifyWebGlFallback } from "./webgl_fallback_browser.mjs";

await verifyWebGlFallback();
await verifyWebGlFallback({ adapterUnavailable: true });
await closeBrowserSession();
console.log("Homepage shaders, transitions and IDE use WebGL2 with no WebGPU API or no adapter.");
