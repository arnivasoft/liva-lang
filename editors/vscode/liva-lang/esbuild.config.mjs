import * as esbuild from "esbuild";

const production = process.argv.includes("--production");
const watch = process.argv.includes("--watch");

/** @type {import("esbuild").BuildOptions} */
const buildOptions = {
    entryPoints: ["src/extension.ts"],
    bundle: true,
    outfile: "out/extension.js",
    platform: "node",
    format: "cjs",
    external: ["vscode"],
    sourcemap: !production,
    minify: production,
    target: "node18",
};

async function main() {
    if (watch) {
        const ctx = await esbuild.context(buildOptions);
        await ctx.watch();
        console.log("[esbuild] watching for changes...");
    } else {
        await esbuild.build(buildOptions);
        console.log("[esbuild] build complete");
    }
}

main().catch((e) => {
    console.error(e);
    process.exit(1);
});
