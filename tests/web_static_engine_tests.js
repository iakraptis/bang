"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const calls = [];
let locateFile;
const responses = {
  bang_static_web_init: (...args) => {
    calls.push(["init", ...args]);
    return JSON.stringify({ ok: true });
  },
  bang_static_web_new_game: (...args) => {
    calls.push(["new", ...args]);
    return JSON.stringify({ sessionId: "abc" });
  },
  bang_static_web_current_state: (...args) => {
    calls.push(["get", ...args]);
    return JSON.stringify({ sessionId: args[0] });
  },
  bang_static_web_apply_action: (...args) => {
    calls.push(["action", ...args]);
    return JSON.stringify({ sessionId: args[0], action: args[1] });
  },
};

const window = {
  createBangStaticModule: async options => {
    locateFile = options.locateFile;
    return {
      cwrap(name) {
        assert.equal(typeof responses[name], "function");
        return (...args) => responses[name](...args);
      },
    };
  },
};
const context = vm.createContext({
  document: {
    currentScript: {
      dataset: {
        assetVersion: "build 1",
        policyPath: "/static-policy.json",
      },
    },
  },
  window,
});
vm.runInContext(
  fs.readFileSync(require.resolve("../web/static-engine.js"), "utf8"),
  context,
);

(async () => {
  const game = await window.bangStaticEngine.request("/api/new", {
    method: "POST",
    body: JSON.stringify({
      seed: 17,
      humanPlayer: 1,
      charactersPerSide: 6,
    }),
  });
  assert.equal(game.sessionId, "abc");
  assert.equal(
    locateFile("bang_static_web.wasm", "/assets/"),
    "/assets/bang_static_web.wasm?v=build%201",
  );
  assert.equal(
    locateFile("bang_static_web.data", "/assets/"),
    "/assets/bang_static_web.data?v=build%201",
  );

  const current =
    await window.bangStaticEngine.request("/api/session/a%20b");
  assert.equal(current.sessionId, "a b");
  const next =
    await window.bangStaticEngine.request("/api/session/abc/action", {
      method: "POST",
      body: JSON.stringify({ action: 42 }),
    });
  assert.equal(next.sessionId, "abc");
  assert.equal(next.action, 42);
  assert.deepEqual(calls, [
    ["init", "/static-policy.json"],
    ["new", 17, 1, 6],
    ["get", "a b"],
    ["action", "abc", 42],
  ]);

  await assert.rejects(
    window.bangStaticEngine.request("/api/health"),
    /Unsupported static engine endpoint/,
  );

  responses.bang_static_web_current_state = () =>
    JSON.stringify({ error: "unknown session" });
  await assert.rejects(
    window.bangStaticEngine.request("/api/session/missing"),
    /unknown session/,
  );

  console.log("static web engine bridge tests passed");
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
