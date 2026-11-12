import assert from "node:assert/strict";
import net from "node:net";

const webPort = Number(process.env.ALTAIR_TEST_WEB_PORT || 18080);
const consolePort = Number(process.env.ALTAIR_TEST_CONSOLE_PORT || 18767);
const consoleSocket = net.createConnection(consolePort, "127.0.0.1");
let consoleOutput = "";
let browserOutput = "";
let browser;
consoleSocket.on("data", data => { consoleOutput += data.toString("latin1"); });

async function waitFor(getText, text, start = 0) {
  const deadline = Date.now() + 30000;
  while (!getText().slice(start).includes(text)) {
    if (Date.now() >= deadline)
      throw new Error(`Missing ${text}: ${getText()}`);
    await new Promise(resolve => setTimeout(resolve, 25));
  }
}

try {
  await new Promise((resolve, reject) => {
    consoleSocket.once("connect", resolve);
    consoleSocket.once("error", reject);
  });
  await waitFor(() => consoleOutput, "A>");
  assert.match(consoleOutput, /CP\/M 2\.2/);
  let mark = consoleOutput.length;
  consoleSocket.write("B:\r");
  await waitFor(() => consoleOutput, "B>", mark);

  browser = new WebSocket(`ws://127.0.0.1:${webPort}/ws`);
  browser.binaryType = "arraybuffer";
  browser.onmessage = event => {
    browserOutput += typeof event.data === "string"
      ? event.data : Buffer.from(event.data).toString("latin1");
  };
  await new Promise((resolve, reject) => {
    browser.onopen = resolve;
    browser.onerror = reject;
  });
  await waitFor(() => consoleOutput, "[Browser has control.]");
  await waitFor(() => browserOutput, "[Browser has control.");
  mark = consoleOutput.length;
  consoleSocket.write("D:\r");
  await new Promise(resolve => setTimeout(resolve, 200));
  browser.send(Buffer.from("\r"));
  await waitFor(() => browserOutput, "B>");
  assert.ok(!consoleOutput.slice(mark).includes("B>"), "browser output reached console");
  let browserMark = browserOutput.length;
  browser.send(Buffer.from("C:\r"));
  await waitFor(() => browserOutput, "C>", browserMark);
  await new Promise(resolve => { browser.onclose = resolve; browser.close(); });
  await waitFor(() => consoleOutput, "[Console has control.");
  mark = consoleOutput.length;
  consoleSocket.write("\r");
  await waitFor(() => consoleOutput, "C>", mark);
  mark = consoleOutput.length;
  consoleSocket.write("DIR SOUND.COM\r");
  await waitFor(() => consoleOutput, "C>", mark);
  assert.match(consoleOutput.slice(mark), /SOUND\s+COM/i);
  console.log("PASS: console boot, browser takeover, input isolation and console return in one CP/M session");
} finally {
  if (browser && browser.readyState === WebSocket.OPEN) browser.close();
  consoleSocket.destroy();
}
