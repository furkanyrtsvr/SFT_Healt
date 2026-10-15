import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

async function render() {
  const workerUrl = new URL("../dist/server/index.js", import.meta.url);
  workerUrl.searchParams.set("test", `${process.pid}-${Date.now()}`);
  const { default: worker } = await import(workerUrl.href);

  return worker.fetch(
    new Request("http://localhost/", { headers: { accept: "text/html" } }),
    { ASSETS: { fetch: async () => new Response("Not found", { status: 404 }) } },
    { waitUntil() {}, passThroughOnException() {} },
  );
}

test("SFT Health sayfasını sunucu tarafında oluşturur", async () => {
  const response = await render();
  assert.equal(response.status, 200);
  assert.match(response.headers.get("content-type") ?? "", /^text\/html\b/i);

  const html = await response.text();
  assert.match(html, /<title>SFT Health \| Solunum ve Nabız Takibi<\/title>/i);
  assert.match(html, /Solunum ve nabız takibi/);
  assert.match(html, /Ölçülen MEP/);
  assert.match(html, /Ölçülen MIP/);
  assert.match(html, /ODA ORTAMI/);
  assert.match(html, /Ölçüm geçmişi/);
  assert.match(html, /Hastane kayıt/);
  assert.match(html, /Demo hasta akışı/);
  assert.doesNotMatch(html, /codex-preview|react-loading-skeleton/i);
});

test("Nordic UART Service ve sağlık senkronizasyonunu içerir", async () => {
  const page = await readFile(new URL("../app/health-dashboard.tsx", import.meta.url), "utf8");

  assert.match(page, /6e400001-b5a3-f393-e0a9-e50e24dcca9e/);
  assert.match(page, /6e400002-b5a3-f393-e0a9-e50e24dcca9e/);
  assert.match(page, /6e400003-b5a3-f393-e0a9-e50e24dcca9e/);
  assert.match(page, /sendCommand\("SYNC"\)/);
  assert.match(page, /writeValueWithoutResponse/);
  assert.match(page, /startNotifications/);
  assert.match(page, /speechSynthesis/);
  assert.match(page, /fields\[1\] === "MIP"/);
  assert.match(page, /fields\[1\] === "ENV"/);
  assert.match(page, /fields\[0\] === "R"/);
  assert.doesNotMatch(page, /sendCommand\(`TIME\|/);
  assert.match(page, /PROFILE_SET\|/);
  assert.match(page, /PROFILE_SELECT\|/);
  assert.match(page, /PROFILE_DELETE\|/);
  assert.match(page, /SESSION_END/);
  assert.match(page, /BarcodeDetector/);
});
