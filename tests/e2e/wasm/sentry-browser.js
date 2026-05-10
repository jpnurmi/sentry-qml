import * as Browser from "@sentry/browser";
import { wasmIntegration } from "@sentry/wasm";

globalThis.Sentry = Object.assign({}, Browser, { wasmIntegration });
