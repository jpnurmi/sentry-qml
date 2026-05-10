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
  console.error("Usage: node run-qttest.mjs <test-js> -- <qttest-args...>");
  process.exit(2);
}

function parseArgs() {
  const args = process.argv.slice(2);
  const separator = args.indexOf("--");
  if (args.length < 3 || separator !== 1) {
    usage();
  }
  const testPath = path.resolve(args[0]);
  if (!fs.existsSync(testPath) || path.extname(testPath) !== ".js") {
    throw new Error(`Qt wasm test JavaScript does not exist: ${testPath}`);
  }
  return {
    testPath,
    testArgs: args.slice(separator + 1)
  };
}

function sentryBundlePath() {
  const configuredPath = process.env.SENTRY_QML_WASM_SENTRY_BUNDLE;
  if (configuredPath) {
    return path.resolve(configuredPath);
  }
  return path.join(path.dirname(fileURLToPath(import.meta.url)), "dist", "sentry-browser.js");
}

function testHtml(testFile, testArgs, hasSentryBundle) {
  const sentryScript = hasSentryBundle ? '<script src="/__sentry-sdk.js"></script>' : "";
  return `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, height=device-height, user-scalable=0">
  <title>${testFile}</title>
  <style>
    html, body, #screen { height: 100%; margin: 0; overflow: hidden; width: 100%; }
    #canvas { height: 100%; width: 100%; }
  </style>
</head>
<body>
  <div id="screen"><canvas id="canvas" oncontextmenu="event.preventDefault()" tabindex="-1"></canvas></div>
  ${sentryScript}
  <script>
    var Module = {
      arguments: ${JSON.stringify(testArgs)},
      canvas: document.getElementById("canvas"),
      print: function (text) { console.log(text); },
      printErr: function (text) { console.error(text); },
      onExit: function (status) { console.log("QT_EXIT: " + JSON.stringify(status)); },
      setStatus: function (text) { if (text) { console.log("QT_STATUS: " + text); } }
    };
  </script>
  <script src="/${testFile}"></script>
</body>
</html>`;
}

function startServer(testPath, testArgs, bundlePath) {
  const appDir = path.dirname(testPath);
  const appRoot = appDir + path.sep;
  const hasSentryBundle = fs.existsSync(bundlePath);
  const html = testHtml(path.basename(testPath), testArgs, hasSentryBundle);

  const server = http.createServer((request, response) => {
    const url = new URL(request.url || "/", "http://127.0.0.1");
    let filePath = null;

    if (url.pathname === "/" || url.pathname === "/__sentry-qml-qttest.html") {
      response.writeHead(200, { ...COI_HEADERS, "Content-Type": "text/html; charset=utf-8" });
      response.end(html);
      return;
    }

    if (url.pathname === "/__sentry-sdk.js") {
      filePath = bundlePath;
    } else {
      filePath = path.resolve(appDir, decodeURIComponent(url.pathname.slice(1)));
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

function waitForTotals(page, timeoutMs) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("Timed out waiting for QtTest totals")), timeoutMs);
    page.on("console", (message) => {
      const text = message.text();
      console.log(text);
      const match = text.match(/^Totals: (\d+) passed, (\d+) failed, (\d+) skipped, (\d+) blacklisted/);
      if (match) {
        clearTimeout(timer);
        resolve({
          passed: Number(match[1]),
          failed: Number(match[2]),
          skipped: Number(match[3]),
          blacklisted: Number(match[4])
        });
      }
    });
    page.on("pageerror", (error) => console.log(`[PAGE ERROR] ${error.message}`));
  });
}

async function main() {
  const { testPath, testArgs } = parseArgs();
  const server = await startServer(testPath, testArgs, sentryBundlePath());
  const { port } = server.address();
  const browser = await chromium.launch({
    headless: true,
    args: ["--no-sandbox", "--use-gl=swiftshader"]
  });

  try {
    const page = await (await browser.newContext()).newPage();
    const totalsPromise = waitForTotals(page, 120000);
    await page.goto(`http://127.0.0.1:${port}/__sentry-qml-qttest.html`);
    const totals = await totalsPromise;
    process.exitCode = totals.failed === 0 ? 0 : 1;
  } finally {
    await browser.close();
    server.close();
  }
}

main().catch((error) => {
  console.error(error && error.stack ? error.stack : String(error));
  process.exit(2);
});
