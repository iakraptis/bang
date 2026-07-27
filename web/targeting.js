(function (root, factory) {
  "use strict";

  const api = factory();
  if (typeof module === "object" && module.exports) {
    module.exports = api;
  } else {
    root.BangTargeting = api;
  }
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";

  function effectiveDropZone(target, game) {
    if (typeof target !== "string" || !target.endsWith("_rearguard")) {
      return target;
    }

    const ownSide = target.startsWith("own_");
    const playerIndex = ownSide ? game?.humanPlayer : 1 - game?.humanPlayer;
    const player = game?.players?.[playerIndex];
    const rearguard = player?.positions?.find(position => position.role === "rearguard");

    // The engine resolves a rearguard target to the active character when
    // that side is down to one survivor. Reflect that effective target in
    // the drop zone rather than highlighting the fallen character's slot.
    if (rearguard && (!rearguard.character || rearguard.life <= 0)) {
      return `${ownSide ? "own" : "opponent"}_active`;
    }
    return target;
  }

  return { effectiveDropZone };
});
