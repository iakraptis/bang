(() => {
  "use strict";

  const app = document.querySelector("#app");
  const modalRoot = document.querySelector("#modal-root");
  const dragRoot = document.querySelector("#drag-root");
  const fxRoot = document.querySelector("#fx-root");
  const stageRoot = document.querySelector("#stage-root");
  const toastEl = document.querySelector("#toast");
  const tipEl = document.querySelector("#tooltip");
  const { effectiveDropZone } = window.BangTargeting;
  const { planPlays, playCaption, motifFor, characterEvents, seatedCharacters,
          TIMING: FX_TIMING } = window.BangCardFx;

  let game = null;
  let busy = false;
  let selectedCard = null;
  let toastTimer = null;
  let logOpen = false;
  let renderedGame = null; // the state the current DOM was built from
  let autoPass = null; // { id } of the decision being auto-applied
  let autoPassTimer = null;
  let endTurnSpotlight = false; // no card/character plays left → spotlight end turn
  let hoverPreview = null; // card instance hovered for drop-zone preview
  let fxBusyUntil = 0; // performance.now() the running play ceremonies finish at
  let stagingTimer = null; // clears the class that steps the phase pill aside
  const reducedMotion = window.matchMedia?.("(prefers-reduced-motion: reduce)").matches ?? false;
  /* A finger is not a small cursor. It cannot hover, so nothing may hide
     behind hover; it cannot drag without also scrolling whatever is under it,
     so the hand rail and drag-to-play cannot both own the same gesture; and it
     is imprecise, so a tap that wandered is a scroll rather than a click.
     Everything downstream of those three facts keys off this. */
  const touchFirst = window.matchMedia?.("(pointer: coarse)").matches ?? false;

  const revealIcons = { barrel: "◉", snake: "ϟ", dynamite: "✹", horseshoe: "Ω" };
  const sideClass = side => String(side || "").toLowerCase();
  const cardImage = key =>
    new URL(`cards/${encodeURIComponent(String(key || ""))}.png`, document.baseURI).href;
  const escapeHtml = value => String(value ?? "")
    .replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;").replaceAll("'", "&#039;");

  /* ---------- icons (inline SVG, stroke inherits currentColor) ---------- */
  const svg = body => `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
    stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${body}</svg>`;
  const ICONS = {
    end: svg('<path d="M5 5l9 7-9 7z"/><path d="M18 5v14"/>'),
    swap: svg('<path d="M7 8h11l-3.2-3.2M17 16H6l3.2 3.2"/>'),
    hit: svg('<path d="M12 3l1.8 4.6L18 5l-1.4 4.2L21 11l-4.1 1.9L18.5 18l-4.4-1.5L12 21l-2.1-4.5L5.5 18 7 12.9 3 11l4.4-1.8L6 5l4.2 2.6z"/>'),
    check: svg('<path d="M4.5 12.5l5 5L19.5 7"/>'),
    cross: svg('<path d="M6 6l12 12M18 6L6 18"/>'),
    log: svg('<path d="M6 4h12v16H6z"/><path d="M9 8h6M9 12h6M9 16h4"/>'),
    about: svg('<circle cx="12" cy="12" r="9"/><path d="M12 11v6"/><path d="M12 7h.01"/>'),
    plus: svg('<path d="M12 5v14M5 12h14"/>'),
    close: svg('<path d="M6 6l12 12M18 6L6 18"/>'),
    sound: svg('<path d="M4 9.5v5h3.5L12 18.5v-13L7.5 9.5H4z"/><path d="M15 9a4.2 4.2 0 010 6"/><path d="M17.5 6.5a7.6 7.6 0 010 11"/>'),
    mute: svg('<path d="M4 9.5v5h3.5L12 18.5v-13L7.5 9.5H4z"/><path d="M15.5 9.5l5 5M20.5 9.5l-5 5"/>'),
  };

  async function api(path, options = {}, retries = 6) {
    if (window.bangStaticEngine?.request) {
      return await window.bangStaticEngine.request(path, options);
    }
    for (let attempt = 0; ; attempt += 1) {
      let response;
      try {
        response = await fetch(path, {
          ...options,
          headers: { "Content-Type": "application/json", ...(options.headers || {}) },
        });
      } catch (error) {
        // Network failure — the server may be restarting to load a fresh AI
        // checkpoint. Wait it out instead of stranding the player.
        if (attempt < retries) { await sleep(800); continue; }
        throw new Error("The table is not answering. Check the connection.");
      }
      if ((response.status === 502 || response.status === 503) && attempt < retries) {
        await sleep(800); continue;
      }
      let payload;
      try { payload = await response.json(); } catch { payload = {}; }
      if (!response.ok) throw new Error(payload.error || `Request failed (${response.status})`);
      return payload;
    }
  }
  const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

  /* ---------- sound effects (synthesized WebAudio, no assets) ---------- */
  const sfx = (() => {
    let ctx = null;
    let master = null;
    let noiseBuffer = null;
    let muted = localStorage.getItem("bangDuelMuted") === "1";

    function ensure() {
      if (muted) return null;
      if (!ctx) {
        const AC = window.AudioContext || window.webkitAudioContext;
        if (!AC) return null;
        ctx = new AC();
        master = ctx.createGain();
        master.gain.value = 0.45;
        master.connect(ctx.destination);
        noiseBuffer = ctx.createBuffer(1, ctx.sampleRate, ctx.sampleRate);
        const data = noiseBuffer.getChannelData(0);
        for (let i = 0; i < data.length; i += 1) data[i] = Math.random() * 2 - 1;
      }
      if (ctx.state === "suspended") ctx.resume();
      return ctx;
    }

    // A tone with an optional pitch glide and an exponential decay.
    function tone({ type = "sine", from = 440, to = from, at = 0, duration = 0.2, volume = 0.5 }) {
      const context = ensure();
      if (!context) return;
      const t0 = context.currentTime + at;
      const osc = context.createOscillator();
      const gain = context.createGain();
      osc.type = type;
      osc.frequency.setValueAtTime(Math.max(20, from), t0);
      if (to !== from) osc.frequency.exponentialRampToValueAtTime(Math.max(20, to), t0 + duration);
      gain.gain.setValueAtTime(0.0001, t0);
      gain.gain.exponentialRampToValueAtTime(volume, t0 + 0.012);
      gain.gain.exponentialRampToValueAtTime(0.0001, t0 + duration);
      osc.connect(gain).connect(master);
      osc.start(t0);
      osc.stop(t0 + duration + 0.05);
    }

    // A filtered noise burst (gunshots, whooshes, booms).
    function noise({ at = 0, duration = 0.3, volume = 0.5, type = "lowpass", from = 1200, to = from, q = 0.8 }) {
      const context = ensure();
      if (!context) return;
      const t0 = context.currentTime + at;
      const source = context.createBufferSource();
      source.buffer = noiseBuffer;
      source.loop = true;
      const filter = context.createBiquadFilter();
      filter.type = type;
      filter.Q.value = q;
      filter.frequency.setValueAtTime(from, t0);
      if (to !== from) filter.frequency.exponentialRampToValueAtTime(Math.max(30, to), t0 + duration);
      const gain = context.createGain();
      gain.gain.setValueAtTime(0.0001, t0);
      gain.gain.exponentialRampToValueAtTime(volume, t0 + 0.015);
      gain.gain.exponentialRampToValueAtTime(0.0001, t0 + duration);
      source.connect(filter).connect(gain).connect(master);
      source.start(t0);
      source.stop(t0 + duration + 0.05);
    }

    const effects = {
      shot(at = 0) {
        noise({ at, duration: 0.22, volume: 0.9, from: 3200, to: 300 });
        tone({ at, type: "triangle", from: 160, to: 45, duration: 0.18, volume: 0.7 });
      },
      hit(at = 0) {
        tone({ at, from: 110, to: 55, duration: 0.16, volume: 0.6 });
        noise({ at, duration: 0.1, volume: 0.35, from: 900, to: 250 });
      },
      missed(at = 0) { noise({ at, duration: 0.3, volume: 0.4, type: "bandpass", from: 500, to: 2600, q: 1.4 }); },
      heal(at = 0) {
        tone({ at, from: 520, duration: 0.12, volume: 0.28 });
        tone({ at: at + 0.09, from: 780, duration: 0.16, volume: 0.28 });
      },
      draw(at = 0) { noise({ at, duration: 0.12, volume: 0.2, type: "highpass", from: 1400, to: 2600 }); },
      flip(at = 0) { tone({ at, type: "square", from: 950, duration: 0.045, volume: 0.16 }); },
      boom(at = 0) {
        tone({ at, from: 75, to: 28, duration: 0.7, volume: 0.9 });
        noise({ at, duration: 0.55, volume: 0.7, from: 420, to: 60 });
      },
      rattle(at = 0) {
        for (let i = 0; i < 6; i += 1) noise({ at: at + i * 0.05, duration: 0.03, volume: 0.28, type: "bandpass", from: 2800, q: 3 });
      },
      death(at = 0) {
        tone({ at, from: 210, to: 52, duration: 0.75, volume: 0.5 });
        noise({ at, duration: 0.25, volume: 0.28, from: 500, to: 120 });
      },
      turn(at = 0) {
        tone({ at, from: 660, duration: 0.07, volume: 0.2 });
        tone({ at: at + 0.08, from: 880, duration: 0.09, volume: 0.2 });
      },
      win(at = 0) { [392, 523, 659, 784].forEach((f, i) => tone({ at: at + i * 0.11, from: f, duration: 0.22, volume: 0.3 })); },
      lose(at = 0) { [330, 262, 196].forEach((f, i) => tone({ at: at + i * 0.16, from: f, duration: 0.3, volume: 0.28 })); },
      // a card sliding out onto the felt
      whoosh(at = 0) { noise({ at, duration: 0.2, volume: 0.22, type: "bandpass", from: 700, to: 1900, q: 0.9 }); },
      // equipment bolting onto a character
      snap(at = 0) {
        tone({ at, type: "square", from: 320, to: 180, duration: 0.06, volume: 0.24 });
        noise({ at, duration: 0.07, volume: 0.3, type: "highpass", from: 2200 });
      },
      // a hand reaching across the table
      swipe(at = 0) { noise({ at, duration: 0.26, volume: 0.32, type: "bandpass", from: 2400, to: 420, q: 2.2 }); },
      // Duel: two hammers falling together
      clash(at = 0) {
        tone({ at, type: "triangle", from: 240, to: 90, duration: 0.2, volume: 0.5 });
        tone({ at: at + 0.06, type: "triangle", from: 210, to: 80, duration: 0.22, volume: 0.45 });
      },
    };

    return {
      play(name, delayMs = 0) { effects[name]?.(delayMs / 1000); },
      isMuted: () => muted,
      toggle() {
        muted = !muted;
        localStorage.setItem("bangDuelMuted", muted ? "1" : "0");
        if (muted && ctx) ctx.suspend();
        return muted;
      },
    };
  })();

  /* ---------- action mapping ---------- */
  // Actions that resolve by committing a card to the table rather than a character.
  const TABLE_KINDS = new Set(["reload", "discard", "avoid", "select_card", "choose_reveal"]);
  // Actions that live on cards or characters; everything else goes to the dock.
  const CARD_KINDS = new Set([...TABLE_KINDS, "play_card", "play_as_colt"]);
  const CHAR_KINDS = new Set(["use_ability", "choose_active"]);

  function legalActions() { return game?.legalActions || []; }

  // True while the human owns the current decision — the only time the hand's
  // playability hint should be shown.
  function myDecision() {
    return !!game && !game.terminal && !busy && game.currentPlayer === game.humanPlayer;
  }

  /* ---------- auto-pass ----------
     When the only legal decision is ending the turn (or skipping an optional
     effect that has no "use" counterpart), take it after a short, visible
     countdown. Any pointer interaction cancels the countdown. */
  const AUTOPASS_DELAY = 900;

  function cancelAutoPass(manual = false) {
    if (autoPassTimer) clearTimeout(autoPassTimer);
    autoPassTimer = null;
    if (autoPass && manual && game) {
      app.querySelectorAll(".dock-btn.countdown").forEach(el => el.classList.remove("countdown"));
      const pill = app.querySelector(".phase-pill .phase-text");
      if (pill) pill.textContent = phaseText();
    }
    autoPass = null;
  }

  // Whether an optional decision takes the effect or waves it off. The label
  // fallback keeps the page sane when an older server omits the flag.
  function acceptsEffect(action) {
    return action.accept ?? action.label.startsWith("Use");
  }

  function autoPassable(action) {
    return action.kind === "end_turn" ||
      (action.kind === "optional" && !acceptsEffect(action) && game.phase !== "duel_response");
  }

  function scheduleAutoPass() {
    cancelAutoPass();
    if (!myDecision()) return;
    const actions = legalActions();
    if (actions.length !== 1 || !autoPassable(actions[0])) return;
    const action = actions[0];
    const stateRef = game;
    autoPass = { id: action.id };
    // Never end the turn out from under a ceremony still telling the player
    // what just happened.
    const wait = Math.max(AUTOPASS_DELAY, fxBusyUntil - performance.now() + 260);
    autoPassTimer = setTimeout(() => {
      autoPassTimer = null;
      // Stale countdowns (an answer arrived meanwhile) must not fire.
      if (game !== stateRef || busy) return;
      if (legalActions().some(a => a.id === action.id)) applyAction(action.id);
    }, wait);
  }

  function phaseText() {
    if (game.terminal) return "The duel is over";
    if (autoPass) return "Out of plays — ending turn…";
    return game.currentPlayer === game.humanPlayer ? game.phaseLabel : "Opponent is thinking…";
  }

  // The server's plain-English explanation of the pending decision — what is
  // being asked and what the options mean. Only shown while the answer is ours.
  function phaseHint() {
    if (game.terminal || game.currentPlayer !== game.humanPlayer) return "";
    return game.phaseHint || "";
  }

  function actionsForInstance(instance) {
    return legalActions().filter(action =>
      action.cardInstance === instance || action.card?.instance === instance);
  }

  function actionsForCharacter(characterId) {
    return legalActions().filter(action =>
      CHAR_KINDS.has(action.kind) && action.character?.id === characterId);
  }

  function zoneForAction(action) {
    if (action.target) return effectiveDropZone(action.target, game);
    if (TABLE_KINDS.has(action.kind)) return "table";
    return null;
  }

  function actionableInstances() {
    const result = new Set();
    for (const action of legalActions()) {
      if (Number.isInteger(action.cardInstance)) result.add(action.cardInstance);
      if (action.kind === "choose_reveal" && action.card?.instance !== undefined) {
        result.add(action.card.instance);
      }
    }
    return result;
  }

  // Non-card, non-character decisions surface as icon buttons in the dock.
  function dockActions() {
    return legalActions().filter(action =>
      !CARD_KINDS.has(action.kind) && !CHAR_KINDS.has(action.kind));
  }

  function dockButton(action) {
    const icon =
      action.kind === "end_turn" ? ICONS.end :
      action.kind === "swap" ? ICONS.swap :
      action.kind === "take_hit" ? ICONS.hit :
      action.kind === "optional" ? (acceptsEffect(action) ? ICONS.check : ICONS.cross) : null;
    const tone =
      action.kind === "take_hit" || (action.kind === "optional" && !acceptsEffect(action)) ? "danger" :
      action.kind === "optional" && acceptsEffect(action) ? "confirm" :
      action.kind === "end_turn" ? "confirm" : "";
    const countingDown = autoPass?.id === action.id ? "countdown" : "";
    const spotlight = action.kind === "end_turn" && endTurnSpotlight && !countingDown ? "spotlight" : "";
    // While an ability is waiting on a yes/no, a bare ✓/✗ does not say what
    // either option does — spell the two choices out.
    const named = action.kind === "optional" && game.phase === "choose_ability";
    if (action.kind === "choose_back") {
      const side = sideClass(action.origin || "law");
      return `<button class="dock-btn deck-pick" data-action="${action.id}" ${busy ? "disabled" : ""}
        ${tipAttr(action.label, action.detail || "")}>
        <span class="mini-back ${side}"></span></button>`;
    }
    if (!icon) {
      // Safety net: an unmapped decision still gets a (small) labeled button.
      return `<button class="dock-btn labeled" data-action="${action.id}" ${busy ? "disabled" : ""}
        ${tipAttr(action.label, action.detail || "")}>${escapeHtml(action.label)}</button>`;
    }
    return `<button class="dock-btn ${tone} ${countingDown} ${spotlight} ${named ? "named" : ""}" data-action="${action.id}" ${busy ? "disabled" : ""}
      ${tipAttr(action.label, action.detail || "")}>${icon}${named ? `<span>${escapeHtml(action.label)}</span>` : ""}</button>`;
  }

  /* ---------- markup helpers ---------- */
  function tipAttr(title, body) {
    return `data-tip-title="${escapeHtml(title)}" data-tip="${escapeHtml(body)}"`;
  }

  // A side must be down to its last character before "reload" cards can be
  // recycled (BANG! The Duel rulebook, RELOAD).
  function reloadUnlocked() {
    return (game?.players || []).some(player =>
      player.positions.filter(position => position.character && position.life > 0).length <= 1);
  }

  function cardTip(card, extra = "") {
    let body = `${card.rulesText}${extra ? ` — ${extra}` : ""}`;
    if (card.reloadable) {
      body += reloadUnlocked()
        ? " ↻ Recycle: drop on the table to discard it and draw a card."
        : " ↻ Recyclable once a side is down to its last character.";
    }
    return body;
  }

  // Why a dimmed hand card can't be played — kept phase-aware and generic so
  // the frontend never second-guesses the engine's legal-action set.
  function unplayableNote() {
    if (game.phase === "play") {
      return "Not playable right now — it needs a legal target or a free BANG! slot.";
    }
    if (game.phase === "choose_ability") {
      return "Not part of this choice — the pending ability decides what can be picked.";
    }
    return "On hold — only answers to the current situation can be played.";
  }

  // What picking this card would do, when the pending decision is an ability
  // step (pay a cost, keep one of two draws, …) rather than a normal play.
  function pickNote(instance) {
    if (game.phase !== "choose_ability") return "";
    return actionsForInstance(instance).find(action => action.detail)?.detail || "";
  }

  function cardNote(instance, actionable) {
    if (!myDecision()) return "";
    return actionable ? pickNote(instance) : unplayableNote();
  }

  function cardMarkup(card, { compact = false, actionable = false, selected = false, fanIndex = null, fanMid = 0, note = "" } = {}) {
    if (!card) return "";
    const fanStyle = fanIndex === null ? "" : `style="--i:${fanIndex};--mid:${fanMid}"`;
    const tip = cardTip(card, `${card.origin} ${card.kind} · reveal: ${card.reveal}`);
    return `<article class="pcard ${compact ? "compact" : ""} ${actionable ? "actionable" : ""} ${selected ? "selected" : ""}"
      data-instance="${card.instance}" ${fanStyle} ${actionable ? 'role="button" tabindex="0"' : ""}
      ${tipAttr(card.name, note ? `${tip} ${note}` : tip)}>
      <img class="pcard-art" src="${cardImage(card.key)}" alt="" draggable="false">
      <span class="sr-only">${escapeHtml(card.name)}. ${escapeHtml(card.rulesText)}</span>
      <span class="pcard-reveal" aria-hidden="true">${revealIcons[card.reveal] || "◇"}</span>
    </article>`;
  }

  function lifeMarkup(life, maxLife) {
    let html = "";
    for (let i = 0; i < maxLife; i += 1) {
      html += `<span class="bullet ${i >= life ? "empty" : ""}" aria-hidden="true"></span>`;
    }
    return `<div class="life-row" aria-label="${life} of ${maxLife} life">${html}</div>`;
  }

  function equipmentMarkup(cards, actionable) {
    if (!cards?.length) return "";
    return `<div class="equip-row">${cards.map(card =>
      `<span class="equip-card ${actionable.has(card.instance) ? "actionable" : ""} ${selectedCard === card.instance ? "selected" : ""}"
        data-instance="${card.instance}" ${actionable.has(card.instance) ? 'role="button" tabindex="0"' : ""}
        ${tipAttr(card.name, [cardTip(card), pickNote(card.instance)].filter(Boolean).join(" "))}>
        <img src="${cardImage(card.key)}" alt="${escapeHtml(card.name)}" draggable="false"></span>`).join("")}</div>`;
  }

  function characterMarkup(position, { own, actionable }) {
    const zone = `${own ? "own" : "opponent"}_${position.role === "active" ? "active" : "rearguard"}`;
    if (!position.character) {
      const fallen = game.setupComplete;
      return `<div class="char-slot" data-zone="${zone}">
        <article class="char ${fallen ? "eliminated" : "facedown"}"
          ${tipAttr(fallen ? "Fallen" : "Face down", fallen ? "This seat is empty" : "Revealed once both sides commit")}>
          <span class="char-glyph">${fallen ? "☠" : "?"}</span>
        </article>
      </div>`;
    }
    const character = position.character;
    const charActions = own ? actionsForCharacter(character.id) : [];
    // The character whose ability is waiting on the current decision.
    const asking = myDecision() && game.phaseCharacter?.id === character.id ? "asking" : "";
    return `<div class="char-slot ${position.role === "active" ? "is-active" : ""}" data-zone="${zone}">
      <article class="char ${position.life <= 0 ? "eliminated" : ""} ${charActions.length ? "actionable" : ""} ${asking}"
        data-cid="${character.id}" ${charActions.length ? `data-char-id="${character.id}" role="button" tabindex="0"` : ""}
        ${tipAttr(character.name, `${character.abilityText}${character.alwaysActive ? " (Always active)" : ""}`)}>
        <img class="char-art" src="${cardImage(character.key)}" alt="${escapeHtml(character.name)}" draggable="false">
        ${position.role === "active" ? '<span class="active-star" aria-label="Active">★</span>' : ""}
        ${lifeMarkup(position.life, character.maxLife)}
      </article>
      ${equipmentMarkup(position.equipment, actionable)}
    </div>`;
  }

  function deckStack(count, side, tip) {
    return `<div class="deck-stack ${sideClass(side)}" ${tipAttr(tip, "Cards left in this draw pile")}>
      <span class="mini-back ${sideClass(side)}"></span>
      <span class="deck-count">${count}</span>
    </div>`;
  }

  // The reserve pile sits face down, so its size is public while the identities
  // stay hidden (rulebook setup; see docs/architecture.md information boundary).
  function reserveStack(count) {
    const tip = count
      ? `${count} face-down character${count === 1 ? "" : "s"} waiting to replace the fallen`
      // An empty reserve is not lethal on its own: the rulebook keeps the last
      // survivor in play as both AC and RC, with no replacement to follow.
      : "Empty — fallen characters can no longer be replaced";
    return `<div class="reserve-stack ${count ? "" : "empty"}" ${tipAttr("Reserve", tip)}>
      <span class="mini-back char-back"></span>
      <span class="deck-count">${count}</span>
    </div>`;
  }

  function opponentRow(player) {
    const actionable = actionableInstances();
    const positions = [...player.positions].sort((a, b) => a.role === "active" ? -1 : b.role === "active" ? 1 : 0);
    const backs = [];
    const shown = Math.min(player.handCount, 8);
    for (let i = 0; i < shown; i += 1) backs.push(`<span class="card-back ${sideClass(player.side)}"></span>`);
    const known = player.knownHand?.length
      ? `<div class="known-row" ${tipAttr("Known cards", "Cards in this hand you have seen")}>${player.knownHand.map(card =>
          `<span class="known-card" ${tipAttr(card.name, card.rulesText)}><img src="${cardImage(card.key)}" alt="" draggable="false"></span>`).join("")}</div>`
      : "";
    return `<section class="row opponent ${sideClass(player.side)}">
      <div class="row-side">
        ${deckStack(player.deckCount, player.side, `${player.side} deck`)}
        ${reserveStack(player.reserveCount ?? 0)}
        <div class="backs-fan" ${tipAttr(`${player.handCount} cards`, "The opponent's hand")}>${backs.join("")}
          ${player.handCount > shown ? `<span class="more-count">+${player.handCount - shown}</span>` : ""}
          <span class="hand-count" aria-hidden="true">${player.handCount}</span></div>
        ${known}
      </div>
      <div class="char-row">${positions.map(position => characterMarkup(position, { own: false, actionable })).join("")}</div>
      <div class="row-side right"></div>
    </section>`;
  }

  function centerRow() {
    const top = game.discard?.at(-1);
    const choice = [...(game.choiceCards || []), ...(game.revealedCards || [])];
    const actionable = actionableInstances();
    const humanTurn = !game.terminal && game.currentPlayer === game.humanPlayer;
    const hint = phaseHint();
    const coaching = coach.markup();
    return `<section class="row center ${hint ? "has-hint" : ""}" data-zone="table">
      <div class="phase-ribbon">
        <div class="phase-pill ${humanTurn && !busy ? "yours" : ""} ${hint || coaching ? "with-hint" : ""}">
          <span class="phase-line">
            ${busy ? '<span class="spinner"></span>' : ""}
            <span class="phase-text">${escapeHtml(phaseText())}</span>
          </span>
          ${hint ? `<span class="phase-hint">${escapeHtml(hint)}</span>` : ""}
          ${coaching}
        </div>
      </div>
      <div class="table-felt">
        <div class="stage-slot"></div>
        <div class="stage-veil"></div>
        ${choice.length ? `<div class="choice-row">${choice.map(card => cardMarkup(card, {
          compact: true, actionable: actionable.has(card.instance), selected: selectedCard === card.instance,
          note: pickNote(card.instance),
        })).join("")}</div>` : ""}
        <div class="table-piles">
          ${game.commonDeckCreated ? deckStack(game.commonDeckCount, "", "Common deck") : ""}
          <div class="discard-spot" ${tipAttr("Discard", `${game.discardCount} cards`)}>
            ${top ? cardMarkup(top, { compact: true }) : '<span class="discard-empty"></span>'}
          </div>
        </div>
      </div>
    </section>`;
  }

  function playerRow(player) {
    const actionable = actionableInstances();
    const positions = [...player.positions].sort((a, b) => a.role === "active" ? -1 : b.role === "active" ? 1 : 0);
    const hand = player.hand || [];
    const fanTier = hand.length > 9 ? 2 : hand.length > 6 ? 1 : 0;
    const fanMid = (hand.length - 1) / 2;
    // Your own reserve is face up (you looked at those characters during setup),
    // so the cards themselves carry the count; an empty pile still needs a cue.
    const reserve = player.reserve?.length
      ? `<div class="known-row" ${tipAttr("Reserve", "Characters waiting to replace the fallen")}>${player.reserve.map(character =>
          `<span class="known-card" data-cid="${character.id}" ${tipAttr(character.name, character.abilityText)}><img src="${cardImage(character.key)}" alt="" draggable="false"></span>`).join("")}</div>`
      : reserveStack(0);
    return `<section class="row player ${sideClass(player.side)}">
      <div class="player-table">
        <div class="row-side">
          ${deckStack(player.deckCount, player.side, `${player.side} deck`)}
          ${reserve}
        </div>
        <div class="char-row">${positions.map(position => characterMarkup(position, { own: true, actionable })).join("")}</div>
        <div class="dock">${game.terminal ? "" : dockActions().map(dockButton).join("")}</div>
      </div>
      <div class="cards-fan ${myDecision() ? "hinting" : ""}" data-fan="${fanTier}">${hand.map((card, index) => cardMarkup(card, {
        actionable: actionable.has(card.instance),
        selected: selectedCard === card.instance,
        fanIndex: index, fanMid,
        note: cardNote(card.instance, actionable.has(card.instance)),
      })).join("")}</div>
    </section>`;
  }

  /* ---------- the guided first duel ----------
     The board already says what the rules are asking for ("Answer the
     incoming hit"). What a first-time player is missing is the other half:
     which finger goes where. So for one duel only, a second line under the
     phase text says what to physically do — read off the same legal actions
     the board is already highlighting, never a script, so it cannot get out
     of step with the game or contradict it. It retires itself after the first
     couple of turns, or the moment the player says they have got it. */
  const coach = {
    done: localStorage.getItem("bangDuelCoached") === "1",

    dismiss() {
      this.done = true;
      localStorage.setItem("bangDuelCoached", "1");
      render();
    },

    // Two turns is enough to have picked a character, played a card and ended
    // a turn — everything the board asks you to do with your hands.
    retire() {
      if (this.done || !game || (game.turnNumber ?? 0) < 3) return;
      this.done = true;
      localStorage.setItem("bangDuelCoached", "1");
    },

    line() {
      if (this.done || !game || game.terminal || busy || !myDecision()) return "";
      if (selectedCard !== null) {
        const zones = actionsForInstance(selectedCard).map(zoneForAction).filter(Boolean);
        if (zones.length) return `${touchFirst ? "Tap" : "Click"} one of the glowing targets — or the card again to put it back.`;
      }
      if (legalActions().some(action => CHAR_KINDS.has(action.kind))) {
        return `${touchFirst ? "Tap" : "Click"} a glowing character card to choose them.`;
      }
      if (actionableInstances().size) {
        return touchFirst
          ? "Tap a glowing card to play it. Hold any card to read what it does."
          : "Click a glowing card to play it, or drag it onto its target. Hover any card to read it.";
      }
      if (endTurnSpotlight) return "Nothing left to play — the pulsing button ends your turn.";
      return "";
    },

    markup() {
      const line = this.line();
      if (!line) return "";
      return `<span class="coach-line">
        <span class="coach-mark" aria-hidden="true">☞</span>
        <span>${escapeHtml(line)}</span>
        <button class="coach-done" data-coach-done aria-label="Stop showing these tips">Got it</button>
      </span>`;
    },
  };

  function logDrawer() {
    const logs = [...(game.log || [])].reverse().map(entry =>
      `<div class="log-entry ${sideClass(entry.side)}">${escapeHtml(entry.text)}</div>`).join("");
    return `<aside class="log-drawer ${logOpen ? "open" : ""}" aria-label="Action log">
      <header><strong>Trail</strong><button class="ghost-btn" data-toggle-log>${ICONS.close}</button></header>
      <div class="log-list">${logs}</div>
    </aside>`;
  }

  function winnerOverlay() {
    if (!game.terminal) return "";
    const winner = game.players[game.winner];
    const stats = matchStats && matchStats.sessionId === game.sessionId ? matchStats : null;
    return `<div class="winner-overlay">
      <div class="winner-banner">
        <div class="star">★</div>
        <strong>${escapeHtml(winner.side)} wins</strong>
        <span>${winner.isHuman ? "The frontier is yours." : "The AI claimed this duel."}</span>
        ${stats ? `<dl class="winner-stats">
          <div><dt>Turns</dt><dd>${game.turnNumber}</dd></div>
          <div><dt>Damage dealt</dt><dd>${stats.damageDealt}</dd></div>
          <div><dt>Damage taken</dt><dd>${stats.damageTaken}</dd></div>
          <div><dt>Cards played</dt><dd>${stats.cardsPlayed}</dd></div>
          <div><dt>Shots avoided</dt><dd>${stats.shotsAvoided}</dd></div>
          <div><dt>Best hit</dt><dd>${stats.bestBurst}</dd></div>
        </dl>` : ""}
        <div class="winner-actions">
          <button class="primary-btn" data-quick-play>Ride again</button>
          <button class="ghost-btn" data-share-card>${shareVerb()}</button>
          <button class="ghost-btn" data-new-game>Change the setup</button>
        </div>
      </div>
    </div>`;
  }

  /* ---------- the wanted poster ----------
     The one thing a finished duel had nothing of was a way out of the tab.
     Every number the scoreboard already knows is drawn onto a poster at share
     proportions, so the end of a game produces an image — handed to the
     system share sheet where there is one, saved as a PNG where there is not.
     It is drawn rather than screenshotted: the board is a live WebGL canvas
     inside a DOM layout, and nothing in a browser can photograph that. */

  const POSTER = { w: 1080, h: 1350 };

  const shareVerb = () =>
    (navigator.canShare && navigator.share) ? "Share the poster" : "Save the poster";

  const titleCase = value => String(value || "").replace(/\b[a-z]/g, letter => letter.toUpperCase());

  function posterLines() {
    const stats = matchStats && matchStats.sessionId === game.sessionId ? matchStats : null;
    const won = game.players[game.winner]?.isHuman;
    /* The last few lines of the trail: the duel's own account of how it
       ended, which is the part worth showing someone who was not there. The
       numbers say how it went, these say what happened. */
    const trail = (game.log || [])
      .filter(entry => entry.text && !/^—/.test(entry.text))
      .slice(-5)
      .map(entry => entry.text.length > 54 ? `${entry.text.slice(0, 53)}…` : entry.text);
    return {
      won, trail,
      headline: won ? "LAST ONE STANDING" : "OUTGUNNED",
      side: `${titleCase(game.players[game.humanPlayer]?.side)} · ${game.charactersPerSide} characters a side`,
      opponent: game.ai?.trained
        ? `vs ${game.ai.name} · iteration ${game.ai.iteration}`
        : `vs ${game.ai?.name || "the house heuristic"}`,
      seed: `seed ${game.seed ?? "—"}`,
      stats: [
        ["Turns", game.turnNumber],
        ["Damage dealt", stats?.damageDealt ?? 0],
        ["Damage taken", stats?.damageTaken ?? 0],
        ["Cards played", stats?.cardsPlayed ?? 0],
        ["Shots dodged", stats?.shotsAvoided ?? 0],
        ["Biggest hit", stats?.bestBurst ?? 0],
      ],
    };
  }

  function drawPoster() {
    const canvas = document.createElement("canvas");
    canvas.width = POSTER.w;
    canvas.height = POSTER.h;
    const ctx = canvas.getContext("2d");
    const { w, h } = POSTER;
    const copy = posterLines();
    const gold = "#e8bf6a";
    const paper = "#efdfb8";

    // the boards of the saloon wall
    ctx.fillStyle = "#2a1912";
    ctx.fillRect(0, 0, w, h);
    for (let x = 0; x < w; x += 26) {
      ctx.fillStyle = x % 52 === 0 ? "rgba(255,255,255,.018)" : "rgba(0,0,0,.06)";
      ctx.fillRect(x, 0, 13, h);
    }
    const glow = ctx.createRadialGradient(w / 2, h * 0.38, 40, w / 2, h * 0.38, w * 0.85);
    glow.addColorStop(0, copy.won ? "rgba(199,155,70,.24)" : "rgba(165,43,34,.2)");
    glow.addColorStop(1, "rgba(0,0,0,.55)");
    ctx.fillStyle = glow;
    ctx.fillRect(0, 0, w, h);

    // the poster itself, pinned slightly crooked
    ctx.save();
    ctx.translate(w / 2, h / 2);
    ctx.rotate(-0.012);
    ctx.translate(-w / 2, -h / 2);
    const px = 78, py = 76, pw = w - px * 2, ph = h - py * 2;
    ctx.shadowColor = "rgba(0,0,0,.55)";
    ctx.shadowBlur = 60;
    ctx.shadowOffsetY = 22;
    ctx.fillStyle = paper;
    ctx.fillRect(px, py, pw, ph);
    ctx.shadowColor = "transparent";
    ctx.strokeStyle = "#8d693b";
    ctx.lineWidth = 5;
    ctx.strokeRect(px + 16, py + 16, pw - 32, ph - 32);
    ctx.lineWidth = 1.5;
    ctx.strokeRect(px + 28, py + 28, pw - 56, ph - 56);

    const mid = w / 2;
    ctx.textAlign = "center";

    const label = (text, y, size, colour, spacing = 6) => {
      ctx.fillStyle = colour;
      ctx.font = `${size}px Georgia, "Times New Roman", serif`;
      ctx.letterSpacing = `${spacing}px`;
      ctx.fillText(text, mid, y);
      ctx.letterSpacing = "0px";
    };

    label("★  BANG! THE DUEL  ★", py + 108, 34, "#7d5a34", 8);
    ctx.fillStyle = "#8d693b";
    ctx.fillRect(px + 150, py + 140, pw - 300, 3);

    label(copy.won ? "WANTED" : "IN MEMORIAM", py + 226, 46, "#5a4338", 16);

    // the verdict, in the biggest type the poster has room for
    ctx.fillStyle = copy.won ? "#a52b22" : "#4a3a33";
    ctx.font = `bold 92px Georgia, "Times New Roman", serif`;
    ctx.letterSpacing = "2px";
    let size = 92;
    while (ctx.measureText(copy.headline).width > pw - 130 && size > 40) {
      size -= 4;
      ctx.font = `bold ${size}px Georgia, "Times New Roman", serif`;
    }
    ctx.fillText(copy.headline, mid, py + 340);
    ctx.letterSpacing = "0px";

    label(copy.side, py + 400, 30, "#6b5245", 4);
    label(copy.opponent, py + 444, 26, "#8a7263", 3);

    // the tally, two columns
    const top = py + 520;
    const rowH = 96;
    const colW = (pw - 200) / 2;
    copy.stats.forEach(([name, value], index) => {
      const col = index % 2;
      const row = Math.floor(index / 2);
      const cx = px + 100 + colW * col + colW / 2;
      const cy = top + row * rowH;
      ctx.fillStyle = "#a58a63";
      ctx.font = `22px Georgia, serif`;
      ctx.letterSpacing = "4px";
      ctx.fillText(String(name).toUpperCase(), cx, cy);
      ctx.letterSpacing = "0px";
      ctx.fillStyle = "#a52b22";
      ctx.font = `bold 54px Georgia, serif`;
      ctx.fillText(String(value), cx, cy + 56);
    });

    // rule, then how it ended in the duel's own words
    const ruleY = top + rowH * 3 - 6;
    const rule = y => {
      ctx.strokeStyle = "rgba(125,90,52,.4)";
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(px + 120, y);
      ctx.lineTo(px + pw - 120, y);
      ctx.stroke();
    };
    rule(ruleY);

    /* Everything below the tally is laid out downward against a hard floor:
       the trail is as many lines as it is, and the small print underneath has
       to stay where it is, so lines that do not fit are simply not drawn. */
    const floor = py + ph - 128;
    let y = ruleY + 46;
    if (matchStats?.bestStreak > 1) {
      label(`Longest run of scoring turns: ${matchStats.bestStreak}`, y, 26, "#6b5245", 2);
      y += 52;
    }
    label("HOW IT ENDED", y, 22, "#a58a63", 8);
    y += 42;
    for (const line of copy.trail) {
      if (y > floor - 18) break;
      label(line, y, 25, "#6b5245", 0);
      y += 36;
    }

    rule(floor);
    label(copy.seed, py + ph - 92, 26, "#8a7263", 5);
    label("bang.matehegedus.com", py + ph - 52, 24, "#a58a63", 5);
    ctx.restore();

    // a couple of pins, so it reads as a poster rather than a slide
    for (const [x, y] of [[px + 40, py + 40], [w - px - 40, py + 44]]) {
      ctx.beginPath();
      ctx.arc(x, y, 13, 0, Math.PI * 2);
      ctx.fillStyle = gold;
      ctx.fill();
      ctx.strokeStyle = "rgba(60,35,14,.6)";
      ctx.lineWidth = 3;
      ctx.stroke();
    }
    return canvas;
  }

  function posterBlob(canvas) {
    return new Promise(resolve => canvas.toBlob(resolve, "image/png"));
  }

  async function sharePoster() {
    if (!game?.terminal) return;
    let blob;
    try {
      blob = await posterBlob(drawPoster());
    } catch (error) {
      showToast("The poster could not be drawn on this browser.");
      return;
    }
    if (!blob) { showToast("The poster could not be drawn on this browser."); return; }
    const name = `bang-duel-${game.seed ?? "duel"}.png`;
    const file = new File([blob], name, { type: "image/png" });
    // The share sheet is the point on a phone; a download is the desktop
    // equivalent and the fallback when a browser will not take files.
    if (navigator.canShare?.({ files: [file] }) && navigator.share) {
      try {
        await navigator.share({ files: [file], title: "BANG! The Duel" });
        return;
      } catch (error) {
        if (error?.name === "AbortError") return;   // they changed their mind
      }
    }
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = name;
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 4000);
    showToast("Poster saved.");
  }

  function render() {
    if (!game) return;
    cancelDrag();
    closeChooser();
    hideTip();
    const previous = renderedGame;
    const previousRects = previous && previous !== game ? captureRects() : null;
    // The board rebuilds its whole DOM every state; on a phone the hand is a
    // scrolled rail, and losing its position on every play would throw the
    // player back to the first card each time they did anything.
    const railAt = app.querySelector(".cards-fan")?.scrollLeft ?? 0;
    const human = game.players[game.humanPlayer];
    const opponent = game.players[1 - game.humanPlayer];
    app.classList.toggle("busy", busy);
    coach.retire();
    scheduleAutoPass();
    // Spotlight the end-turn button once nothing in the hand or on the
    // characters can be acted on — only dock decisions are left.
    endTurnSpotlight = myDecision() &&
      legalActions().some(action => action.kind === "end_turn") &&
      !legalActions().some(action => CARD_KINDS.has(action.kind) || CHAR_KINDS.has(action.kind));
    app.innerHTML = `
      <header class="topbar">
        <div class="brand"><span class="brand-mark">B!</span></div>
        <div class="top-icons">
          <span class="ai-dot ${game.ai?.trained ? "trained" : ""}"
            ${tipAttr(game.ai?.name || "AI", game.ai?.trained ? `Trained checkpoint · iteration ${game.ai.iteration}` : "Built-in heuristic")}></span>
          <a class="ghost-btn" href="about.html" aria-label="About the trained AI"
            ${tipAttr("About the AI", "Training, architecture, and learned strategy")}>${ICONS.about}</a>
          <button class="ghost-btn" data-toggle-sound ${tipAttr(sfx.isMuted() ? "Sound off" : "Sound on", "Toggle sound effects")}>${sfx.isMuted() ? ICONS.mute : ICONS.sound}</button>
          <button class="ghost-btn" data-toggle-log ${tipAttr("Action log", "")}>${ICONS.log}</button>
          <button class="ghost-btn" data-new-game ${tipAttr("New duel", "")}>${ICONS.plus}</button>
        </div>
      </header>
      <main class="table-wrap">
        ${opponentRow(opponent)}
        ${centerRow()}
        ${playerRow(human)}
      </main>
      ${logDrawer()}
      ${winnerOverlay()}`;
    renderedGame = game;
    voxelStage.attach();
    voxelStage.place();
    voxelStage.reseat(previousRects ? previous : null, game);
    if (previousRects) {
      runAnimations(previous, game, previousRects);
      // The ceremonies just scheduled push the auto-pass countdown back, so it
      // never ends the turn while the table is still explaining itself.
      scheduleAutoPass();
    }
    updateZoneMarks();
    tuneHandRail(railAt);
  }

  /* Keep the hand rail where the player left it, and bring a freshly selected
     card to the middle. On desktop the fan does not scroll at all, so nothing
     below this line fires there. */
  function tuneHandRail(restoreTo = 0) {
    const rail = app.querySelector(".cards-fan");
    if (!rail) return;
    if (rail.scrollWidth <= rail.clientWidth + 4) return;
    rail.scrollLeft = Math.min(restoreTo, rail.scrollWidth - rail.clientWidth);
    if (selectedCard === null) return;
    const card = rail.querySelector(`.pcard[data-instance="${selectedCard}"]`);
    card?.scrollIntoView({ inline: "center", block: "nearest", behavior: "smooth" });
  }

  /* ---------- the 3D battlefield ----------
     The stage is a WebGL canvas in its own fixed layer rather than a node
     inside the board, because the board replaces its whole DOM on every
     state and would throw the drawing context away with it. Each render
     measures the hole left for it in the felt and moves the layer there.

     It loads as a module, so it arrives after the first paint; everything
     here has to work whether or not it ever shows up (no WebGL, an old
     browser, reduced motion). */

  const voxelStage = {
    api: null,
    mounted: false,

    available() {
      return !!this.api && !reducedMotion;
    },

    attach() {
      const api = window.BangVoxelStage;
      if (!api || this.mounted || reducedMotion || !api.supported?.()) return;
      if (!api.mount(stageRoot)) return;
      this.api = api;
      this.mounted = true;
      // The board only gives the middle of the felt over to the battlefield
      // once there is a battlefield to put there.
      document.body.classList.add("has-stage");
      this.place();
      if (game) this.sync(game);
      requestAnimationFrame(() => {
        this.place();
        stageRoot.classList.add("lit");
      });
    },

    // Sit the layer exactly over the slot the felt leaves for it.
    place() {
      if (!this.mounted) return;
      const slot = app.querySelector(".stage-slot");
      if (!slot) { stageRoot.style.display = "none"; return; }
      const rect = slot.getBoundingClientRect();
      if (!rect.width || !rect.height) { stageRoot.style.display = "none"; return; }
      stageRoot.style.display = "";
      stageRoot.style.left = `${rect.left}px`;
      stageRoot.style.top = `${rect.top}px`;
      stageRoot.style.width = `${rect.width}px`;
      stageRoot.style.height = `${rect.height}px`;
      this.api.resize();
    },

    sync(state) {
      if (!this.available() || !state) return;
      this.api.sync(Array.from(seatedCharacters(state).values()));
    },

    /* The cast changing, in the order it has to happen: the fallen are put
       down while they are still standing on the stage, the board's new seating
       is applied, and only then does the replacement ride in. */
    reseat(prev, next) {
      if (!this.available() || !next) return;
      const seats = prev ? characterEvents(prev, next) : { fallen: [], arrivals: [] };
      for (const who of seats.fallen) {
        this.api.fall(who.id, who.key, who.zone, who.swept ? 200 : 380, who.swept);
      }
      this.sync(next);
      for (const who of seats.arrivals) {
        this.api.arrive(who.id, who.replacement ? 940 : 300);
      }
    },
  };

  window.addEventListener("bang-voxel-stage-ready", () => voxelStage.attach());
  window.addEventListener("resize", () => voxelStage.place());

  /* ---------- animations ----------
     The board fully re-renders per state, so animation is a diff pass:
     capture element rects before the swap, compare the old and new game
     states, and fly/flip ghost cards between the two layouts. */

  function captureRects() {
    const rects = new Map();
    app.querySelectorAll("[data-instance]").forEach(el =>
      rects.set(`i${el.dataset.instance}`, el.getBoundingClientRect()));
    app.querySelectorAll("[data-cid]").forEach(el =>
      rects.set(`c${el.dataset.cid}`, el.getBoundingClientRect()));
    // Seats, so a character arriving into one can be flown in from wherever it
    // was standing (or flipped up in place when the seat was face down).
    app.querySelectorAll(".char-slot[data-zone]").forEach(el =>
      rects.set(`z${el.dataset.zone}`, el.getBoundingClientRect()));
    const anchors = {
      ownDeck: ".row.player .deck-stack",
      oppDeck: ".row.opponent .deck-stack",
      oppBacks: ".row.opponent .backs-fan",
      discard: ".discard-spot",
      lawDeck: ".deck-stack.law",
      outlawDeck: ".deck-stack.outlaw",
      ownReserve: ".row.player .row-side .known-row, .row.player .reserve-stack",
      oppReserve: ".row.opponent .reserve-stack",
    };
    for (const [key, selector] of Object.entries(anchors)) {
      const el = app.querySelector(selector);
      if (el) rects.set(key, el.getBoundingClientRect());
    }
    return rects;
  }

  function ghostArt(key) {
    return `<img class="fx-art" src="${cardImage(key)}" alt="">`;
  }

  /* Exit paths — the "unique fashion" each card motif reaches its
     destination. A builder receives the trip geometry and returns Web
     Animation keyframes; ghosts scale and rotate about their own centre, so
     `dx`/`dy` already carry the offset that lands the card exactly on target.
       dx, dy   travel to the destination
       sx, sy   final scale
       px, py   unit vector across the travel direction
       dist     length of the trip */
  const place = (x, y, sx, sy, extra = "", opacity = 1) =>
    ({ transform: `translate(${x}px, ${y}px) scale(${sx}, ${sy}) ${extra}`.trim(), opacity });
  const midScale = (s, t = 0.5) => 1 + (s - 1) * t;

  const EXIT_PATHS = {
    // plain toss: a shallow arc with a lazy turn
    toss: ({ dx, dy, sx, sy }) => [
      place(0, 0, 1, 1, "rotate(0deg)", .95),
      { ...place(dx * .5, dy * .5 - 26, midScale(sx), midScale(sy), "rotate(-6deg)"), offset: .55 },
      place(dx, dy, sx, sy, "rotate(3deg)"),
    ],

    // gunshot: the card kicks back with the recoil, then whips onto the pile
    recoil: ({ dx, dy, sx, sy, px, py }) => [
      place(0, 0, 1, 1, "rotate(0deg)", .96),
      { ...place(-dx * .13 + px * 14, -dy * .13 + py * 14, 1.16, 1.16, "rotate(-14deg)"), offset: .2 },
      { ...place(dx * 1.07, dy * 1.07, sx * .95, sy * .95, "rotate(11deg)"), offset: .76 },
      place(dx, dy, sx, sy, "rotate(4deg)"),
    ],

    // barrage: flung wide, spinning twice before it settles
    scatter: ({ dx, dy, sx, sy, px, py, dist }) => {
      const arc = Math.min(dist * .45, 200);
      return [
        place(0, 0, 1, 1, "rotate(0deg)", .96),
        { ...place(dx * .34 + px * arc, dy * .34 + py * arc, 1.12, 1.12, "rotate(300deg)"), offset: .44 },
        { ...place(dx * 1.09 - px * arc * .22, dy * 1.09 - py * arc * .22, sx, sy, "rotate(650deg)"), offset: .84 },
        place(dx, dy, sx, sy, "rotate(720deg)"),
      ];
    },

    // Duel: the card lunges, hangs for a beat, then drops dead onto the pile
    clash: ({ dx, dy, sx, sy }) => [
      place(0, 0, 1, 1, "rotate(0deg)", .96),
      { ...place(dx * .72, dy * .72 - 34, 1.18, 1.18, "rotate(-4deg)"), offset: .3 },
      { ...place(dx * .74, dy * .74 - 30, 1.15, 1.15, "rotate(-3deg)"), offset: .55 },
      place(dx, dy, sx, sy, "rotate(1deg)"),
    ],

    // Dynamite: flares white, blows apart, drops in as debris
    shatter: ({ dx, dy, sx, sy }) => [
      { ...place(0, 0, 1, 1, "rotate(0deg)", .96), filter: "brightness(1)" },
      { ...place(dx * .16, dy * .16, 1.42, 1.42, "rotate(5deg)"), offset: .24, filter: "brightness(3)" },
      { ...place(dx * .8, dy * .8, midScale(sx, .8) * 1.1, midScale(sy, .8) * 1.1, "rotate(-13deg)"), offset: .66, filter: "brightness(1.3)" },
      { ...place(dx, dy, sx, sy, "rotate(-4deg)"), filter: "brightness(1)" },
    ],

    // Rattlesnake: an S-curve across the felt
    slither: ({ dx, dy, sx, sy, px, py, dist }) => {
      const w = Math.min(dist * .24, 78);
      const bend = (t, swing, rot) => place(
        dx * t + px * w * swing, dy * t + py * w * swing,
        midScale(sx, t), midScale(sy, t), `rotate(${rot}deg)`);
      return [
        place(0, 0, 1, 1, "rotate(0deg)", .96),
        { ...bend(.25, 1, 12), offset: .25 },
        { ...bend(.5, -1, -12), offset: .5 },
        { ...bend(.75, .55, 8), offset: .75 },
        place(dx, dy, sx, sy, "rotate(0deg)"),
      ];
    },

    // Beer / Strong Ale: rises like foam, then settles softly
    settle: ({ dx, dy, sx, sy }) => [
      place(0, 0, 1, 1, "rotate(0deg)", .96),
      { ...place(dx * .22, dy * .22 - 52, 1.1, 1.1, "rotate(-3deg)"), offset: .34 },
      { ...place(dx * .68, dy * .68 - 18, midScale(sx, .7), midScale(sy, .7), "rotate(2deg)"), offset: .72 },
      place(dx, dy, sx, sy, "rotate(0deg)"),
    ],

    // a block: the card turns edge-on, bats the shot aside, spins off
    ricochet: ({ dx, dy, sx, sy, px, py }) => [
      place(0, 0, 1, 1, "rotateY(0deg) rotate(0deg)", .96),
      { ...place(px * 46 + dx * .1, py * 46 + dy * .1, 1.14, 1.14, "rotateY(180deg) rotate(-16deg)"), offset: .3 },
      { ...place(dx * .72 - px * 20, dy * .72 - py * 20, midScale(sx, .7), midScale(sy, .7), "rotateY(430deg) rotate(14deg)"), offset: .74 },
      place(dx, dy, sx, sy, "rotateY(720deg) rotate(0deg)"),
    ],

    // Stealing / Schoolmarm: hooks out sideways and drags the loot back in
    swipe: ({ dx, dy, sx, sy, px, py, dist }) => {
      const hook = Math.min(dist * .6, 230);
      return [
        place(0, 0, 1, 1, "rotate(0deg)", .96),
        { ...place(px * hook + dx * .2, py * hook + dy * .2, 1.08, .92, "rotate(-22deg) skewX(10deg)"), offset: .38 },
        { ...place(dx * .9 + px * hook * .18, dy * .9 + py * hook * .18, midScale(sx, .85), midScale(sy, .85), "rotate(10deg) skewX(-6deg)"), offset: .8 },
        place(dx, dy, sx, sy, "rotate(0deg) skewX(0deg)"),
      ];
    },

    // General Store / Wagon: spirals down onto the pile
    pinwheel: ({ dx, dy, sx, sy, px, py, dist }) => {
      const swing = Math.min(dist * .3, 130);
      return [
        place(0, 0, 1, 1, "rotate(0deg)", .96),
        { ...place(dx * .3 + px * swing, dy * .3 + py * swing, 1.05, 1.05, "rotate(200deg)"), offset: .35 },
        { ...place(dx * .72 - px * swing * .6, dy * .72 - py * swing * .6, midScale(sx, .8), midScale(sy, .8), "rotate(400deg)"), offset: .72 },
        place(dx, dy, sx, sy, "rotate(540deg)"),
      ];
    },

    // Water / Stampede: end-over-end, like a character changing places
    tumble: ({ dx, dy, sx, sy }) => [
      place(0, 0, 1, 1, "rotateX(0deg)", .96),
      { ...place(dx * .35, dy * .35 - 30, 1.08, 1.08, "rotateX(360deg)"), offset: .4 },
      { ...place(dx * .76, dy * .76 - 10, midScale(sx, .8), midScale(sy, .8), "rotateX(720deg)"), offset: .76 },
      place(dx, dy, sx, sy, "rotateX(1080deg)"),
    ],

    // equipment: driven home and locked with a bounce
    slot: ({ dx, dy, sx, sy }) => [
      place(0, 0, 1, 1, "rotate(0deg)", .96),
      { ...place(dx * 1.09, dy * 1.09, sx * 1.14, sy * 1.14, "rotate(-3deg)"), offset: .68 },
      { ...place(dx * .98, dy * .98, sx * .95, sy * .95, "rotate(1deg)"), offset: .86 },
      place(dx, dy, sx, sy, "rotate(0deg)"),
    ],
  };

  // Fly a ghost card from one rect to another; optionally flip it face-up
  // from a deck-colored back on the way, or take a named exit path.
  function flyGhost({ html, from, to, delay = 0, duration = 420, flip = false, side = "",
                      path = null, easing = "cubic-bezier(.45,.15,.25,1)", tone = "", linger = 0 }) {
    if (!from || !to || !from.width || !to.width) return;
    const ghost = document.createElement("div");
    ghost.className = `fx-ghost ${tone ? `tone-${tone}` : ""}`;
    ghost.style.left = `${from.left}px`;
    ghost.style.top = `${from.top}px`;
    ghost.style.width = `${from.width}px`;
    ghost.style.height = `${from.height}px`;
    ghost.innerHTML = flip
      ? `<div class="fx-flip"><span class="fx-face fx-back"><span class="mini-back ${side}"></span></span><span class="fx-face fx-front">${html}</span></div>`
      : html;
    fxRoot.appendChild(ghost);
    const sx = to.width / from.width;
    const sy = to.height / from.height;
    // Ghosts scale about their centre so rotation reads naturally; the extra
    // half-difference keeps the landing rect exactly on target.
    const dx = to.left - from.left + from.width * (sx - 1) / 2;
    const dy = to.top - from.top + from.height * (sy - 1) / 2;
    const dist = Math.hypot(dx, dy) || 1;
    const builder = typeof path === "function" ? path : EXIT_PATHS[path];
    const frames = builder
      ? builder({ dx, dy, sx, sy, px: -dy / dist, py: dx / dist, dist })
      : [place(0, 0, 1, 1, "", .95), place(dx, dy, sx, sy)];
    const move = ghost.animate(frames, { duration, delay, easing, fill: "both" });
    // `linger` holds the card at its landing pose — how a played card stays up
    // on centre stage long enough to be read before it leaves for the pile.
    move.onfinish = () => { if (linger > 0) setTimeout(() => ghost.remove(), linger); else ghost.remove(); };
    if (flip) {
      ghost.querySelector(".fx-flip").animate([
        { transform: "rotateY(180deg)" },
        { transform: "rotateY(0deg)" },
      ], { duration, delay, easing: "ease-out", fill: "both" });
    }
    setTimeout(() => ghost.remove(), delay + duration + linger + 600); // safety net
  }

  /* ---------- effect primitives ----------
     Small, self-removing DOM sprinkles the motifs compose into a signature
     look: flashes, shock rings, tracer lines, particle sprays. */

  function fxNode(className, rect, { size = 0, html = "", tone = "" } = {}) {
    if (!rect) return null;
    const el = document.createElement("div");
    el.className = `${className} ${tone ? `tone-${tone}` : ""}`;
    el.style.left = `${rect.left + rect.width / 2}px`;
    el.style.top = `${rect.top + rect.height / 2}px`;
    if (size) { el.style.width = `${size}px`; el.style.height = `${size}px`; }
    if (html) el.innerHTML = html;
    fxRoot.appendChild(el);
    return el;
  }

  function retire(el, ms) { setTimeout(() => el.remove(), ms); }

  // A hot flare centred on a rect — muzzle flash, blast core, arcane pop.
  function flashBurst(rect, tone, delay = 0, scale = 1) {
    const el = fxNode("fx-flash", rect, { size: Math.max(60, (rect?.width || 80) * 1.5 * scale), tone });
    if (!el) return;
    el.animate([
      { transform: "translate(-50%, -50%) scale(.2)", opacity: 0 },
      { transform: "translate(-50%, -50%) scale(1)", opacity: .95, offset: .22 },
      { transform: "translate(-50%, -50%) scale(1.5)", opacity: 0 },
    ], { duration: 360, delay, easing: "ease-out", fill: "both" }).onfinish = () => el.remove();
    retire(el, delay + 900);
  }

  // An expanding ring: impact, shield, shockwave.
  function shockRing(rect, tone, delay = 0, { size = 1, duration = 620, thick = 3 } = {}) {
    const base = Math.max(70, (rect?.width || 90) * 1.35) * size;
    const el = fxNode("fx-ring", rect, { size: base, tone });
    if (!el) return;
    el.style.borderWidth = `${thick}px`;
    el.animate([
      { transform: "translate(-50%, -50%) scale(.25)", opacity: 0 },
      { transform: "translate(-50%, -50%) scale(.6)", opacity: .9, offset: .2 },
      { transform: "translate(-50%, -50%) scale(1.35)", opacity: 0 },
    ], { duration, delay, easing: "cubic-bezier(.2,.7,.3,1)", fill: "both" }).onfinish = () => el.remove();
    retire(el, delay + duration + 500);
  }

  // A bullet streak from one rect's centre to another's.
  function tracerShot(from, to, tone, delay = 0, duration = 240) {
    if (!from || !to) return;
    const x1 = from.left + from.width / 2;
    const y1 = from.top + from.height / 2;
    const x2 = to.left + to.width / 2;
    const y2 = to.top + to.height / 2;
    const length = Math.hypot(x2 - x1, y2 - y1);
    if (length < 12) return;
    const el = document.createElement("div");
    el.className = `fx-tracer ${tone ? `tone-${tone}` : ""}`;
    el.style.left = `${x1}px`;
    el.style.top = `${y1}px`;
    el.style.width = `${length}px`;
    el.style.transform = `rotate(${Math.atan2(y2 - y1, x2 - x1)}rad)`;
    fxRoot.appendChild(el);
    el.animate([
      { clipPath: "inset(0 100% 0 0)", opacity: 1 },
      { clipPath: "inset(0 0 0 0)", opacity: 1, offset: .55 },
      { clipPath: "inset(0 0 0 92%)", opacity: 0 },
    ], { duration, delay, easing: "cubic-bezier(.2,.8,.3,1)", fill: "both" }).onfinish = () => el.remove();
    retire(el, delay + duration + 500);
  }

  // A spray of small marks flung out of a rect: sparks, splinters, bubbles.
  function particles(rect, { tone = "", count = 8, delay = 0, spread = Math.PI * 2,
                             aim = -Math.PI / 2, distance = 70, duration = 620,
                             gravity = 0, shape = "spark", size = 7 } = {}) {
    if (!rect) return;
    for (let i = 0; i < count; i += 1) {
      const spin = (i / Math.max(1, count - 1) - .5) * spread;
      const angle = aim + spin;
      const reach = distance * (.6 + ((i * 37) % 100) / 140);
      const el = fxNode(`fx-bit shape-${shape}`, rect, { size, tone });
      if (!el) return;
      el.animate([
        { transform: "translate(-50%, -50%) translate(0, 0) scale(.4)", opacity: 1 },
        {
          transform: `translate(-50%, -50%) translate(${Math.cos(angle) * reach}px, ${Math.sin(angle) * reach + gravity}px) scale(1)`,
          opacity: 0,
        },
      ], { duration: duration + i * 18, delay, easing: "cubic-bezier(.15,.7,.3,1)", fill: "both" })
        .onfinish = () => el.remove();
      retire(el, delay + duration + 700);
    }
  }

  // The plaque under a staged card: who played what, and what it does.
  function stageCaption(rect, title, subtitle, tone, delay, hold) {
    if (!rect) return;
    const el = document.createElement("div");
    el.className = `fx-plaque ${tone ? `tone-${tone}` : ""}`;
    el.innerHTML = `<span class="fx-plaque-who">${escapeHtml(title)}</span>
      <strong class="fx-plaque-card">${escapeHtml(subtitle)}</strong>`;
    el.style.left = `${rect.left + rect.width / 2}px`;
    el.style.top = `${rect.top + rect.height + 12}px`;
    fxRoot.appendChild(el);
    const life = Math.max(420, hold + 320);
    el.animate([
      { transform: "translate(-50%, 10px) scale(.86)", opacity: 0 },
      { transform: "translate(-50%, 0) scale(1)", opacity: 1, offset: .18 },
      { transform: "translate(-50%, 0) scale(1)", opacity: 1, offset: .78 },
      { transform: "translate(-50%, -8px) scale(.96)", opacity: 0 },
    ], { duration: life, delay, easing: "cubic-bezier(.25,1.2,.4,1)", fill: "both" })
      .onfinish = () => el.remove();
    retire(el, delay + life + 500);
  }

  /* Motif signatures: what happens around the card while it is on stage, and
     what lands on the character it was aimed at. */
  const BURSTS = {
    muzzle(stage, target, delay) {
      flashBurst(stage, "danger", delay, .8);
      particles(stage, { tone: "danger", count: 7, delay, spread: 1.5, distance: 74, shape: "spark" });
      tracerShot(stage, target, "danger", delay + 90, 220);
      shockRing(target, "danger", delay + 250, { size: .9, duration: 520 });
      particles(target, { tone: "danger", count: 8, delay: delay + 260, distance: 62, gravity: 26 });
    },
    spray(stage, target, delay) {
      flashBurst(stage, "danger", delay, 1.1);
      for (let i = 0; i < 3; i += 1) tracerShot(stage, target, "danger", delay + 60 + i * 80, 200);
      shockRing(target, "danger", delay + 260, { size: 1.25, duration: 620 });
      particles(target, { tone: "danger", count: 14, delay: delay + 280, distance: 96, gravity: 30 });
    },
    clash(stage, target, delay) {
      shockRing(stage, "danger", delay, { size: 1.4, duration: 520, thick: 4 });
      shockRing(target, "danger", delay + 160, { size: 1.4, duration: 520, thick: 4 });
      particles(stage, { tone: "danger", count: 10, delay: delay + 120, distance: 84, shape: "shard" });
    },
    shards(stage, target, delay) {
      // fuse first, then the card goes up
      particles(stage, { tone: "gold", count: 5, delay, aim: -Math.PI / 2, spread: .9, distance: 40, duration: 420 });
      flashBurst(stage, "blast", delay + 240, 1.6);
      shockRing(stage, "blast", delay + 240, { size: 1.9, duration: 720, thick: 5 });
      particles(stage, { tone: "blast", count: 16, delay: delay + 250, distance: 150, gravity: 60, shape: "shard", size: 10 });
      shockRing(target, "blast", delay + 380, { size: 1.3, duration: 620 });
    },
    venom(stage, target, delay) {
      particles(stage, { tone: "venom", count: 10, delay, distance: 78, duration: 760, shape: "drop" });
      shockRing(target, "venom", delay + 200, { size: 1, duration: 660 });
      particles(target, { tone: "venom", count: 7, delay: delay + 230, distance: 58, gravity: 34, shape: "drop" });
    },
    bubbles(stage, target, delay) {
      particles(stage, { tone: "heal", count: 12, delay, aim: -Math.PI / 2, spread: 1.9, distance: 96, duration: 900, gravity: -30, shape: "bubble" });
      shockRing(target, "heal", delay + 220, { size: 1, duration: 700 });
      particles(target, { tone: "heal", count: 8, delay: delay + 240, aim: -Math.PI / 2, spread: 1.6, distance: 66, gravity: -24, shape: "bubble" });
    },
    shield(stage, target, delay) {
      shockRing(stage, "guard", delay, { size: 1.5, duration: 560, thick: 4 });
      shockRing(stage, "guard", delay + 140, { size: 1.9, duration: 620, thick: 2 });
      particles(stage, { tone: "guard", count: 9, delay: delay + 80, distance: 74, shape: "shard" });
    },
    grab(stage, target, delay) {
      particles(target, { tone: "greed", count: 9, delay, distance: 70, shape: "shard" });
      shockRing(target, "greed", delay + 60, { size: 1.1, duration: 560 });
      tracerShot(target, stage, "greed", delay + 120, 300);
    },
    fan(stage, target, delay) {
      particles(stage, { tone: "gold", count: 9, delay, aim: -Math.PI / 2, spread: 2.4, distance: 104, duration: 720, shape: "chip" });
      shockRing(stage, "gold", delay + 100, { size: 1.3, duration: 620, thick: 2 });
    },
    swap(stage, target, delay) {
      shockRing(stage, "neutral", delay, { size: 1.4, duration: 560, thick: 2 });
      shockRing(target, "neutral", delay + 180, { size: 1.2, duration: 560, thick: 2 });
      particles(stage, { tone: "neutral", count: 10, delay: delay + 60, distance: 88, shape: "chip" });
    },
    bolt(stage, target, delay) {
      flashBurst(target, "gear", delay + 120, .7);
      shockRing(target, "gear", delay + 140, { size: .95, duration: 480, thick: 2 });
      particles(target, { tone: "gear", count: 7, delay: delay + 150, distance: 54, gravity: 18, shape: "chip" });
    },
    none() {},
  };

  /* ---------- the play ceremony ----------
     A played card is the one thing the board cannot show on its own: it
     leaves a hand and lands on a pile in the same frame. So every play — the
     opponent's above all — is replayed: the card takes centre stage face up,
     a plaque names who played what, the motif fires at the target, and the
     card leaves along its own exit path. */

  // Where the card comes from: its own seat when the player could see it,
  // otherwise a card-shaped patch of the owner's hand. `known` is false for a
  // card that was face down until now — the one that earns the flip.
  function ceremonyOrigin(plan, oldRects, oppFrom) {
    const seat = plan.instance !== null ? oldRects.get(`i${plan.instance}`) : null;
    if (seat?.width) return { rect: seat, known: true };
    const anchor = plan.isHuman
      ? (oldRects.get("ownDeck") || oldRects.get("discard"))
      : (oppFrom || oldRects.get("oppDeck"));
    if (!anchor?.width) return null;
    const width = anchor.height * .643;
    return {
      rect: { left: anchor.left + anchor.width / 2 - width / 2, top: anchor.top, width, height: anchor.height },
      known: false,
    };
  }

  // The staged card and its plaque are one block, centred on the felt — the
  // plaque must never spill onto the character rows below.
  const PLAQUE_ROOM = 76;

  function centreStageRect(felt) {
    const height = Math.max(84, Math.min(felt.height - PLAQUE_ROOM, 250));
    const width = height * .643;
    /* On a portrait stage the camera is looking down the field from your own
       end, so your character stands in the bottom of the frame and the empty
       dust is above them. Dead-centre would park the staged card on the one
       figure most worth watching, so it rides higher there. */
    const room = felt.height - height - PLAQUE_ROOM;
    const fromTop = felt.width / felt.height < 1.5 ? room * 0.3 : room / 2;
    return {
      left: felt.left + felt.width / 2 - width / 2,
      top: felt.top + fromTop,
      width, height,
    };
  }

  function runPlayCeremonies(plans, next, oldRects, { oppFrom, discardTo, instanceRect, instanceEl }) {
    const handled = new Set();
    if (!plans.length) return handled;
    const felt = app.querySelector(".table-felt")?.getBoundingClientRect();
    if (!felt?.width) return handled;
    const stage = centreStageRect(felt);
    const kinds = cardKindsByKey(next);
    let discardBusyUntil = 0;

    for (const plan of plans) {
      const motif = plan.motif;
      const origin = ceremonyOrigin(plan, oldRects, oppFrom);
      if (!origin) continue;
      const from = origin.rect;
      const equipping = plan.destination === "equipment" && plan.instance !== null;
      const to = (equipping ? instanceRect(plan.instance) : discardTo) || discardTo;
      if (!to) continue;
      if (plan.instance !== null) handled.add(plan.instance);

      // 1 — the card takes the stage, flipping face up if it was the
      //     opponent's hidden hand it came out of
      flyGhost({
        html: ghostArt(plan.card.key), from, to: stage,
        delay: plan.enterAt, duration: FX_TIMING.enter, linger: plan.holdMs,
        flip: !origin.known,
        side: sideClass(plan.card.origin || next.players[plan.player]?.side),
        easing: "cubic-bezier(.2,.9,.25,1.05)", tone: motif.tone,
      });

      // 2 — say it in words, so the opponent's turn is never a mystery
      const who = plan.isHuman ? "You" : `${next.players[plan.player]?.side || "Opponent"}`;
      stageCaption(stage, playCaption(plan, who), plan.card.name || plan.key,
                   motif.tone, plan.enterAt + 120, plan.holdMs + FX_TIMING.enter - 120);

      // 3 — the signature burst, aimed at whatever the card was played on
      const targetEl = plan.zone
        ? app.querySelector(`[data-zone="${effectiveDropZone(plan.zone, next)}"] .char`)
        : null;
      const targetRect = targetEl?.getBoundingClientRect() || stage;
      (BURSTS[motif.burst] || BURSTS.none)(stage, targetRect, plan.impactAt);
      if (motif.shake) tableShake(plan.impactAt + 120);
      if (plan.verb === "avoid") calloutBanner("MISSED!", "missed", plan.impactAt);
      else if ((plan.card.kind || kinds.get(plan.key)) === "bang") calloutBanner("BANG!", "bang", plan.impactAt);

      // 4 — and out it goes, each card in its own fashion. The exit ghost is
      //     spawned a frame early so it takes over without a blink.
      const HANDOVER = 60;
      setTimeout(() => flyGhost({
        html: ghostArt(plan.card.key), from: stage, to, delay: HANDOVER,
        duration: plan.exitMs, path: motif.exit, tone: motif.tone,
        easing: motif.exit === "settle" ? "cubic-bezier(.3,.7,.4,1)" : "cubic-bezier(.35,.1,.25,1)",
      }), Math.max(0, plan.exitAt - HANDOVER));
      sfx.play(motif.exit === "slot" ? "snap" : "whoosh", plan.exitAt);

      if (equipping) concealFor(instanceEl(plan.instance), plan.endsAt);
      else discardBusyUntil = Math.max(discardBusyUntil, plan.endsAt);
    }

    if (discardBusyUntil) concealFor(app.querySelector(".discard-spot .pcard"), discardBusyUntil);
    // The staged card sits where the phase pill lives; step the pill aside
    // rather than let the two fight over the middle of the felt.
    const until = plans[plans.length - 1].endsAt;
    app.classList.add("staging");
    clearTimeout(stagingTimer);
    stagingTimer = setTimeout(() => app.classList.remove("staging"), until);
    fxBusyUntil = Math.max(fxBusyUntil, performance.now() + until);
    return handled;
  }

  // Hide the real element while its ghost is in flight.
  function concealFor(el, totalMs) {
    if (!el) return;
    el.style.visibility = "hidden";
    setTimeout(() => { el.style.visibility = ""; }, totalMs);
  }

  function floatText(text, rect, tone, delay = 0) {
    if (!rect) return;
    const el = document.createElement("div");
    el.className = `fx-float ${tone}`;
    el.textContent = text;
    el.style.left = `${rect.left + rect.width / 2}px`;
    el.style.top = `${rect.top + rect.height * 0.35}px`;
    fxRoot.appendChild(el);
    el.animate([
      { transform: "translate(-50%, 0) scale(.6)", opacity: 0 },
      { transform: "translate(-50%, -14px) scale(1.15)", opacity: 1, offset: .25 },
      { transform: "translate(-50%, -52px) scale(1)", opacity: 0 },
    ], { duration: 1100, delay, easing: "ease-out", fill: "both" }).onfinish = () => el.remove();
    setTimeout(() => el.remove(), delay + 1800);
  }

  // Big western display text flashed over the felt (BANG!, MISSED!, …).
  function calloutBanner(text, tone, delay = 0) {
    const el = document.createElement("div");
    el.className = `fx-callout ${tone}`;
    el.textContent = text;
    fxRoot.appendChild(el);
    el.animate([
      { transform: "translate(-50%, -50%) scale(.35) rotate(-7deg)", opacity: 0 },
      { transform: "translate(-50%, -50%) scale(1.12) rotate(-2deg)", opacity: 1, offset: .22 },
      { transform: "translate(-50%, -50%) scale(1) rotate(-2deg)", opacity: 1, offset: .68 },
      { transform: "translate(-50%, -58%) scale(1.05) rotate(-2deg)", opacity: 0 },
    ], { duration: 1150, delay, easing: "cubic-bezier(.2,1.2,.35,1)", fill: "both" }).onfinish = () => el.remove();
    setTimeout(() => el.remove(), delay + 1900);
  }

  // Death ceremony: a skull bursts over the fallen character's slot.
  function skullPop(rect, delay = 0) {
    if (!rect) return;
    const el = document.createElement("div");
    el.className = "fx-skull";
    el.textContent = "☠";
    el.style.left = `${rect.left + rect.width / 2}px`;
    el.style.top = `${rect.top + rect.height / 2}px`;
    fxRoot.appendChild(el);
    el.animate([
      { transform: "translate(-50%, -50%) scale(.2) rotate(-30deg)", opacity: 0 },
      { transform: "translate(-50%, -50%) scale(1.5) rotate(8deg)", opacity: 1, offset: .3 },
      { transform: "translate(-50%, -50%) scale(1.25) rotate(0deg)", opacity: 1, offset: .62 },
      { transform: "translate(-50%, -64%) scale(1.1)", opacity: 0 },
    ], { duration: 1300, delay, easing: "cubic-bezier(.25,1.3,.4,1)", fill: "both" }).onfinish = () => el.remove();
    setTimeout(() => el.remove(), delay + 2100);
  }

  /* A fallen character topples out of its seat: the card blanches, tips over
     the edge of the felt and slides away, kicking up dust as it goes. A
     character swept off after already lying at zero life gets the short
     version — it was toppled and greyed the moment it went down. */
  function characterFall(rect, key, { delay = 0, swept = false } = {}) {
    if (!rect?.width) return 0;
    const ghost = document.createElement("div");
    ghost.className = "fx-ghost fx-fallen";
    ghost.style.left = `${rect.left}px`;
    ghost.style.top = `${rect.top}px`;
    ghost.style.width = `${rect.width}px`;
    ghost.style.height = `${rect.height}px`;
    ghost.innerHTML = ghostArt(key);
    fxRoot.appendChild(ghost);
    const duration = swept ? 380 : 780;
    const frames = swept
      ? [
          { transform: "rotate(-7deg) translate(0, 0)", filter: "grayscale(1) brightness(.8)", opacity: .8 },
          { transform: "rotate(-70deg) translate(-10px, 16px)", filter: "grayscale(1) brightness(.5)", opacity: 0 },
        ]
      : [
          { transform: "rotate(0deg) translate(0, 0)", filter: "grayscale(0) brightness(1)", opacity: 1 },
          { transform: "rotate(4deg) translate(0, -7px)", filter: "grayscale(.35) brightness(1.3)", opacity: 1, offset: .14 },
          { transform: "rotate(-34deg) translate(-6px, 2px)", filter: "grayscale(.9) brightness(.85)", opacity: 1, offset: .5 },
          { transform: "rotate(-76deg) translate(-14px, 12px)", filter: "grayscale(1) brightness(.6)", opacity: .9, offset: .78 },
          { transform: "rotate(-84deg) translate(-22px, 26px)", filter: "grayscale(1) brightness(.45)", opacity: 0 },
        ];
    ghost.animate(frames, {
      duration, delay, fill: "both",
      easing: swept ? "ease-in" : "cubic-bezier(.5,.02,.6,1)",
    }).onfinish = () => ghost.remove();
    setTimeout(() => ghost.remove(), delay + duration + 700);
    if (!swept) {
      // dust off the felt where the card hits it
      particles(rect, {
        tone: "gear", count: 11, delay: delay + 330, spread: Math.PI * .9,
        aim: -Math.PI / 2, distance: 58, gravity: 44, duration: 760,
        shape: "chip", size: 6,
      });
    }
    return duration;
  }

  /* A replacement takes the empty seat: the card rides in from the reserve —
     flipping face up on the way when it was standing face down — and lands
     with a thump of dust and a flare of its life pips. */
  function characterArrival(cid, key, from, { delay = 0, flip = false, side = "", name = "" } = {}) {
    const el = app.querySelector(`[data-cid="${cid}"]`);
    const to = el?.getBoundingClientRect();
    if (!to?.width) return;
    const duration = 540;
    if (from?.width) {
      flyGhost({
        html: ghostArt(key), from, to, delay, duration, flip, side,
        easing: "cubic-bezier(.3,1.05,.35,1)",
      });
      concealFor(el, delay + duration);
    }
    const landed = delay + (from?.width ? duration : 0);
    shockRing(to, "gold", landed, { size: .85, duration: 520, thick: 2 });
    particles(to, {
      tone: "gear", count: 9, delay: landed, spread: Math.PI * 1.1,
      aim: Math.PI / 2, distance: 46, gravity: -10, duration: 520,
      shape: "chip", size: 5,
    });
    el.animate([
      { transform: "translateY(-6px) scale(1.06)", filter: "brightness(1.35) drop-shadow(0 0 18px rgba(232,191,106,.9))" },
      { transform: "translateY(2px) scale(.99)", filter: "brightness(1.1)", offset: .45 },
      { transform: "translateY(0) scale(1)", filter: "none" },
    ], { duration: 460, delay: landed, easing: "cubic-bezier(.3,1.4,.5,1)" });
    if (name) stageCaption(to, "takes the seat", name, "gold", landed, 620);
  }

  function tableShake(delay = 0) {
    app.querySelector(".table-wrap")?.animate([
      { transform: "translate(0, 0)" },
      { transform: "translate(-5px, 2px)" },
      { transform: "translate(4px, -2px)" },
      { transform: "translate(-3px, 1px)" },
      { transform: "translate(2px, -1px)" },
      { transform: "translate(0, 0)" },
    ], { duration: 380, delay, easing: "ease-out" });
  }

  function collectLife(state) {
    const life = new Map();
    for (const player of state.players) {
      for (const position of player.positions) {
        if (position.character) life.set(position.character.id, position.life);
      }
    }
    return life;
  }

  function collectEquipment(state) {
    const equipment = new Set();
    for (const player of state.players) {
      for (const position of player.positions) {
        for (const card of position.equipment || []) equipment.add(card.instance);
      }
    }
    return equipment;
  }

  // key → card kind, learned from every card the player can legitimately see.
  // Lets presentation code (sounds, banners) tell BANG!-kind cards apart
  // without hard-coding any card list.
  function cardKindsByKey(state) {
    const kinds = new Map();
    const register = card => { if (card?.key) kinds.set(card.key, card.kind); };
    (state.discard || []).forEach(register);
    (state.choiceCards || []).forEach(register);
    (state.revealedCards || []).forEach(register);
    for (const player of state.players) {
      (player.hand || []).forEach(register);
      (player.knownHand || []).forEach(register);
      for (const position of player.positions) (position.equipment || []).forEach(register);
    }
    return kinds;
  }

  // Log entries that appeared between two states (the server trims long logs).
  function newLogEntries(prev, next) {
    return prev.log.length <= next.log.length ? next.log.slice(prev.log.length) : [];
  }

  /* ---------- match stats, accumulated from the per-state diffs ---------- */
  let matchStats = null; // { sessionId, damageDealt, damageTaken, cardsPlayed, shotsAvoided }

  function updateMatchStats(prev, next) {
    if (!matchStats || matchStats.sessionId !== next.sessionId) {
      matchStats = {
        sessionId: next.sessionId, damageDealt: 0, damageTaken: 0, cardsPlayed: 0, shotsAvoided: 0,
        bestBurst: 0, takedowns: 0, streak: 0, bestStreak: 0, streakTurn: null,
      };
    }
    const me = next.humanPlayer;
    const owner = new Map(); // character id → player index
    [prev, next].forEach(state => state.players.forEach((player, index) =>
      player.positions.forEach(position => {
        if (position.character) owner.set(position.character.id, index);
      })));
    const prevLife = collectLife(prev);
    const nextLife = collectLife(next);
    /* Damage the player landed in this one state change — the unit a "double
       hit" is counted in. It is per diff rather than per turn because that is
       the beat the board animates on: one card resolving, everything it did
       arriving together. */
    let burst = 0;
    let takedowns = 0;
    for (const [cid, life] of nextLife) {
      const before = prevLife.get(cid);
      if (before === undefined || before <= life) continue;
      if (owner.get(cid) === me) matchStats.damageTaken += before - life;
      else { matchStats.damageDealt += before - life; burst += before - life; }
      if (owner.get(cid) !== me && before > 0 && life <= 0) takedowns += 1;
    }
    // A character can also leave the board outright between two states.
    for (const [cid] of prevLife) {
      if (nextLife.has(cid) || owner.get(cid) === me || (prevLife.get(cid) ?? 0) <= 0) continue;
      takedowns += 1;
    }
    matchStats.takedowns += takedowns;
    matchStats.bestBurst = Math.max(matchStats.bestBurst, burst);

    /* The streak: turns of yours in a row that drew blood. Counted once per
       turn number, so three hits in one turn is a streak of one, not three. */
    if (burst > 0 && matchStats.streakTurn !== next.turnNumber) {
      matchStats.streakTurn = next.turnNumber;
      matchStats.streak += 1;
      matchStats.bestStreak = Math.max(matchStats.bestStreak, matchStats.streak);
    } else if (next.currentPlayer !== me && prev.currentPlayer === me &&
               matchStats.streakTurn !== prev.turnNumber) {
      matchStats.streak = 0;   // your turn ended without landing anything
    }

    for (const entry of newLogEntries(prev, next)) {
      if (entry.player !== me) continue;
      const engine = entry.engineText || "";
      if (/^(?:play|play_as_colt)\(/.test(engine)) matchStats.cardsPlayed += 1;
      else if (engine.startsWith("avoid(")) matchStats.shotsAvoided += 1;
    }
    return { burst, takedowns };
  }

  /* The one moment worth clipping: several points of damage landing on one
     card, or a character going down. It gets a display-type callout of its
     own, sized to how big it was. */
  const BURST_WORDS = [null, null, "DOUBLE HIT", "TRIPLE HIT", "MASSACRE"];

  function comboCallout({ burst, takedowns }, at) {
    if (reducedMotion) return;
    if (takedowns > 1) { bigCallout(`${takedowns}× TAKEDOWN`, "takedown", at); return; }
    if (takedowns === 1) { bigCallout("TAKEDOWN", "takedown", at); return; }
    if (burst >= 2) {
      bigCallout(BURST_WORDS[Math.min(burst, 4)] || `${burst}× HIT`, "combo", at);
      return;
    }
    /* A run of scoring turns is worth saying out loud — but only when it
       reaches a number worth saying. Stamping the screen every turn of a long
       run turns the reward into wallpaper, so it lands on milestones and then
       gets out of the way. */
    if (matchStats?.streakTurn === game?.turnNumber && STREAK_MARKS.has(matchStats.streak)) {
      bigCallout(`${matchStats.streak} TURN STREAK`, "streak", at);
    }
  }

  const STREAK_MARKS = new Set([3, 5, 8, 12, 17]);

  function bigCallout(text, tone, delay = 0) {
    const el = document.createElement("div");
    el.className = `fx-callout fx-combo ${tone}`;
    el.textContent = text;
    fxRoot.appendChild(el);
    el.animate([
      { transform: "translate(-50%, -50%) rotate(-2deg) scale(.4)", opacity: 0 },
      { transform: "translate(-50%, -50%) rotate(-2deg) scale(1.18)", opacity: 1, offset: .18 },
      { transform: "translate(-50%, -50%) rotate(-2deg) scale(1)", opacity: 1, offset: .32 },
      { transform: "translate(-50%, -58%) rotate(-2deg) scale(1.04)", opacity: 1, offset: .74 },
      { transform: "translate(-50%, -74%) rotate(-2deg) scale(1.1)", opacity: 0 },
    ], { duration: 1250, delay, easing: "cubic-bezier(.2,1.4,.4,1)", fill: "both" })
      .onfinish = () => el.remove();
    setTimeout(() => el.remove(), delay + 2000);
    tableShake(delay);
  }

  /* ---------- sound cues from the state diff ----------
     Runs even when reduced motion silences the visuals: sound is a separate
     channel with its own mute toggle. */
  function soundCues(prev, next, plans) {
    const me = next.humanPlayer;

    // every play speaks in its own voice, on the beat its ceremony lands
    for (const plan of plans) sfx.play(plan.motif.sound, plan.impactAt);

    // life swings and deaths
    const prevLife = collectLife(prev);
    const nextLife = collectLife(next);
    for (const [cid, life] of nextLife) {
      const before = prevLife.get(cid);
      if (before === undefined || before === life) continue;
      sfx.play(life < before ? "hit" : "heal", 300);
      if (before > 0 && life <= 0) sfx.play("death", 420);
    }
    for (const [cid] of prevLife) {
      if (!nextLife.has(cid)) sfx.play("death", 380);
    }

    // a replacement riding in from the reserve: the flip, then the landing
    characterEvents(prev, next).arrivals.forEach((who, index) => {
      const at = (who.replacement ? 900 : 300) + index * 160;
      sfx.play("flip", at);
      sfx.play("snap", at + 420);
    });

    // staged reveals (dynamite, rattlesnake, barrel checks, …)
    const prevRevealSeq = Math.max(0, ...(prev.revealEvents || []).map(event => event.sequence));
    (next.revealEvents || []).filter(event => event.sequence > prevRevealSeq)
      .forEach((event, index) => {
        const at = index * 700 + 420;
        if (event.reason === "dynamite" && event.triggered) sfx.play("boom", at + 500);
        else if (event.reason === "rattlesnake") sfx.play("rattle", at);
        else sfx.play("flip", at);
      });

    // draws on both sides
    const prevHand = new Set((prev.players[me].hand || []).map(card => card.instance));
    let draws = 0;
    for (const card of next.players[me].hand || []) {
      if (!prevHand.has(card.instance)) { sfx.play("draw", 240 + draws * 90); draws += 1; }
    }
    if (next.players[1 - me].handCount > prev.players[1 - me].handCount) sfx.play("draw", 300);

    // your-turn chime and the end-of-duel sting
    if (!next.terminal && next.currentPlayer === me && prev.currentPlayer !== me) sfx.play("turn", 500);
    if (next.terminal && !prev.terminal) sfx.play(next.winner === me ? "win" : "lose", 700);
  }

  /* Every resolved play, restaged on the battlefield. The motif and the beat
     are the ones the 2D ceremony and the sound already run on, so the card
     landing on the felt, the crack of the shot and the figure taking the hit
     are the same moment seen three ways. */
  function stageCeremonies(plans) {
    if (!voxelStage.available()) return;
    for (const plan of plans) {
      voxelStage.api.ceremony(plan.motif.id, {
        actorZone: plan.isHuman ? "own_active" : "opponent_active",
        targetZone: plan.zone,
        cardKey: plan.key,
        delay: plan.impactAt,
      });
    }
  }

  // Reveals the engine resolved on its own (Dynamite, Rattlesnake, a Barrel
  // check) run before anything a player chose to do.
  function newRevealEvents(prev, next) {
    const seen = Math.max(0, ...(prev.revealEvents || []).map(event => event.sequence));
    return (next.revealEvents || []).filter(event => event.sequence > seen);
  }

  function runAnimations(prev, next, oldRects) {
    if (prev.sessionId !== next.sessionId) return;
    const damage = updateMatchStats(prev, next);
    // One schedule drives both channels, so the gunshot cracks on the frame
    // the bullet lands.
    const plans = planPlays(prev, next, {
      reducedMotion,
      startAt: reducedMotion ? 0 : newRevealEvents(prev, next).length * 700,
    });
    soundCues(prev, next, plans);
    stageCeremonies(plans);
    // The combo lands a beat after the last card that caused it, so it reads
    // as the consequence rather than as part of the play.
    comboCallout(damage, Math.max(320, ...plans.map(plan => plan.impactAt + 340)));
    if (reducedMotion) return;
    const me = next.humanPlayer;
    const newRect = selector => app.querySelector(selector)?.getBoundingClientRect();
    const instanceEl = instance => app.querySelector(`[data-instance="${instance}"]`);
    const instanceRect = instance => instanceEl(instance)?.getBoundingClientRect();

    const prevHand = new Set((prev.players[me].hand || []).map(card => card.instance));
    const nextHand = new Set((next.players[me].hand || []).map(card => card.instance));
    const prevEquipment = collectEquipment(prev);
    const nextEquipment = collectEquipment(next);
    const oppFrom = oldRects.get("oppBacks") || oldRects.get("oppDeck");
    const discardTo = newRect(".discard-spot") || newRect(".table-piles");

    /* reveal events (Dynamite, Rattlesnake, Barrel checks the engine resolved
       on its own) → the flipped card takes center stage, then slides to the
       discard pile */
    const prevRevealSeq = Math.max(0, ...(prev.revealEvents || []).map(event => event.sequence));
    const prevRevealedSet = new Set((prev.revealedCards || []).map(card => card.instance));
    const newEvents = (next.revealEvents || []).filter(event => event.sequence > prevRevealSeq);
    const revealHandled = new Set();
    let revealDelay = 0;
    const REVEAL_LABELS = { dynamite: "Dynamite!", rattlesnake: "Rattlesnake!", barrel: "Barrel", sid_curry: "Sid Curry" };
    for (const event of newEvents) {
      revealHandled.add(event.card.instance);
      if (prevRevealedSet.has(event.card.instance)) continue; // was chosen on-screen already
      const side = sideClass(next.players[event.player]?.side);
      const from = oldRects.get(`${side}Deck`) ||
                   (event.player === me ? oldRects.get("ownDeck") : oldRects.get("oppDeck"));
      const felt = newRect(".table-felt");
      if (!from || !felt) continue;
      const width = from.width * 1.9;
      const height = from.height * 1.9;
      const stage = {
        left: felt.left + felt.width / 2 - width / 2,
        top: felt.top + felt.height / 2 - height / 2,
        width, height,
      };
      flyGhost({ html: ghostArt(event.card.key), from, to: stage,
                 delay: revealDelay, duration: 480, flip: true, side });
      if (discardTo) {
        setTimeout(() => flyGhost({ html: ghostArt(event.card.key), from: stage, to: discardTo,
                                    duration: 420, path: motifFor(event.card).exit }),
                   revealDelay + 1150);
      }
      floatText(REVEAL_LABELS[event.reason] || "Reveal",
                { ...stage, top: stage.top - 8 },
                event.reason === "barrel" || event.reason === "sid_curry"
                  ? (event.triggered ? "heal" : "damage")
                  : (event.triggered ? "damage" : "heal"),
                revealDelay + 420);
      revealDelay += 700;
    }

    /* play ceremonies — every card either side commits takes centre stage
       with a plaque saying who played what, bursts in its own signature, and
       then leaves for the discard pile along its motif's exit path */
    const ceremonyHandled = runPlayCeremonies(plans, next, oldRects, { oppFrom, discardTo, instanceRect, instanceEl });

    /* cards played / discarded → fly onto the discard pile */
    const nextRevealedSet = new Set((next.revealedCards || []).map(card => card.instance));
    const prevDiscardCount = prev.discard?.length || 0;
    if ((next.discard?.length || 0) >= prevDiscardCount) {
      const played = (next.discard || []).slice(prevDiscardCount)
        .filter(card => !revealHandled.has(card.instance) && !nextRevealedSet.has(card.instance) &&
                        !ceremonyHandled.has(card.instance))
        .slice(-4);
      played.forEach((card, index) => {
        const from = oldRects.get(`i${card.instance}`) || oppFrom;
        // Even a plain discard leaves the way its card would.
        flyGhost({ html: ghostArt(card.key), from, to: discardTo, delay: index * 110,
                   duration: 420, path: motifFor(card).exit });
      });
      if (played.length) {
        concealFor(app.querySelector(".discard-spot .pcard"),
                   (played.length - 1) * 110 + 420);
      }
    }

    /* revealed cards leaving the center after a choice → slide to discard */
    for (const card of prev.revealedCards || []) {
      if (nextRevealedSet.has(card.instance)) continue;
      flyGhost({ html: ghostArt(card.key), from: oldRects.get(`i${card.instance}`),
                 to: discardTo, duration: 420, path: motifFor(card).exit });
    }

    /* equipment entering play → fly from its previous home onto the character */
    for (const player of next.players) {
      for (const position of player.positions) {
        for (const card of position.equipment || []) {
          if (nextEquipment.has(card.instance) && !prevEquipment.has(card.instance) &&
              !ceremonyHandled.has(card.instance)) {
            const from = oldRects.get(`i${card.instance}`) || oppFrom;
            const to = instanceRect(card.instance);
            flyGhost({ html: ghostArt(card.key), from, to, duration: 460, path: "slot" });
            concealFor(instanceEl(card.instance), 460);
          }
        }
      }
    }

    /* my draws → flip out of my deck into the hand */
    let drawIndex = 0;
    for (const card of next.players[me].hand || []) {
      if (prevHand.has(card.instance)) continue;
      const stolen = oldRects.has(`i${card.instance}`);
      const from = stolen ? oldRects.get(`i${card.instance}`) : oldRects.get("ownDeck");
      const to = instanceRect(card.instance);
      const delay = 240 + drawIndex * 90;
      flyGhost({ html: ghostArt(card.key), from, to, delay, duration: 430,
                 flip: !stolen, side: sideClass(card.origin) });
      concealFor(instanceEl(card.instance), delay + 430);
      drawIndex += 1;
    }

    /* opponent draws → card backs slide from their deck into their hand */
    const opp = 1 - me;
    const oppGain = next.players[opp].handCount - prev.players[opp].handCount;
    const oppBacksTo = newRect(".row.opponent .backs-fan") || newRect(".row.opponent .deck-stack");
    for (let index = 0; index < Math.min(oppGain, 4); index += 1) {
      flyGhost({
        html: `<span class="mini-back ${sideClass(next.players[opp].side)} fx-fill"></span>`,
        from: oldRects.get("oppDeck"), to: oppBacksTo,
        delay: 240 + index * 90, duration: 360,
      });
    }

    /* reveals (Dynamite, Rattlesnake, Barrel, …) → flip out of the owning deck */
    const prevRevealed = new Set((prev.revealedCards || []).map(card => card.instance));
    (next.revealedCards || []).forEach((card, index) => {
      if (prevRevealed.has(card.instance)) return;
      const side = sideClass(card.origin);
      const from = oldRects.get(`${side}Deck`) || oldRects.get("ownDeck");
      const to = instanceRect(card.instance);
      const delay = 140 + index * 130;
      flyGhost({ html: ghostArt(card.key), from, to, delay, duration: 500, flip: true, side });
      concealFor(instanceEl(card.instance), delay + 500);
    });

    /* choice rows (General Store, Wagon, …) → pop in */
    const prevChoice = new Set((prev.choiceCards || []).map(card => card.instance));
    (next.choiceCards || []).forEach((card, index) => {
      if (prevChoice.has(card.instance)) return;
      instanceEl(card.instance)?.animate([
        { transform: "scale(.4) translateY(14px)", opacity: 0 },
        { transform: "scale(1) translateY(0)", opacity: 1 },
      ], { duration: 320, delay: 140 + index * 70, easing: "cubic-bezier(.3,1.4,.5,1)", fill: "backwards" });
    });

    /* life changes → flash, shake, floating bullets; eliminations → skull */
    const prevLife = collectLife(prev);
    const nextLife = collectLife(next);
    for (const [cid, life] of nextLife) {
      const before = prevLife.get(cid);
      if (before === undefined || before === life) continue;
      const el = app.querySelector(`[data-cid="${cid}"]`);
      const rect = el?.getBoundingClientRect() || oldRects.get(`c${cid}`);
      const delta = life - before;
      floatText(delta < 0 ? String(delta) : `+${delta}`, rect, delta < 0 ? "damage" : "heal", 320);
      if (voxelStage.available()) {
        if (delta < 0) voxelStage.api.hit(cid, 300);
        else voxelStage.api.heal(cid, 300);
      }
      if (el && delta < 0) {
        el.animate([
          { filter: "none" },
          { filter: "drop-shadow(0 0 20px rgba(219,75,53,.95)) saturate(1.6) brightness(1.15)" },
          { filter: "none" },
        ], { duration: 550, delay: 300 });
        el.animate([
          { transform: "translateX(0)" }, { transform: "translateX(-6px)" },
          { transform: "translateX(5px)" }, { transform: "translateX(-3px)" },
          { transform: "translateX(0)" },
        ], { duration: 320, delay: 300 });
      }
      if (el && delta > 0) {
        el.animate([
          { filter: "none" },
          { filter: "drop-shadow(0 0 18px rgba(232,191,106,.9)) brightness(1.12)" },
          { filter: "none" },
        ], { duration: 600, delay: 300 });
      }
      /* knockout: the character card topples, the skull bursts, the table shakes */
      if (el && before > 0 && life <= 0) {
        el.animate([
          { transform: "rotate(0deg) translateY(0)", filter: "grayscale(0)" },
          { transform: "rotate(-9deg) translateY(9px)", filter: "grayscale(1)", offset: .55 },
          { transform: "rotate(-6deg) translateY(6px)", filter: "grayscale(1)", offset: .8 },
          { transform: "rotate(-7deg) translateY(7px)", filter: "grayscale(1)" },
        ], { duration: 750, delay: 380, easing: "cubic-bezier(.3,.9,.4,1)", fill: "both" });
        skullPop(rect, 420);
        tableShake(420);
      }
      /* The same knockout on the battlefield, on the same beat: the figure
         goes down where it stood and the camera comes in to watch. It stays
         lying there afterwards, because the board keeps the seat until it is
         cleared. Outside the `el` guard above — the stage should react to a
         character dying whether or not its card is currently on screen. */
      if (before > 0 && life <= 0 && voxelStage.available()) {
        voxelStage.api.down(cid, 380);
      }
    }
    /* the fallen leaving the table, and the reserve riding in behind them */
    const seats = characterEvents(prev, next);
    let clearsAt = 0;
    for (const who of seats.fallen) {
      const rect = oldRects.get(`c${who.id}`);
      const delay = who.swept ? 200 : 380;
      const fall = characterFall(rect, who.key, { delay, swept: who.swept });
      if (!who.swept) {
        skullPop(rect, 420);
        tableShake(420);
        stageCaption(rect, "falls", who.name, "danger", delay + 160, 480);
      }
      clearsAt = Math.max(clearsAt, rect ? delay + fall * 0.7 : 0);
    }

    seats.arrivals.forEach((who, index) => {
      const known = oldRects.get(`c${who.id}`); // your reserve is face up: fly from it
      // Opening reveal: the seat was sitting face down, so it flips where it is.
      const from = known ||
        (who.replacement ? oldRects.get(who.own ? "ownReserve" : "oppReserve") : null) ||
        oldRects.get(`z${who.zone}`);
      characterArrival(who.id, who.key, from, {
        delay: (who.replacement ? Math.max(clearsAt, 320) : 260) + index * 160,
        flip: !known,
        side: sideClass(who.side),
        name: who.replacement ? who.name : "",
      });
    });

    /* hand reflow (FLIP) — surviving cards glide to their new fan slot */
    for (const el of app.querySelectorAll(".cards-fan .pcard")) {
      const instance = Number(el.dataset.instance);
      if (!prevHand.has(instance) || !nextHand.has(instance)) continue;
      const old = oldRects.get(`i${instance}`);
      const now = el.getBoundingClientRect();
      if (!old) continue;
      const dx = old.left - now.left;
      const dy = old.top - now.top;
      if (Math.hypot(dx, dy) < 5) continue;
      try {
        el.animate([
          { transform: `translate(${dx}px, ${dy}px)` },
          { transform: "translate(0, 0)" },
        ], { duration: 260, easing: "ease-out", composite: "add" });
      } catch { /* older engines without composite:add just skip the glide */ }
    }
  }

  function markZones(actions) {
    app.querySelectorAll("[data-zone].drop-ok").forEach(el => el.classList.remove("drop-ok", "drop-hot"));
    const zones = new Set(actions.map(zoneForAction).filter(Boolean));
    for (const zone of zones) {
      app.querySelector(`[data-zone="${zone}"]`)?.classList.add("drop-ok");
    }
  }

  // Zone marks come from the selected card, or — softer — from the card under
  // the cursor, so legal targets show before the card is even picked up.
  function updateZoneMarks() {
    const previewing = selectedCard === null && hoverPreview !== null && !drag;
    const actions = selectedCard !== null ? actionsForInstance(selectedCard)
      : previewing ? actionsForInstance(hoverPreview) : [];
    markZones(actions);
    app.classList.toggle("zone-preview", previewing && actions.length > 0);
  }

  /* ---------- toast / tooltip ---------- */
  function showToast(message) {
    toastEl.textContent = message;
    toastEl.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { toastEl.hidden = true; }, 4200);
  }

  function showTip(el) {
    const body = el.dataset.tip;
    const title = el.dataset.tipTitle;
    if (!body && !title) return;
    tipEl.innerHTML = `${title ? `<strong>${escapeHtml(title)}</strong>` : ""}${escapeHtml(body || "")}`;
    tipEl.hidden = false;
    const rect = el.getBoundingClientRect();
    const tipRect = tipEl.getBoundingClientRect();
    let left = Math.min(Math.max(8, rect.left + rect.width / 2 - tipRect.width / 2), window.innerWidth - tipRect.width - 8);
    let top = rect.top - tipRect.height - 10;
    if (top < 8) top = Math.min(rect.bottom + 10, window.innerHeight - tipRect.height - 8);
    tipEl.style.left = `${left}px`;
    tipEl.style.top = `${top}px`;
  }
  function hideTip() { tipEl.hidden = true; }

  /* ---------- card sheet (the touch stand-in for a hover tooltip) ----------
     Every rule the game has is written on a card, and on a desktop you read it
     by hovering. A phone has no hover, so the same text arrives on a long
     press instead, in a sheet from the bottom edge where the thumb already is.
     It is the same `data-tip` payload — nothing is written twice. */
  let sheetEl = null;

  function showCardSheet(el) {
    const title = el.dataset.tipTitle || "";
    const body = el.dataset.tip || "";
    if (!title && !body) return;
    closeCardSheet();
    const art = el.querySelector("img")?.getAttribute("src");
    sheetEl = document.createElement("div");
    sheetEl.className = "card-sheet-backdrop";
    sheetEl.innerHTML = `<div class="card-sheet" role="dialog" aria-modal="true" aria-label="${escapeHtml(title)}">
      ${art ? `<img src="${art}" alt="" draggable="false">` : "<span></span>"}
      <div>
        <h3 class="card-sheet-name">${escapeHtml(title)}</h3>
        <p class="card-sheet-body">${escapeHtml(body)}</p>
        <span class="card-sheet-hint">Tap anywhere to close</span>
      </div>
    </div>`;
    document.body.appendChild(sheetEl);
    if (navigator.vibrate) navigator.vibrate(12);
  }

  function closeCardSheet() {
    sheetEl?.remove();
    sheetEl = null;
  }

  /* A long press only counts if the finger stayed put: on the hand rail the
     same gesture, moved, is a scroll. */
  let pressTimer = null;
  let pressFrom = null;

  function armLongPress(event) {
    const el = event.target.closest?.("[data-tip]");
    clearTimeout(pressTimer);
    if (!el || !touchFirst) return;
    pressFrom = { x: event.clientX, y: event.clientY };
    pressTimer = setTimeout(() => { pressTimer = null; showCardSheet(el); }, 430);
  }

  function cancelLongPress(event) {
    if (event && pressFrom && pressTimer &&
        Math.hypot(event.clientX - pressFrom.x, event.clientY - pressFrom.y) < 10) return;
    clearTimeout(pressTimer);
    pressTimer = null;
  }

  /* ---------- chooser popover ---------- */
  function showChooser(actions, x, y) {
    closeChooser();
    const el = document.createElement("div");
    el.className = "chooser";
    el.innerHTML = actions.map(action => `<button data-action="${action.id}">
      <strong>${escapeHtml(action.label)}</strong>
      ${action.detail ? `<small>${escapeHtml(action.detail)}</small>` : ""}
    </button>`).join("");
    dragRoot.appendChild(el);
    const rect = el.getBoundingClientRect();
    el.style.left = `${Math.min(Math.max(8, x - rect.width / 2), window.innerWidth - rect.width - 8)}px`;
    el.style.top = `${Math.min(Math.max(8, y - rect.height - 14), window.innerHeight - rect.height - 8)}px`;
  }
  function closeChooser() { dragRoot.querySelector(".chooser")?.remove(); }

  /* ---------- drag & drop ---------- */
  let drag = null; // { instance, el, actions, startX, startY, active, ghost, grabX, grabY, startRect }

  function beginDrag(event) {
    drag.active = true;
    hideTip();
    closeChooser();
    const rect = drag.el.getBoundingClientRect();
    drag.startRect = rect;
    drag.grabX = drag.startX - rect.left;
    drag.grabY = drag.startY - rect.top;
    const ghost = drag.el.cloneNode(true);
    ghost.classList.add("drag-ghost");
    ghost.classList.remove("drag-src", "selected");
    ghost.style.width = `${rect.width}px`;
    ghost.style.height = `${rect.height}px`;
    dragRoot.appendChild(ghost);
    drag.ghost = ghost;
    drag.el.classList.add("drag-src");
    document.body.classList.add("dragging");
    app.classList.remove("zone-preview");
    markZones(drag.actions);
    moveDrag(event);
  }

  function moveDrag(event) {
    const x = event.clientX;
    const y = event.clientY;
    drag.ghost.style.transform = `translate(${x - drag.grabX}px, ${y - drag.grabY}px) rotate(3deg) scale(1.05)`;
    const hot = hitZone(x, y);
    app.querySelectorAll("[data-zone].drop-hot").forEach(el => { if (el !== hot) el.classList.remove("drop-hot"); });
    hot?.classList.add("drop-hot");
  }

  function hitZone(x, y) {
    for (const node of document.elementsFromPoint(x, y)) {
      const zone = node.closest?.("[data-zone]");
      if (zone?.classList.contains("drop-ok")) return zone;
    }
    return null;
  }

  function finishDrag(event) {
    const zoneEl = hitZone(event.clientX, event.clientY);
    const { actions, ghost, startRect } = drag;
    cleanupDragMarks();
    if (zoneEl) {
      ghost.remove();
      const matching = actions.filter(action => zoneForAction(action) === zoneEl.dataset.zone);
      if (matching.length === 1) {
        applyAction(matching[0].id);
      } else if (matching.length > 1) {
        showChooser(matching, event.clientX, event.clientY);
      }
    } else {
      // fly back to the hand, then remove
      ghost.classList.add("returning");
      ghost.style.transform = `translate(${startRect.left}px, ${startRect.top}px) rotate(0deg) scale(1)`;
      setTimeout(() => ghost.remove(), 240);
    }
    drag = null;
  }

  function cleanupDragMarks() {
    document.body.classList.remove("dragging");
    app.querySelectorAll(".drag-src").forEach(el => el.classList.remove("drag-src"));
    app.querySelectorAll("[data-zone].drop-ok, [data-zone].drop-hot")
      .forEach(el => el.classList.remove("drop-ok", "drop-hot"));
    updateZoneMarks();
  }

  function cancelDrag() {
    if (!drag) return;
    drag.ghost?.remove();
    cleanupDragMarks();
    drag = null;
  }

  document.addEventListener("pointerdown", event => {
    if (sheetEl) { closeCardSheet(); return; }
    cancelAutoPass(true); // taking control cancels a pending auto-pass
    armLongPress(event);
    if (busy || !game || game.terminal || event.button !== 0) return;
    const el = event.target.closest("[data-instance].actionable");
    if (!el) return;
    const instance = Number(el.dataset.instance);
    const actions = actionsForInstance(instance);
    if (!actions.length) return;
    drag = { instance, el, actions, startX: event.clientX, startY: event.clientY, active: false };
  });

  window.addEventListener("pointermove", event => {
    cancelLongPress(event);
    if (!drag) return;
    const travelled = Math.hypot(event.clientX - drag.startX, event.clientY - drag.startY);
    if (!drag.active) {
      /* On a touch screen the hand is a rail you swipe, so a finger that moves
         is scrolling it — not picking a card up and not, on release, tapping
         one either. Play there is tap-to-select then tap-the-target. */
      if (touchFirst) { if (travelled > 12) drag = null; return; }
      if (travelled < 7) return;
      if (!drag.actions.some(action => zoneForAction(action) !== null)) return; // nothing droppable
      beginDrag(event);
      return;
    }
    moveDrag(event);
  });

  window.addEventListener("pointerup", event => {
    cancelLongPress();
    if (!drag) return;
    if (drag.active) {
      finishDrag(event);
    } else {
      // plain click on a card: toggle selection / play when unambiguous
      const instance = drag.instance;
      drag = null;
      toggleCardSelection(instance, event.clientX, event.clientY);
    }
  });

  window.addEventListener("pointercancel", () => { cancelLongPress(); cancelDrag(); });
  window.addEventListener("blur", () => { cancelLongPress(); cancelDrag(); });
  document.addEventListener("scroll", () => cancelLongPress(), true);

  function toggleCardSelection(instance, x, y) {
    const actions = actionsForInstance(instance);
    // A card whose only resolution is a single table action (choose reveal,
    // pick from a choice row, …) applies immediately on click.
    if (actions.length === 1 && TABLE_KINDS.has(actions[0].kind)) {
      applyAction(actions[0].id);
      return;
    }
    // A single play with a single target also applies immediately: one click,
    // no extra confirmation.
    if (actions.length === 1 && !zoneForAction(actions[0])) {
      applyAction(actions[0].id);
      return;
    }
    if (actions.length > 1 && x) {
      const zones = new Set(actions.map(zoneForAction).filter(Boolean));
      if (!zones.size) { showChooser(actions, x, y); return; }
    }
    selectedCard = selectedCard === instance ? null : instance;
    render();
  }

  /* ---------- click handling (buttons, zones, characters) ---------- */
  document.addEventListener("click", event => {
    const actionButton = event.target.closest("[data-action]");
    if (actionButton) { applyAction(Number(actionButton.dataset.action)); return; }
    if (dragRoot.querySelector(".chooser") && !event.target.closest(".chooser")) { closeChooser(); return; }
    if (event.target.closest("[data-toggle-log]")) { logOpen = !logOpen; render(); return; }
    if (event.target.closest("[data-toggle-sound]")) { sfx.toggle(); render(); return; }
    if (event.target.closest("[data-share-card]")) { sharePoster(); return; }
    if (event.target.closest("[data-quick-play]")) { createGame(lastSetup()); return; }
    if (event.target.closest("[data-new-game]")) { showNewGameModal(); return; }
    if (event.target.closest("[data-coach-done]")) { coach.dismiss(); return; }
    if (event.target.closest("[data-close-modal]")) { modalRoot.innerHTML = ""; return; }
    if (busy || !game || game.terminal) return;
    // Click a highlighted zone while a card is selected → play it there.
    if (selectedCard !== null) {
      const zoneEl = event.target.closest("[data-zone].drop-ok");
      if (zoneEl) {
        const matching = actionsForInstance(selectedCard)
          .filter(action => zoneForAction(action) === zoneEl.dataset.zone);
        if (matching.length === 1) applyAction(matching[0].id);
        else if (matching.length > 1) showChooser(matching, event.clientX, event.clientY);
        return;
      }
    }
    // Click a character with a pending ability / setup choice.
    const charEl = event.target.closest("[data-char-id]");
    if (charEl && !event.target.closest("[data-instance]")) {
      const actions = actionsForCharacter(Number(charEl.dataset.charId));
      if (actions.length === 1) applyAction(actions[0].id);
      else if (actions.length > 1) showChooser(actions, event.clientX, event.clientY);
    }
  });

  document.addEventListener("dblclick", event => {
    if (busy || !game || game.terminal) return;
    const el = event.target.closest("[data-instance].actionable");
    if (!el) return;
    const actions = actionsForInstance(Number(el.dataset.instance));
    if (actions.length === 1) applyAction(actions[0].id);
  });

  document.addEventListener("keydown", event => {
    if (event.key === "Escape") { closeChooser(); if (selectedCard !== null) { selectedCard = null; render(); } return; }
    if ((event.key === "Enter" || event.key === " ") && event.target.matches?.("[data-instance].actionable")) {
      event.preventDefault();
      toggleCardSelection(Number(event.target.dataset.instance), 0, 0);
    }
  });

  document.addEventListener("mouseover", event => {
    if (document.body.classList.contains("dragging")) return;
    const el = event.target.closest("[data-tip]");
    if (el) showTip(el); else hideTip();
    // Preview the legal drop zones of the card under the cursor.
    const cardEl = event.target.closest("[data-instance].actionable");
    const instance = cardEl && myDecision() ? Number(cardEl.dataset.instance) : null;
    if (instance !== hoverPreview) { hoverPreview = instance; updateZoneMarks(); }
  });
  document.addEventListener("mouseout", event => {
    if (!event.relatedTarget?.closest?.("[data-tip]")) hideTip();
    if (!event.relatedTarget && hoverPreview !== null) {
      hoverPreview = null; // the cursor left the window
      updateZoneMarks();
    }
  });

  /* ---------- server round-trips ---------- */
  async function applyAction(actionId) {
    if (busy || !game) return;
    busy = true;
    closeChooser();
    render();
    try {
      game = await api(`/api/session/${encodeURIComponent(game.sessionId)}/action`, {
        method: "POST", body: JSON.stringify({ action: actionId }),
      });
      selectedCard = null;
      localStorage.setItem("bangDuelSession", game.sessionId);
    } catch (error) {
      if (/unknown session/i.test(error.message)) {
        // The server lost this game and could not restore it from disk.
        // Try once to re-fetch (a restart may still be settling), then offer
        // a fresh duel rather than a dead table.
        const recovered = await tryRecoverSession();
        if (!recovered) {
          localStorage.removeItem("bangDuelSession");
          showToast("That duel could not be recovered — deal a new one.");
          showNewGameModal();
        }
      } else {
        showToast(error.message);
      }
    } finally {
      busy = false;
      render();
    }
  }

  async function tryRecoverSession() {
    try {
      game = await api(`/api/session/${encodeURIComponent(game.sessionId)}`);
      selectedCard = null;
      showToast("The table was restored after a reload.");
      return true;
    } catch {
      return false;
    }
  }

  async function createGame(values = lastSetup()) {
    busy = true; selectedCard = null; modalRoot.innerHTML = "";
    matchStats = null;
    if (game) render();
    else app.innerHTML = `<div class="boot-card"><span class="boot-kicker">Dealing</span><strong>BANG! THE DUEL</strong><span class="boot-line"></span></div>`;
    try {
      game = await api("/api/new", { method: "POST", body: JSON.stringify(values) });
      rememberSetup(values);
      localStorage.setItem("bangDuelSession", game.sessionId);
    } catch (error) {
      showToast(error.message);
      if (!game) {
        app.innerHTML = `<div class="boot-card"><strong>Could not open the table</strong><p>${escapeHtml(error.message)}</p><button class="primary-btn" data-new-game>Try again</button></div>`;
      }
    } finally {
      busy = false;
      if (game) render();
    }
  }

  /* ---------- getting in ----------
     The first thing a new player met used to be a form with a roster dropdown
     and a numeric random seed. Those are the controls of a simulator, and they
     ask someone who has never played to make three decisions before they are
     allowed to find out what the game is. They are still all there, one tap
     away, behind "Custom duel" — but the default path from cold start to a
     dealt hand is now a single button. */

  const DEFAULT_SETUP = { humanPlayer: 0, charactersPerSide: 4 };

  // What the last duel was set up with, so "Ride again" means what it says.
  function lastSetup() {
    try {
      const stored = JSON.parse(localStorage.getItem("bangDuelSetup") || "null");
      if (stored && Number.isInteger(stored.humanPlayer) && Number.isInteger(stored.charactersPerSide)) {
        return { humanPlayer: stored.humanPlayer, charactersPerSide: stored.charactersPerSide };
      }
    } catch { /* a corrupt entry just falls back to the standard duel */ }
    return { ...DEFAULT_SETUP };
  }

  function rememberSetup(values) {
    // The seed is deliberately not kept: replaying one duel over and over is
    // what the seed box is for, not what "again" means.
    localStorage.setItem("bangDuelSetup", JSON.stringify({
      humanPlayer: values.humanPlayer, charactersPerSide: values.charactersPerSide,
    }));
  }

  function showSplash() {
    app.innerHTML = `<div class="splash">
      <div class="splash-card">
        <span class="splash-kicker">Four gunslingers · one street</span>
        <strong class="splash-title">BANG!<em>The Duel</em></strong>
        <p class="splash-line">Play cards, call your shots, outlast a trained AI.
          About five minutes, and the table teaches you as you go.</p>
        <button class="play-btn" data-quick-play>Play</button>
        <span class="splash-alts">
          <button class="splash-alt" data-new-game>Custom duel</button>
          <a class="splash-alt" href="about.html">About the AI</a>
        </span>
      </div>
    </div>`;
  }

  function showNewGameModal() {
    const setup = game ? { humanPlayer: game.humanPlayer, charactersPerSide: game.charactersPerSide } : lastSetup();
    const currentSide = setup.humanPlayer;
    const currentCharacters = setup.charactersPerSide;
    modalRoot.innerHTML = `<div class="modal-backdrop" role="dialog" aria-modal="true" aria-labelledby="new-game-title">
      <form class="modal" id="new-game-form">
        <h2 id="new-game-title">New duel</h2>
        <div class="side-pick">
          <label class="side-option law ${currentSide === 0 ? "picked" : ""}">
            <input type="radio" name="humanPlayer" value="0" ${currentSide === 0 ? "checked" : ""}>
            <span class="mini-back law"></span><strong>Law</strong><small>acts first</small>
          </label>
          <label class="side-option outlaw ${currentSide === 1 ? "picked" : ""}">
            <input type="radio" name="humanPlayer" value="1" ${currentSide === 1 ? "checked" : ""}>
            <span class="mini-back outlaw"></span><strong>Outlaws</strong><small>bigger hand</small>
          </label>
        </div>
        <div class="form-row">
          <label for="roster">Roster</label>
          <select id="roster" name="charactersPerSide">${[2, 3, 4, 5, 6, 8, 10, 12].map(value =>
            `<option value="${value}" ${value === currentCharacters ? "selected" : ""}>${value}${value === 4 ? " · standard" : ""}</option>`).join("")}</select>
          <label for="seed">Seed</label>
          <input id="seed" name="seed" type="number" min="1" max="2147483647" placeholder="random">
        </div>
        <div class="modal-actions">${game ? '<button type="button" class="ghost-btn" data-close-modal>Cancel</button>' : ""}<button class="primary-btn" type="submit">Deal</button></div>
      </form>
    </div>`;
  }

  document.addEventListener("change", event => {
    if (!event.target.matches?.('.side-option input')) return;
    modalRoot.querySelectorAll(".side-option").forEach(el =>
      el.classList.toggle("picked", el.querySelector("input").checked));
  });

  document.addEventListener("submit", event => {
    if (event.target.id !== "new-game-form") return;
    event.preventDefault();
    const form = new FormData(event.target);
    const values = {
      humanPlayer: Number(form.get("humanPlayer")),
      charactersPerSide: Number(form.get("charactersPerSide")),
    };
    const seed = Number(form.get("seed"));
    if (seed > 0) values.seed = seed;
    createGame(values);
  });

  async function start() {
    const stored = localStorage.getItem("bangDuelSession");
    if (stored) {
      try { game = await api(`/api/session/${encodeURIComponent(stored)}`); render(); return; }
      catch { localStorage.removeItem("bangDuelSession"); }
    }
    showSplash();
  }

  start();
})();
