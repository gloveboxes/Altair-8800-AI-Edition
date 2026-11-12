import assert from "node:assert/strict";
import net from "node:net";

const webPort = Number(process.env.ALTAIR_TEST_WEB_PORT || 18080);
const soundPort = Number(process.env.ALTAIR_TEST_SOUND_PORT || 18766);
let terminal = new WebSocket(`ws://127.0.0.1:${webPort}/ws`);
terminal.binaryType = "arraybuffer";
let output = "";
let changed;
terminal.addEventListener("message", event => {
  output += typeof event.data === "string" ? event.data : Buffer.from(event.data).toString("latin1");
  if (changed) changed();
});
terminal.addEventListener("error", event => console.error("WebSocket error", event));

async function waitText(text, start = 0, timeout = 30000) {
  const deadline = Date.now() + timeout;
  while (!output.slice(start).includes(text)) {
    if (Date.now() >= deadline) throw new Error(`Missing ${text}: ${output}`);
    await new Promise(resolve => {
      changed = resolve;
      setTimeout(resolve, 100);
    });
  }
  changed = null;
}

await waitText("A>");
assert.match(output, /CP\/M 2\.2/);
if (!process.env.ALTAIR_TEST_NO_AUDIO) {
  const audio = net.createConnection(soundPort, "127.0.0.1");
  await new Promise((resolve, reject) => {
    audio.once("connect", resolve);
    audio.once("error", reject);
  });
  let nonzero = false;
  let remainder = Buffer.alloc(0);
  audio.on("data", chunk => {
    const data = Buffer.concat([remainder, chunk]);
    const count = data.length - data.length % 4;
    for (let i = 0; i < count; i += 4) {
      const value = data.readFloatLE(i);
      assert.ok(Number.isFinite(value) && Math.abs(value) <= 1);
      if (Math.abs(value) > 0.001) nonzero = true;
    }
    remainder = data.subarray(count);
  });
  let start = output.length;
  terminal.send(Buffer.from("C:\r"));
  await waitText("C>", start);
  start = output.length;
  terminal.send(Buffer.from("SOUND\r"));
  await waitText("SOUND", start);
  const deadline = Date.now() + 15000;
  while (!nonzero && Date.now() < deadline)
    await new Promise(resolve => setTimeout(resolve, 50));
  assert.ok(nonzero, `speaker produced no nonzero audio; terminal: ${output}`);
  start = output.length;
  terminal.send(Buffer.from("x"));
  await waitText("C>", start);
  audio.destroy();
  console.log("PASS: CP/M boot, browser input and nonzero SBC speaker PCM");
}
if (process.env.ALTAIR_TEST_DISK_WRITE) {
  let start = output.length;
  terminal.send(Buffer.from("D:\r"));
  await waitText("D>", start);
  start = output.length;
  terminal.send(Buffer.from("SAVE 1 PERSIST.COM\r"));
  await waitText("D>", start);
  assert.doesNotMatch(output.slice(start), /ERROR|BAD|READ ONLY/i);
  console.log("PASS: saved test file to mounted drive D");
}
terminal.close();
