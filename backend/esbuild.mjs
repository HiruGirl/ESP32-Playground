import { build } from "esbuild";
import { mkdirSync } from "node:fs";

mkdirSync("dist", { recursive: true });
await build({
  entryPoints: ["src/server.ts"],
  bundle: true,
  platform: "node",
  target: "node20",
  format: "esm",
  outfile: "dist/server.js",
  external: ["express", "ws"],
  banner: {
    js: 'import{createRequire as __cr}from"node:module";const require=__cr(import.meta.url);',
  },
});
console.log("backend built: dist/server.js");
