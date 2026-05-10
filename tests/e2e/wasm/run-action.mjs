import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "@playwright/test";

const MIME_TYPES = new Map([
  [".css", "text/css; charset=utf-8"],
  [".html", "text/html; charset=utf-8"],
  [".js", "application/javascript"],
  [".json", "application/json"],
  [".mjs", "application/javascript"],
  [".svg", "image/svg+xml"],
  [".wasm", "application/wasm"]
]);

const COI_HEADERS = {
  "Cross-Origin-Embedder-Policy": "require-corp",
  "Cross-Origin-Opener-Policy": "same-origin"
};

function usage() {
  console.error("Usage: node run-action.mjs <app-html-or-dir> -- <e2e-app-args...>");
  process.exit(2);
}

function parseArgs() {
  const args = process.argv.slice(2);
  const separator = args.indexOf("--");
  if (args.length < 3 || separator < 1) {
    usage();
  }
  return {
    appPath: path.resolve(args[0]),
    appArgs: args.slice(separator + 1)
  };
}

function discoverApp(inputPath) {
  const stat = fs.statSync(inputPath);
  const appDir = stat.isDirectory() ? inputPath : path.dirname(inputPath);
  const htmlPath = stat.isDirectory()
    ? fs.readdirSync(appDir)
        .filter((file) => file.endsWith(".html"))
        .map((file) => path.join(appDir, file))
        .find((file) => fs.readFileSync(file, "utf8").includes("qtLoad("))
    : inputPath;

  if (!htmlPath || !fs.existsSync(htmlPath)) {
    throw new Error(`Could not find a Qt wasm HTML file in ${appDir}`);
  }

  const html = fs.readFileSync(htmlPath, "utf8");
  const entryMatch = html.match(/entryFunction:\s*window\.([A-Za-z0-9_]+)/);
  const scriptMatch = html.match(/<script\b[^>]*\bsrc="([^"]+\.js)"/);
  if (!entryMatch || !scriptMatch) {
    throw new Error(`Could not read Qt wasm entry metadata from ${htmlPath}`);
  }

  return {
    appDir,
    entryFunction: entryMatch[1],
    scriptFile: scriptMatch[1]
  };
}

function sentryBundlePath() {
  const configuredPath = process.env.SENTRY_QML_E2E_WASM_SENTRY_BUNDLE;
  if (configuredPath) {
    return path.resolve(configuredPath);
  }
  return path.join(path.dirname(fileURLToPath(import.meta.url)), "dist", "sentry-browser.js");
}

function testHtml(app, appArgs) {
  return `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, height=device-height, user-scalable=0">
  <title>Sentry QML wasm E2E</title>
  <style>
    html, body, #screen { height: 100%; margin: 0; overflow: hidden; width: 100%; }
  </style>
</head>
<body>
  <div id="screen"></div>
  <script src="/__sentry-sdk.js"></script>
  <script src="/${app.scriptFile}"></script>
  <script src="/qtloader.js"></script>
  <script>
    qtLoad({
      arguments: ${JSON.stringify(appArgs)},
      print: function (text) { console.log(text); },
      printErr: function (text) { console.error(text); },
      qt: {
        entryFunction: window.${app.entryFunction},
        containerElements: [document.getElementById("screen")],
        onExit: function (exitData) {
          console.log("QT_EXIT: " + JSON.stringify(exitData));
        }
      }
    }).catch(function (error) {
      console.error(error && error.stack ? error.stack : String(error));
    });
  </script>
</body>
</html>`;
}

function startServer(app, appArgs, bundlePath) {
  const appRoot = app.appDir + path.sep;
  const html = testHtml(app, appArgs);

  const server = http.createServer((request, response) => {
    const url = new URL(request.url || "/", "http://127.0.0.1");
    let filePath = null;

    if (url.pathname === "/" || url.pathname === "/__sentry-qml-e2e.html") {
      response.writeHead(200, { ...COI_HEADERS, "Content-Type": "text/html; charset=utf-8" });
      response.end(html);
      return;
    }

    if (url.pathname === "/__sentry-sdk.js") {
      filePath = bundlePath;
    } else {
      filePath = path.resolve(app.appDir, decodeURIComponent(url.pathname.slice(1)));
      if (!filePath.startsWith(appRoot)) {
        response.writeHead(403, COI_HEADERS);
        response.end("Forbidden");
        return;
      }
    }

    try {
      if (!fs.statSync(filePath).isFile()) {
        throw new Error("not a file");
      }
    } catch {
      response.writeHead(404, COI_HEADERS);
      response.end("Not Found");
      return;
    }

    response.writeHead(200, {
      ...COI_HEADERS,
      "Content-Type": MIME_TYPES.get(path.extname(filePath)) || "application/octet-stream",
      "Cross-Origin-Resource-Policy": "same-origin"
    });
    fs.createReadStream(filePath).pipe(response);
  });

  return new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => resolve(server));
  });
}

function waitForResult(page, timeoutMs) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("Timed out waiting for TEST_RESULT")), timeoutMs);
    page.on("console", (message) => {
      const text = message.text();
      console.log(text);
      const marker = "TEST_RESULT: ";
      const markerIndex = text.indexOf(marker);
      if (markerIndex >= 0) {
        clearTimeout(timer);
        resolve(text.slice(markerIndex + marker.length));
      }
    });
    page.on("pageerror", (error) => console.log(`[PAGE ERROR] ${error.message}`));
  });
}

async function main() {
  const { appPath, appArgs } = parseArgs();
  const app = discoverApp(appPath);
  const bundlePath = sentryBundlePath();
  if (!fs.existsSync(bundlePath)) {
    throw new Error(`Sentry browser bundle does not exist: ${bundlePath}`);
  }

  const server = await startServer(app, appArgs, bundlePath);
  const { port } = server.address();
  const browser = await chromium.launch({
    headless: true,
    args: ["--no-sandbox", "--use-gl=swiftshader"]
  });

  try {
    const page = await (await browser.newContext()).newPage();
    const resultPromise = waitForResult(page, 120000);
    await page.goto(`http://127.0.0.1:${port}/__sentry-qml-e2e.html`);
    const resultText = await resultPromise;
    await page.evaluate(async () => {
      const bridge = globalThis.__sentryQmlWasmBridge;
      if (bridge && bridge.pending) {
        await bridge.pending;
      }
    });
    await page.waitForTimeout(1000);

    const result = JSON.parse(resultText);
    process.exitCode = result.success ? 0 : 1;
  } finally {
    await browser.close();
    server.close();
  }
}

main().catch((error) => {
  console.error(error && error.stack ? error.stack : String(error));
  process.exit(2);
});
