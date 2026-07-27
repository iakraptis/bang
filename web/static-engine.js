(() => {
  "use strict";

  const script = document.currentScript;
  const assetVersion = script?.dataset.assetVersion || "";
  const policyPath = script?.dataset.policyPath || "";
  let engineReady = null;

  function parseResult(text) {
    const payload = JSON.parse(text);
    if (payload?.error) throw new Error(payload.error);
    return payload;
  }

  async function loadEngine() {
    if (!engineReady) {
      engineReady = (async () => {
        if (typeof window.createBangStaticModule !== "function") {
          throw new Error("Static game engine is not loaded.");
        }
        const options = {};
        if (assetVersion) {
          options.locateFile = (path, prefix) => {
            const url = `${prefix || ""}${path}`;
            return path === "bang_static_web.wasm" ||
              path === "bang_static_web.data"
              ? `${url}?v=${encodeURIComponent(assetVersion)}`
              : url;
          };
        }
        const module = await window.createBangStaticModule(options);
        const engine = {
          init: module.cwrap(
              "bang_static_web_init", "string", ["string"]),
          newGame: module.cwrap(
              "bang_static_web_new_game", "string",
              ["number", "number", "number"]),
          currentState: module.cwrap(
              "bang_static_web_current_state", "string", ["string"]),
          applyAction: module.cwrap(
              "bang_static_web_apply_action", "string", ["string", "number"]),
        };
        parseResult(engine.init(policyPath));
        return engine;
      })();
    }
    return engineReady;
  }

  window.bangStaticEngine = {
    async request(path, options = {}) {
      const engine = await loadEngine();
      const method = String(options.method || "GET").toUpperCase();
      const body = options.body ? JSON.parse(options.body) : {};

      if (method === "POST" && path === "/api/new") {
        return parseResult(engine.newGame(
            Number(body.seed || 0),
            Number.isFinite(Number(body.humanPlayer))
              ? Number(body.humanPlayer)
              : -1,
            Number.isFinite(Number(body.charactersPerSide))
              ? Number(body.charactersPerSide)
              : -1));
      }

      const action = path.match(/^\/api\/session\/([^/]+)\/action$/);
      if (method === "POST" && action) {
        return parseResult(engine.applyAction(
            decodeURIComponent(action[1]), Number(body.action)));
      }

      const session = path.match(/^\/api\/session\/([^/]+)$/);
      if (method === "GET" && session) {
        return parseResult(
            engine.currentState(decodeURIComponent(session[1])));
      }

      throw new Error(`Unsupported static engine endpoint: ${method} ${path}`);
    },
  };
})();
