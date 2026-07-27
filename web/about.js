(() => {
  "use strict";

  function initialize() {
    const data = window.BANG_ABOUT_DATA;
    const stage = document.querySelector("#visual-stage");
    const steps = [...document.querySelectorAll(".step")];
    const figureLabel = document.querySelector("[data-figure-label]");
    const figureText = document.querySelector("[data-figure-text]");
    const railLinks = [...document.querySelectorAll(".rail a")];

    if (!data || !stage || !steps.length) {
      if (stage) stage.textContent = "Training data unavailable.";
      return;
    }
    // Scroll dimming and figure rendering belong together: the class is only
    // set once this far, and is dropped again if anything below throws.
    document.documentElement.classList.add("js-scenes");

    const training = data.training;
    const evaluation = data.evaluation;
    const latest = evaluation.latest;
    const style = data.style;
    const calm = window.matchMedia
      && window.matchMedia("(prefers-reduced-motion: reduce)").matches;

    const compact = new Intl.NumberFormat("en", {
      notation: "compact",
      maximumFractionDigits: 1,
    });
    const number = new Intl.NumberFormat("en");
    const pct = value => `${(value * 100).toFixed(1)}%`;
    const dash = "—";
    const esc = value => String(value).replace(/[&<>"']/g, character => ({
      "&": "&amp;",
      "<": "&lt;",
      ">": "&gt;",
      '"': "&quot;",
      "'": "&#39;",
    })[character]);
    const labelCard = value => ({
      missed: "Missed!",
      return_fire: "Return Fire",
      strong_ale: "Strong Ale",
      general_store: "General Store",
      gun_belt: "Gun Belt",
    })[value] || String(value).replaceAll("_", " ")
      .replace(/\b\w/g, letter => letter.toUpperCase());

    /* ---------- copy driven by the training artifacts ---------- */

    const byUplift = [...latest.characters].sort((a, b) => b.uplift - a.uplift);
    const rankedCards = style.cards;
    const opponentResults = training.opponentResults || {};
    const opponentRate = key =>
      opponentResults[key] ? pct(opponentResults[key].winRate) : dash;
    const nameList = names =>
      names.length > 1
        ? `${names.slice(0, -1).join(", ")} and ${names.at(-1)}`
        : names[0] || dash;

    const copy = {
      "[data-training-games]": number.format(training.games),
      "[data-training-steps]": compact.format(training.environmentSteps),
      "[data-training-decisions]": compact.format(training.learnerDecisions),
      "[data-training-iterations]": number.format(training.iterations),
      "[data-batch-games]": number.format(training.batchGames),
      "[data-update-epochs]": number.format(training.updateEpochs),
      "[data-minibatches]": number.format(training.minibatches),
      "[data-target-kl]": training.targetKl.toFixed(3),
      "[data-rollout-hours]": (training.rolloutSeconds / 3600).toFixed(0),
      "[data-learn-hours]": (training.updateSeconds / 3600).toFixed(0),
      "[data-observation-size]": number.format(training.observationSize),
      "[data-action-size]": number.format(training.actionFeatureSize),
      "[data-privileged-size]": number.format(training.privilegedSize),
      "[data-parameter-count]": number.format(training.parameterCount),
      "[data-frozen-rate]": opponentRate("frozenCurrent"),
      "[data-history-rate]": opponentRate("pfspHistory"),
      "[data-heuristic-rate]": opponentRate("heuristic"),
      "[data-random-rate]": opponentRate("random"),
      "[data-eval-games]": number.format(latest.games),
      "[data-eval-checkpoints]": number.format(evaluation.points.length),
      "[data-law-rate]": pct(latest.lawWinRate),
      "[data-outlaw-rate]": pct(latest.outlawWinRate),
      "[data-mean-turns]": Math.round(latest.meanTurns || 0),
      "[data-matchup-spread]": Math.round((latest.matchupSpread || 0) * 100),
      "[data-style-window]": number.format(style.windowIterations),
      "[data-defense-rate]": pct(style.defenseRate),
      "[data-attack-rate]": pct(style.attackRate),
      "[data-ability-rate]": pct(style.abilityRate),
      "[data-rear-rate]": pct(style.rearTargetRate),
      "[data-cards-per-turn]": style.cardsPerTurn.toFixed(1),
      "[data-top-characters]": nameList(byUplift.slice(0, 2).map(c => c.name)),
      "[data-bottom-characters]": nameList(
        byUplift.slice(-2).reverse().map(c => c.name)),
      "[data-top-cards]": nameList(
        rankedCards.slice(0, 3).map(card => labelCard(card.name))),
      "[data-bottom-cards]": nameList(
        rankedCards.slice(-2).reverse().map(card => labelCard(card.name))),
    };

    const entropyPoints = training.points
      .filter(point => Number.isFinite(point.entropy));
    if (entropyPoints.length) {
      copy["[data-first-entropy]"] = pct(entropyPoints[0].entropy);
      copy["[data-final-entropy]"] = pct(entropyPoints.at(-1).entropy);
      copy["[data-first-confidence]"] = pct(entropyPoints[0].confidence);
      copy["[data-final-confidence]"] = pct(entropyPoints.at(-1).confidence);
    }

    for (const [selector, value] of Object.entries(copy)) {
      document.querySelectorAll(selector).forEach(element => {
        element.textContent = value;
      });
    }

    /* ---------- counters ---------- */

    const counters = {
      steps: { to: training.environmentSteps, format: value => compact.format(value) },
      parameters: { to: training.parameterCount, format: value => number.format(Math.round(value)) },
      hours: { to: training.elapsedSeconds / 3600, format: value => `${value.toFixed(0)}h` },
    };

    function countUp(element, to, format, duration = 1200) {
      if (calm || !window.requestAnimationFrame || !window.performance) {
        element.textContent = format(to);
        return;
      }
      const started = performance.now();
      function frame(now) {
        const progress = Math.min(1, (now - started) / duration);
        const eased = 1 - (1 - progress) ** 3;
        element.textContent = format(to * eased);
        if (progress < 1) requestAnimationFrame(frame);
      }
      requestAnimationFrame(frame);
    }

    document.querySelectorAll(".stat-strip [data-count]").forEach(element => {
      const counter = counters[element.dataset.count];
      if (counter) countUp(element, counter.to, counter.format);
    });

    function runFigureCounters(root) {
      root.querySelectorAll("[data-count-to]").forEach(element => {
        const to = Number(element.dataset.countTo);
        const kind = element.dataset.countFormat;
        const format = kind === "compact"
          ? value => compact.format(value)
          : kind === "hours"
            ? value => `${value.toFixed(1)}h`
            : value => number.format(Math.round(value));
        countUp(element, to, format, 1000);
      });
    }

    /* ---------- shared figure furniture ---------- */

    const defs = `
      <defs>
        <linearGradient id="g-accent" x1="0" y1="0" x2="1" y2="0">
          <stop offset="0" stop-color="#8f241c"/>
          <stop offset="1" stop-color="#f0603f"/>
        </linearGradient>
        <linearGradient id="g-gold" x1="0" y1="0" x2="1" y2="0">
          <stop offset="0" stop-color="#8a6a2a"/>
          <stop offset="1" stop-color="#e8bf6a"/>
        </linearGradient>
        <linearGradient id="g-law" x1="0" y1="0" x2="1" y2="0">
          <stop offset="0" stop-color="#234f64"/>
          <stop offset="1" stop-color="#7fb8cd"/>
        </linearGradient>
        <linearGradient id="g-muted" x1="0" y1="0" x2="1" y2="0">
          <stop offset="0" stop-color="#3d2f26"/>
          <stop offset="1" stop-color="#7a6851"/>
        </linearGradient>
        <linearGradient id="g-card" x1="0" y1="0" x2="0.4" y2="1">
          <stop offset="0" stop-color="#f0603f"/>
          <stop offset="1" stop-color="#8f241c"/>
        </linearGradient>
        <linearGradient id="g-card-law" x1="0" y1="0" x2="0.4" y2="1">
          <stop offset="0" stop-color="#7fb8cd"/>
          <stop offset="1" stop-color="#234f64"/>
        </linearGradient>
        <linearGradient id="g-area" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stop-color="#db4b35" stop-opacity=".38"/>
          <stop offset="1" stop-color="#db4b35" stop-opacity="0"/>
        </linearGradient>
        <linearGradient id="g-area-law" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stop-color="#68a2b9" stop-opacity=".3"/>
          <stop offset="1" stop-color="#68a2b9" stop-opacity="0"/>
        </linearGradient>
        <linearGradient id="g-area-gold" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stop-color="#c79b46" stop-opacity=".34"/>
          <stop offset="1" stop-color="#c79b46" stop-opacity="0"/>
        </linearGradient>
        <radialGradient id="plate-fill" cx="34%" cy="26%" r="86%">
          <stop offset="0" stop-color="#2b1c16"/>
          <stop offset="1" stop-color="#150e0c"/>
        </radialGradient>
        <radialGradient id="halo" cx="50%" cy="50%" r="50%">
          <stop offset="0" stop-color="#db4b35" stop-opacity=".34"/>
          <stop offset="1" stop-color="#db4b35" stop-opacity="0"/>
        </radialGradient>
        <filter id="soft-glow" x="-60%" y="-60%" width="220%" height="220%">
          <feGaussianBlur stdDeviation="5" result="blur"/>
          <feMerge>
            <feMergeNode in="blur"/>
            <feMergeNode in="SourceGraphic"/>
          </feMerge>
        </filter>
        <marker id="tip" markerWidth="9" markerHeight="9" refX="8" refY="4.5"
          orient="auto">
          <path d="M0 0L9 4.5L0 9Z" fill="#db4b35"/>
        </marker>
        <marker id="tip-muted" markerWidth="9" markerHeight="9" refX="8" refY="4.5"
          orient="auto">
          <path d="M0 0L9 4.5L0 9Z" fill="#6d5a48"/>
        </marker>
      </defs>`;

    const svg = content => `
      <svg viewBox="0 0 800 560" role="presentation" focusable="false">
        ${defs}${content}
      </svg>`;
    const delay = milliseconds => `style="--delay:${milliseconds}ms"`;
    const halo = (x, y, radius) =>
      `<circle cx="${x}" cy="${y}" r="${radius}" fill="url(#halo)" class="fade-in"/>`;
    const ring = (x, y, radius, colour, wait = 0) => `
      <circle cx="${x}" cy="${y}" r="${radius}" fill="none" stroke="${colour}"
        stroke-width="1.4" class="ring"
        style="--delay:${wait}ms;transform-origin:${x}px ${y}px"/>`;
    const box = (x, y, width, height, label, sub, tone = "", wait = 0) => `
      <g class="pop" ${delay(wait)}>
        <rect x="${x}" y="${y}" width="${width}" height="${height}" rx="7"
          class="node ${tone}"/>
        <text x="${x + width / 2}" y="${y + height / 2 + (sub ? -4 : 4)}"
          text-anchor="middle" class="node-label">${label}</text>
        ${sub ? `<text x="${x + width / 2}" y="${y + height / 2 + 16}"
          text-anchor="middle" class="small-label">${sub}</text>` : ""}
      </g>`;

    /* ---------- act plates ---------- */

    function chapterScene(step) {
      const partNumber = String(step.dataset.part || "").padStart(2, "0");
      const title = esc(step.dataset.partTitle || "").toUpperCase();
      return svg(`
        ${halo(400, 280, 240)}
        <g class="orbit">
          <circle cx="400" cy="280" r="198" fill="none" stroke="#2a1d18"/>
          <circle cx="400" cy="82" r="6" class="gold-fill glow"/>
          <circle cx="598" cy="280" r="4" class="muted-fill"/>
          <circle cx="400" cy="478" r="5" class="law-fill"/>
        </g>
        <g class="orbit reverse">
          <circle cx="400" cy="280" r="150" fill="none" stroke="#33231d"/>
          <circle cx="250" cy="280" r="6" class="accent-fill glow"/>
        </g>
        ${ring(400, 280, 118, "#db4b35", 0)}
        ${ring(400, 280, 118, "#c79b46", 900)}
        <circle cx="400" cy="280" r="112" class="plate"/>
        <text x="400" y="268" text-anchor="middle" class="big-number pop"
          style="font-size:74px">${partNumber}</text>
        <text x="400" y="308" text-anchor="middle" class="small-label fade-in"
          ${delay(260)}>${title}</text>
        <text x="400" y="424" text-anchor="middle" class="chart-title fade-in"
          ${delay(420)}>BANG! THE DUEL</text>`);
    }

    /* ---------- act 1 · the game ---------- */

    function card(x, y, side, active, index) {
      const fill = active
        ? `url(#${side === "law" ? "g-card-law" : "g-card"})`
        : "#33231d";
      return `
        <g class="pop" ${delay(index * 80)}>
          <rect x="${x}" y="${y}" width="68" height="98" rx="8" fill="${fill}"
            opacity="${active ? .95 : .8}" stroke="#4a352b"/>
          <circle cx="${x + 34}" cy="${y + 31}" r="13" fill="#150e0c" opacity=".62"/>
          <path d="M${x + 17} ${y + 77} Q${x + 34} ${y + 54} ${x + 51} ${y + 77}"
            fill="#150e0c" opacity=".62"/>
          ${active ? `<circle cx="${x + 56}" cy="${y + 13}" r="4" fill="#f5eaca"/>` : ""}
        </g>`;
    }

    function duelScene() {
      const law = [140, 218, 296, 374].map((x, index) =>
        card(x, 176 + (index % 2) * 20, "law", index < 2, index)).join("");
      const outlaw = [592, 514, 436, 358].map((x, index) =>
        card(x, 300 - (index % 2) * 20, "outlaw", index < 2, index + 4)).join("");
      return svg(`
        ${halo(400, 282, 210)}
        <text x="400" y="58" text-anchor="middle" class="chart-title">ONE ROSTER HAS TO FALL</text>
        <path d="M84 282H716" class="axis-line draw"/>
        ${ring(400, 282, 26, "#db4b35", 0)}
        <circle cx="400" cy="282" r="7" class="accent-fill glow pulse-dot"/>
        <g class="drift">${law}</g>
        <g class="drift-slow">${outlaw}</g>
        <text x="96" y="140" class="node-label">LAW</text>
        <text x="704" y="424" text-anchor="end" class="node-label">OUTLAW</text>
        <text x="96" y="440" class="small-label">2 IN FRONT · 2 IN RESERVE · 40 CARDS</text>
        <text x="704" y="140" text-anchor="end" class="small-label">2 IN FRONT · 2 IN RESERVE · 40 CARDS</text>`);
    }

    function rosterScene() {
      // Illustrative draft: four of each twelve, two of those already in play.
      const drafted = new Set([1, 4, 6, 9]);
      const inPlay = new Set([1, 6]);
      const block = (side, x0, tone) => latest.characters
        .filter(character => character.side === side)
        .slice(0, 12)
        .map((character, index) => {
          const x = x0 + (index % 2) * 152;
          const y = 150 + Math.floor(index / 2) * 40;
          const chosen = drafted.has(index);
          return `
            <g class="pop" ${delay(index * 40)}>
              <rect x="${x}" y="${y}" width="144" height="30" rx="5"
                fill="${chosen ? `url(#${tone})` : "#221610"}"
                opacity="${chosen ? .9 : .75}" stroke="#3a2a22"/>
              <text x="${x + (inPlay.has(index) ? 22 : 12)}" y="${y + 19}"
                class="small-label"
                style="fill:${chosen ? "#f5eaca" : "#6d5a48"}">${esc(character.name)}</text>
              ${inPlay.has(index)
                ? `<circle cx="${x + 12}" cy="${y + 15}" r="4"
                     class="gold-fill glow pulse-dot"/>`
                : ""}
            </g>`;
        }).join("");
      return svg(`
        <text x="400" y="52" text-anchor="middle" class="chart-title">TWELVE A SIDE · FOUR ARE YOURS · TWO ENTER PLAY</text>
        <text x="72" y="126" class="node-label">LAW</text>
        <text x="728" y="126" text-anchor="end" class="node-label">OUTLAW</text>
        ${block("law", 72, "g-law")}
        ${block("outlaw", 432, "g-accent")}
        <line x1="400" y1="140" x2="400" y2="400" class="grid-line"/>
        <g class="enter" ${delay(560)}>
          <circle cx="76" cy="452" r="4" class="gold-fill"/>
          <text x="90" y="456" class="small-label">IN PLAY — ACTIVE AND REARGUARD</text>
          <rect x="316" y="446" width="12" height="12" rx="3" fill="url(#g-law)"/>
          <rect x="330" y="446" width="12" height="12" rx="3" fill="url(#g-accent)"/>
          <text x="352" y="456" class="small-label">DRAFTED, IN RESERVE</text>
          <rect x="530" y="446" width="12" height="12" rx="3" fill="#221610"
            stroke="#3a2a22"/>
          <text x="552" y="456" class="small-label">LEFT IN THE BOX</text>
        </g>
        <text x="72" y="500" class="small-label">BOTH SIDES CHOOSE BLIND AND COMMIT AT THE SAME TIME</text>`);
    }

    function turnScene() {
      const phases = [
        { label: "HAZARDS", note: "dynamite · snake" },
        { label: "DRAW 2", note: "from your own deck" },
        { label: "PLAY", note: "as much as you can" },
        { label: "DISCARD", note: "down to the limit" },
      ];
      const limits = [
        "ONE BANG! CARD PER TURN",
        "ONE VOLUNTARY SWAP",
        "EQUIPMENT — NO LIMIT",
      ];
      return svg(`
        <text x="40" y="72" class="chart-title">ONE TURN, START TO FINISH</text>
        ${phases.map((phase, index) => {
          const x = 40 + index * 186;
          return `
            ${box(x, 118, 156, 74, phase.label, phase.note,
                  index === 2 ? "hot" : "", index * 120)}
            ${index < 3
              ? `<path d="M${x + 158} 155H${x + 184}" class="connector hot flow"
                   marker-end="url(#tip)"/>`
              : ""}`;
        }).join("")}
        <path d="M490 194V246" class="connector dashed"/>
        <g class="enter" ${delay(520)}>
          <text x="412" y="272" class="chart-title">WHAT THE TURN LIMITS</text>
          ${limits.map((limit, index) => `
            <rect x="412" y="${292 + index * 40}" width="13" height="13" rx="3"
              fill="url(#g-accent)"/>
            <text x="436" y="${304 + index * 40}" class="value-label">${limit}</text>`).join("")}
        </g>
        <g class="enter" ${delay(640)}>
          <text x="40" y="272" class="chart-title">THE HAND LIMIT</text>
          <text x="40" y="330" class="big-number" style="font-size:38px">= YOUR LIFE</text>
          <rect x="40" y="348" width="300" height="2" rx="1" fill="url(#g-gold)"
            class="grow-x" ${delay(760)}/>
          <text x="40" y="380" class="small-label">ACTIVE CHARACTER'S REMAINING LIFE · AT 1 LIFE THE LIMIT IS 2</text>
        </g>
        <line x1="40" y1="428" x2="756" y2="428" class="grid-line"/>
        <text x="40" y="464" class="value-label">a hurt character holds fewer cards — damage compounds into fewer options</text>
        <text x="40" y="508" class="small-label">LAW TAKES THE FIRST TURN · LAW OPENS ON 4 CARDS, OUTLAW ON 5</text>`);
    }

    function combatScene() {
      const answers = [
        { label: "MISSED!", note: "play a card" },
        { label: "HAT", note: "play a card" },
        { label: "BARREL", note: "reveal and pray" },
        { label: "RETURN FIRE", note: "send it back" },
      ];
      const centres = answers.map((_, index) => 118 + index * 190);
      return svg(`
        <text x="400" y="44" text-anchor="middle" class="chart-title">A SHOT IS A QUESTION, NOT A RESULT</text>
        ${box(300, 62, 200, 60, "BANG!", "at the character in front", "hot", 0)}
        <path d="M400 122V146" class="connector"/>
        <path d="M${centres[0]} 146H${centres[3]}" class="connector draw"/>
        ${answers.map((answer, index) => {
          const x = 40 + index * 190;
          return `
            <path d="M${centres[index]} 146V170" class="connector"/>
            ${box(x, 170, 156, 66, answer.label, answer.note,
                  index === 3 ? "hot" : "cool", 140 + index * 90)}`;
        }).join("")}
        ${centres.slice(0, 3).map(centre =>
          `<path d="M${centre} 236V266" class="connector"/>`).join("")}
        <path d="M${centres[0]} 266H${centres[2]}" class="connector"/>
        <path d="M215 266V292" class="connector"/>
        <path d="M${centres[3]} 236V266H585V292" class="connector hot flow"
          marker-end="url(#tip)"/>
        ${box(90, 292, 250, 62, "SHOT AVOIDED", "the answer is spent", "", 480)}
        ${box(460, 292, 250, 62, "STRAIGHT BACK", "now the shooter answers", "hot", 540)}
        <path d="M500 92H772V438H734" class="connector dashed"
          marker-end="url(#tip-muted)"/>
        ${ring(690, 438, 48, "#db4b35", 0)}
        <circle cx="690" cy="438" r="40" class="plate" stroke="#db4b35"/>
        <text x="690" y="444" text-anchor="middle" class="node-label">−1 LIFE</text>
        <text x="690" y="504" text-anchor="middle" class="small-label">IF NOTHING ANSWERS</text>
        <text x="40" y="424" class="value-label">every answer is its own decision —</text>
        <text x="40" y="448" class="value-label">and every one of them can be chained</text>
        <text x="40" y="500" class="small-label">REACHING THE BACK ROW NEEDS A WINCHESTER, A SCOPE, OR THE RIGHT CHARACTER</text>`);
    }

    function hiddenScene() {
      const known = Array.from({ length: 6 }, (_, index) => `
        <rect x="${104 + (index % 3) * 62}" y="${150 + Math.floor(index / 3) * 84}"
          width="46" height="66" rx="6"
          fill="url(#${index % 2 ? "g-card-law" : "g-gold"})" opacity=".92"
          class="pop" ${delay(index * 70)}/>`).join("");
      const secret = Array.from({ length: 6 }, (_, index) => `
        <g class="pop" ${delay(index * 70 + 220)}>
          <rect x="${520 + (index % 3) * 62}" y="${150 + Math.floor(index / 3) * 84}"
            width="46" height="66" rx="6" fill="#241812" stroke="#4a352b"/>
          <text x="${543 + (index % 3) * 62}" y="${190 + Math.floor(index / 3) * 84}"
            text-anchor="middle" class="small-label shimmer"
            style="font-size:15px;--delay:${index * 180}ms">?</text>
        </g>`).join("");
      const legalPips = new Set([3, 4, 9, 15]);
      const pips = Array.from({ length: 22 }, (_, index) => `
        <rect x="${168 + index * 21}" y="430" width="13" height="13" rx="3"
          fill="${legalPips.has(index) ? "url(#g-accent)" : "#2c1e18"}"
          class="pop ${legalPips.has(index) ? "glow" : ""}" ${delay(300 + index * 25)}/>`).join("");
      return svg(`
        <text x="400" y="58" text-anchor="middle" class="chart-title">WHAT IT CAN SEE · WHAT IT CANNOT</text>
        ${known}${secret}
        <path d="M400 128V336" class="axis-line draw"/>
        <text x="176" y="360" text-anchor="middle" class="node-label">OPEN</text>
        <text x="604" y="360" text-anchor="middle" class="node-label">DARK</text>
        <text x="176" y="384" text-anchor="middle" class="small-label">OWN HAND · TABLE · REVEALS</text>
        <text x="604" y="384" text-anchor="middle" class="small-label">THEIR HAND · RESERVE · DECK</text>
        ${pips}
        <text x="168" y="472" class="small-label">997 POSSIBLE MOVES</text>
        <text x="632" y="472" text-anchor="end" class="value-label">4 legal right now</text>`);
    }

    function objectiveScene() {
      const dots = Array.from({ length: 11 }, (_, index) => {
        const x = 92 + index * 52;
        return `
          <circle cx="${x}" cy="236" r="6" class="muted-fill pop"
            ${delay(index * 45)}/>
          <text x="${x}" y="272" text-anchor="middle" class="small-label"
            opacity=".7">0</text>`;
      }).join("");
      return svg(`
        <text x="400" y="62" text-anchor="middle" class="chart-title">REWARD ACROSS ONE DUEL</text>
        <path d="M78 236H700" class="axis-line draw"/>
        ${dots}
        ${halo(672, 236, 92)}
        ${ring(672, 236, 34, "#db4b35", 0)}
        ${ring(672, 236, 34, "#db4b35", 1200)}
        <circle cx="672" cy="236" r="26" fill="url(#g-accent)" class="pop glow"
          ${delay(540)}/>
        <text x="672" y="242" text-anchor="middle" class="node-label"
          style="fill:#17100e;font-size:16px">±1</text>
        <text x="672" y="298" text-anchor="middle" class="small-label">GAME OVER</text>
        <path d="M656 336Q400 418 104 340" class="connector hot flow"
          marker-end="url(#tip)"/>
        <text x="400" y="404" text-anchor="middle" class="value-label">every earlier move is judged by this one bit</text>
        <text x="400" y="472" text-anchor="middle" class="small-label">NO POINTS FOR DAMAGE · NO CREDIT FOR STYLE</text>`);
    }

    /* ---------- act 2 · the algorithm ---------- */

    function searchScene() {
      const levels = [
        { y: 108, count: 1, radius: 15, opacity: 1, label: "YOUR MOVE", note: "fully known" },
        { y: 210, count: 3, radius: 12, opacity: .72, label: "THEIR MOVE", note: "hidden hand — you cannot even list it" },
        { y: 312, count: 7, radius: 9, opacity: .42, label: "THE DRAW", note: "chance branches on every reveal" },
        { y: 396, count: 15, radius: 5.5, opacity: .18, label: "…AND 30 MORE TURNS", note: "" },
      ];
      const positionsAt = count => Array.from({ length: count }, (_, index) =>
        count === 1 ? 300 : 120 + index * (360 / (count - 1)));
      const edges = levels.slice(0, -1).map((level, index) => {
        const from = positionsAt(level.count);
        const to = positionsAt(levels[index + 1].count);
        return to.map(x => {
          const parent = from.reduce((best, candidate) =>
            Math.abs(candidate - x) < Math.abs(best - x) ? candidate : best);
          return `
            <path d="M${parent} ${level.y + level.radius}L${x} ${levels[index + 1].y - levels[index + 1].radius}"
              class="connector" opacity="${levels[index + 1].opacity}"/>`;
        }).join("");
      }).join("");
      // The depth fade lives on the circle and the entry animation on a wrapper:
      // a `pop` class on the circle itself would animate its opacity back to 1.
      const nodes = levels.map((level, index) => positionsAt(level.count).map(x => `
        <g class="pop" ${delay(index * 150)}>
          <circle cx="${x}" cy="${level.y}" r="${level.radius}"
            class="${index === 0 ? "accent-fill glow" : "plate"}"
            stroke="${index === 0 ? "none" : "#4a352b"}"
            opacity="${level.opacity}"/>
        </g>`).join("")).join("");
      return svg(`
        <text x="60" y="52" class="chart-title">WHAT A SEARCH TREE WOULD HAVE TO EXPAND</text>
        ${edges}${nodes}
        ${levels.map((level, index) => `
          <g class="enter" ${delay(200 + index * 130)}>
            <text x="524" y="${level.y + 4}" class="value-label">${level.label}</text>
            <text x="524" y="${level.y + 22}" class="small-label">${level.note}</text>
          </g>`).join("")}
        <line x1="60" y1="452" x2="740" y2="452" class="grid-line"/>
        <text x="60" y="488" class="value-label">the branching is survivable — the not-knowing is not</text>
        <text x="60" y="524" class="small-label">SO LEARN A REFLEX INSTEAD: VISIBLE STATE IN, MOVE OUT</text>`);
    }

    function policyScene() {
      const bars = [.42, .27, .16, .09, .06];
      const vector = Array.from({ length: 14 }, (_, index) => `
        <rect x="60" y="${118 + index * 20}" width="${18 + (index * 37) % 62}"
          height="12" rx="3" fill="url(#g-law)" opacity=".8" class="grow-x"
          ${delay(index * 35)}/>`).join("");
      return svg(`
        <text x="60" y="76" class="chart-title">THE POLICY</text>
        ${vector}
        <text x="60" y="428" class="small-label">EVERYTHING IT IS ALLOWED TO SEE</text>
        <path d="M148 268H214" class="connector hot flow" marker-end="url(#tip)"/>
        ${ring(290, 268, 74, "#db4b35", 0)}
        <circle cx="290" cy="268" r="66" class="plate" stroke="#db4b35"
          stroke-width="2"/>
        <text x="290" y="262" text-anchor="middle" class="big-number"
          style="font-size:38px">π</text>
        <text x="290" y="292" text-anchor="middle" class="small-label">276K WEIGHTS</text>
        <path d="M362 268H428" class="connector hot flow" marker-end="url(#tip)"/>
        ${bars.map((value, index) => {
          const y = 150 + index * 52;
          const width = value * 250;
          const sampled = index === 1;
          return `
            <g class="enter" ${delay(300 + index * 90)}>
              <rect x="452" y="${y}" width="250" height="26" rx="6" class="bar-bg"/>
              <rect x="452" y="${y}" width="${width}" height="26" rx="6"
                fill="url(#${sampled ? "g-gold" : "g-accent"})" class="grow-x"
                ${delay(360 + index * 90)}/>
              <text x="714" y="${y + 18}" class="small-label">${pct(value)}</text>
              ${sampled
                ? `<circle cx="440" cy="${y + 13}" r="6"
                     class="gold-fill glow pulse-dot"/>`
                : ""}
            </g>`;
        }).join("")}
        <text x="452" y="130" class="small-label">A PROBABILITY FOR EVERY LEGAL MOVE</text>
        <text x="452" y="440" class="value-label">it samples — it does not always take the top move</text>
        <text x="452" y="470" class="small-label">PREDICTABILITY IS A LOSING STRATEGY AGAINST A HIDDEN HAND</text>
        <text x="452" y="514" class="mono-label">chosen move · log p recorded for later</text>`);
    }

    function gradientScene() {
      const bars = [
        { label: "went better than predicted", value: .82, up: true, note: "played more" },
        { label: "about as predicted", value: .1, up: true, note: "barely moves" },
        { label: "went worse than predicted", value: .64, up: false, note: "played less" },
      ];
      return svg(`
        <text x="400" y="62" text-anchor="middle" class="chart-title">AFTER EVERY GAME</text>
        <line x1="400" y1="140" x2="400" y2="420" class="axis-line"/>
        <text x="400" y="452" text-anchor="middle" class="small-label">EXPECTATION</text>
        ${bars.map((bar, index) => {
          const y = 168 + index * 88;
          const width = bar.value * 236;
          const x = bar.up ? 400 : 400 - width;
          return `
            <g>
              <text x="78" y="${y - 10}" class="small-label">${bar.label.toUpperCase()}</text>
              <rect x="${x}" y="${y}" width="${width}" height="34" rx="6"
                fill="url(#${bar.up ? "g-gold" : "g-accent"})" opacity=".92"
                class="grow-x"
                style="--delay:${index * 150}ms;transform-origin:400px center"/>
              <text x="700" y="${y + 23}" text-anchor="end" class="node-label"
                style="font-size:15px">${bar.note}</text>
            </g>`;
        }).join("")}
        <text x="400" y="502" text-anchor="middle" class="value-label">the question is never "did we win" — it is "was that better than expected"</text>`);
    }

    function clipScene() {
      const x0 = 108;
      const y0 = 116;
      const width = 570;
      const height = 280;
      const rMin = .5;
      const rMax = 1.5;
      const epsilon = .2;
      const x = value => x0 + (value - rMin) / (rMax - rMin) * width;
      const y = value => y0 + height / 2 - value * (height / 2) / 1.6;
      const samples = Array.from({ length: 121 }, (_, index) =>
        rMin + (index / 120) * (rMax - rMin));
      const clip = r => Math.min(Math.max(r, 1 - epsilon), 1 + epsilon);
      const surrogate = (r, advantage) =>
        Math.min(r * advantage, clip(r) * advantage);
      const coordinates = advantage => samples
        .map(r => `${x(r).toFixed(1)},${y(surrogate(r, advantage)).toFixed(1)}`);
      const points = advantage => coordinates(advantage).join(" ");
      return svg(`
        <text x="${x0}" y="62" class="chart-title">HOW FAR ONE UPDATE MAY MOVE A MOVE</text>
        <rect x="${x(1 - epsilon)}" y="${y0 - 10}" width="${x(1 + epsilon) - x(1 - epsilon)}"
          height="${height + 20}" fill="#c79b46" fill-opacity=".08" class="fade-in"/>
        <line x1="${x(1 - epsilon)}" y1="${y0 - 10}" x2="${x(1 - epsilon)}" y2="${y0 + height + 10}"
          class="connector dashed"/>
        <line x1="${x(1 + epsilon)}" y1="${y0 - 10}" x2="${x(1 + epsilon)}" y2="${y0 + height + 10}"
          class="connector dashed"/>
        <line x1="${x0}" y1="${y(0)}" x2="${x0 + width}" y2="${y(0)}" class="axis-line"/>
        <polyline points="${points(1)}" class="line-gold draw"/>
        <polyline points="${points(-1)}" class="line-accent draw" ${delay(240)}/>
        <text x="${x(1.34)}" y="${y(1.2) - 20}" text-anchor="middle" class="small-label"
          style="fill:#c79b46">GOOD MOVE · CAPPED HERE</text>
        <circle cx="${x(1.34)}" cy="${y(1.2)}" r="5" class="gold-fill glow pulse-dot"/>
        <text x="${x(.66)}" y="${y(-1.2) + 28}" text-anchor="middle" class="small-label"
          style="fill:#db4b35">BAD MOVE · CAPPED HERE</text>
        <circle cx="${x(.66)}" cy="${y(-1.2)}" r="5" class="accent-fill glow pulse-dot"/>
        <text x="${x(1)}" y="${y0 + height + 44}" text-anchor="middle"
          class="mono-label">no change</text>
        <text x="${x(1 - epsilon)}" y="${y0 + height + 44}" text-anchor="middle"
          class="small-label">−20%</text>
        <text x="${x(1 + epsilon)}" y="${y0 + height + 44}" text-anchor="middle"
          class="small-label">+20%</text>
        <text x="${x0}" y="512" class="value-label">outside the band the line goes flat — pushing harder buys nothing</text>`);
    }

    function updateScene() {
      const nodes = [
        { angle: -90, label: "PLAY", sub: `${number.format(training.batchGames)} games` },
        { angle: -18, label: "GRADE", sub: "beat expectation?" },
        { angle: 54, label: "NUDGE", sub: "small steps" },
        { angle: 126, label: "CHECK", sub: "did we move too far?" },
        { angle: 198, label: "FORGET", sub: "drop the batch" },
      ];
      const cx = 400;
      const cy = 268;
      const radius = 158;
      const point = angle => [
        cx + radius * Math.cos(angle * Math.PI / 180),
        cy + radius * Math.sin(angle * Math.PI / 180),
      ];
      const arcs = nodes.map((node, index) => {
        const next = nodes[(index + 1) % nodes.length];
        const sweep = ((next.angle - node.angle) + 360) % 360;
        const from = point(node.angle + 20);
        const to = point(node.angle + sweep - 22);
        return `
          <path d="M${from[0].toFixed(1)} ${from[1].toFixed(1)}
            A${radius} ${radius} 0 0 1 ${to[0].toFixed(1)} ${to[1].toFixed(1)}"
            class="connector hot flow" marker-end="url(#tip)" opacity=".85"/>`;
      }).join("");
      return svg(`
        ${halo(cx, cy, 210)}
        <circle cx="${cx}" cy="${cy}" r="${radius}" fill="none" stroke="#33231d"/>
        ${arcs}
        ${nodes.map((node, index) => {
          const [nx, ny] = point(node.angle);
          return `
            <g class="pop" ${delay(index * 110)}>
              <circle cx="${nx}" cy="${ny}" r="52" class="plate" stroke="#4a352b"/>
              <text x="${nx}" y="${ny - 2}" text-anchor="middle" class="node-label">${node.label}</text>
              <text x="${nx}" y="${ny + 18}" text-anchor="middle" class="small-label">${node.sub}</text>
            </g>`;
        }).join("")}
        ${ring(cx, cy, 62, "#db4b35", 0)}
        <circle cx="${cx}" cy="${cy}" r="56" class="plate" stroke="#db4b35"/>
        <text x="${cx}" y="${cy - 4}" text-anchor="middle" class="big-number"
          style="font-size:26px" data-count-to="${training.iterations}"
          data-count-format="int">0</text>
        <text x="${cx}" y="${cy + 20}" text-anchor="middle" class="small-label">TIMES ROUND</text>
        <text x="400" y="500" text-anchor="middle" class="value-label">every loop is one small, careful step — never a leap</text>`);
    }

    /* ---------- act 2 · the league ---------- */

    function nontransitivityScene() {
      const nodes = [
        { x: 400, y: 150, label: "A" },
        { x: 596, y: 388, label: "B" },
        { x: 204, y: 388, label: "C" },
      ];
      return svg(`
        ${halo(400, 300, 220)}
        <text x="400" y="60" text-anchor="middle" class="chart-title">STRENGTH IS A CIRCLE, NOT A LADDER</text>
        <path d="M452 206Q566 254 582 330" class="connector hot flow" marker-end="url(#tip)"/>
        <path d="M540 428Q400 470 260 428" class="connector hot flow" marker-end="url(#tip)"/>
        <path d="M218 330Q234 254 348 206" class="connector hot flow" marker-end="url(#tip)"/>
        ${nodes.map((node, index) => `
          <g class="pop" ${delay(index * 130)}>
            ${ring(node.x, node.y, 56, "#db4b35", index * 400)}
            <circle cx="${node.x}" cy="${node.y}" r="52" class="plate" stroke="#db4b35"/>
            <text x="${node.x}" y="${node.y + 8}" text-anchor="middle"
              class="big-number" style="font-size:34px">${node.label}</text>
          </g>`).join("")}
        <text x="522" y="266" class="small-label">BEATS</text>
        <text x="382" y="466" class="small-label">BEATS</text>
        <text x="216" y="266" class="small-label">BEATS</text>
        <text x="400" y="524" text-anchor="middle" class="value-label">win every game against yourself, improve nothing</text>`);
    }

    function leagueScene() {
      const opponents = [
        { label: "FROZEN COPY OF ITSELF", value: training.opponents.frozenCurrent, fill: "url(#g-accent)" },
        { label: "OLD CHECKPOINTS", value: training.opponents.pfspHistory, fill: "url(#g-gold)" },
        { label: "SCRIPTED BOT", value: training.opponents.heuristic, fill: "url(#g-law)" },
        { label: "RANDOM PLAYER", value: training.opponents.random, fill: "url(#g-muted)" },
      ];
      const bars = opponents.map((opponent, index) => {
        const y = 358 + index * 38;
        return `
          <text x="96" y="${y + 12}" class="small-label">${opponent.label}</text>
          <rect x="300" y="${y}" width="314" height="15" rx="7.5" class="bar-bg"/>
          <rect x="300" y="${y}" width="${Math.max(314 * opponent.value, 6)}" height="15"
            rx="7.5" fill="${opponent.fill}" class="grow-x" ${delay(index * 110)}/>
          <text x="630" y="${y + 12}" class="value-label">${pct(opponent.value)}</text>`;
      }).join("");
      return svg(`
        ${halo(400, 178, 190)}
        <g class="orbit" style="transform-origin:400px 178px">
          <circle cx="400" cy="178" r="126" fill="none" stroke="#2a1d18"/>
          <circle cx="400" cy="52" r="10" fill="url(#g-gold)" class="glow"/>
          <circle cx="526" cy="178" r="7" class="muted-fill"/>
          <circle cx="400" cy="304" r="7" class="law-fill"/>
          <circle cx="274" cy="178" r="9" class="accent-fill glow"/>
        </g>
        <g class="orbit reverse" style="transform-origin:400px 178px">
          <circle cx="400" cy="178" r="84" fill="none" stroke="#33231d"/>
          <circle cx="484" cy="178" r="6" class="gold-fill"/>
          <circle cx="316" cy="178" r="6" class="muted-fill"/>
        </g>
        ${ring(400, 178, 54, "#db4b35", 0)}
        <circle cx="400" cy="178" r="52" class="plate" stroke="#db4b35"/>
        <text x="400" y="176" text-anchor="middle" class="node-label">LEARNER</text>
        <text x="400" y="196" text-anchor="middle" class="small-label">TODAY</text>
        <text x="96" y="336" class="chart-title">WHO IT FACES, PER GAME</text>
        ${bars}`);
    }

    function pfspScene() {
      const x0 = 108;
      const y0 = 130;
      const width = 566;
      const height = 220;
      const x = value => x0 + value * width;
      const weight = value => Math.max(.05, 1 - Math.abs(value - .5) / .5);
      const y = value => y0 + height - value * height;
      const curvePoints = Array.from({ length: 101 }, (_, index) => {
        const p = index / 100;
        return `${x(p).toFixed(1)},${y(weight(p)).toFixed(1)}`;
      });
      const curve = curvePoints.join(" ");
      const dots = [.1, .24, .38, .48, .55, .66, .8, .93].map((p, index) => `
        <circle cx="${x(p)}" cy="${y(weight(p))}" r="${4 + weight(p) * 7}"
          fill="${weight(p) > .8 ? "url(#g-gold)" : "#6d5a48"}"
          class="pop ${weight(p) > .8 ? "glow" : ""}" ${delay(420 + index * 70)}/>`).join("");
      return svg(`
        <text x="${x0}" y="76" class="chart-title">HOW OFTEN AN OLD CHECKPOINT GETS PICKED</text>
        <path d="M${x0},${y0 + height} L${curvePoints.join(" L")} L${x0 + width},${y0 + height} Z"
          fill="url(#g-area-gold)" class="fade-in" ${delay(260)}/>
        <line x1="${x0}" y1="${y0 + height}" x2="${x0 + width}" y2="${y0 + height}"
          class="axis-line"/>
        <line x1="${x(.5)}" y1="${y0 - 16}" x2="${x(.5)}" y2="${y0 + height}"
          class="grid-line"/>
        <polyline points="${curve}" class="line-gold draw"/>
        ${dots}
        ${ring(x(.5), y(1), 26, "#c79b46", 0)}
        <text x="${x0}" y="${y0 + height + 30}" class="small-label">IT LOSES EVERY TIME</text>
        <text x="${x(.5)}" y="${y0 + height + 30}" text-anchor="middle"
          class="mono-label">even fight</text>
        <text x="${x0 + width}" y="${y0 + height + 30}" text-anchor="end"
          class="small-label">IT WINS EVERY TIME</text>
        <text x="${x0}" y="452" class="value-label">the closer the matchup, the more it gets played</text>
        <text x="${x0}" y="492" class="small-label">NOBODY DROPS OUT ENTIRELY — OLD TRICKS STAY IN ROTATION</text>`);
    }

    /* ---------- act 3 · the representation ---------- */

    function observationScene() {
      const segments = [
        { size: 859, label: "THE VISIBLE BOARD", note: "phase · hp · gear · discard", fill: "url(#g-law)" },
        { size: 2, label: "WHOSE TURN", note: "me or them", fill: "#8a6f52" },
        { size: 160, label: "MY HAND · DISCARD", note: "2 × 80 cards", fill: "url(#g-gold)" },
        { size: 160, label: "CARDS I HAVE SEEN", note: "2 × 80 cards", fill: "url(#g-muted)" },
        { size: 48, label: "CHARACTERS SEEN", note: "2 × 24", fill: "url(#g-accent)" },
      ];
      const total = segments.reduce((sum, segment) => sum + segment.size, 0);
      const width = 620;
      let cursor = 90;
      const bar = segments.map((segment, index) => {
        const segmentWidth = segment.size / total * width;
        const x = cursor;
        cursor += segmentWidth;
        return `
          <rect x="${x}" y="140" width="${Math.max(segmentWidth - 2, 2)}" height="86"
            rx="4" fill="${segment.fill}" class="grow-x" ${delay(index * 110)}/>`;
      }).join("");
      const legend = segments.map((segment, index) => `
        <g class="enter" ${delay(320 + index * 70)}>
          <rect x="90" y="${272 + index * 46}" width="14" height="14" rx="3"
            fill="${segment.fill}"/>
          <text x="116" y="${284 + index * 46}" class="value-label">${segment.label}</text>
          <text x="470" y="${284 + index * 46}" class="mono-label">${segment.size}</text>
          <text x="530" y="${284 + index * 46}" class="small-label">${segment.note}</text>
        </g>`).join("");
      return svg(`
        <text x="90" y="80" class="chart-title">THE WHOLE SITUATION, FLATTENED</text>
        <text x="90" y="118" class="big-number" style="font-size:34px"
          data-count-to="${training.observationSize}" data-count-format="int">0</text>
        <text x="${90 + 116}" y="118" class="value-label">numbers</text>
        ${bar}${legend}
        <text x="90" y="522" class="small-label">ALWAYS WRITTEN FROM THE ACTING SEAT — "ME" VERSUS "THEM"</text>`);
    }

    function actionScene() {
      const fields = [
        { size: 15, label: "what kind of move" },
        { size: 31, label: "which card" },
        { size: 4, label: "reveal slot" },
        { size: 2, label: "whose card" },
        { size: 3, label: "card type" },
        { size: 4, label: "properties" },
        { size: 4, label: "at whom" },
        { size: 24, label: "which character" },
        { size: 4, label: "option" },
      ];
      const total = fields.reduce((sum, field) => sum + field.size, 0);
      const width = 470;
      let cursor = 236;
      const strip = fields.map((field, index) => {
        const fieldWidth = field.size / total * width;
        const x = cursor;
        cursor += fieldWidth;
        const hot = index === 1 || index === 6;
        return `
          <rect x="${x}" y="176" width="${Math.max(fieldWidth - 3, 3)}" height="64"
            rx="4" fill="${hot ? "url(#g-accent)" : "#33231d"}" stroke="#4a352b"
            class="pop" ${delay(index * 60)}/>`;
      }).join("");
      const rows = fields.map((field, index) => `
        <text x="${index < 5 ? 236 : 500}" y="${300 + (index % 5) * 32}"
          class="small-label enter" ${delay(360 + index * 45)}>${field.size} · ${field.label}</text>`).join("");
      return svg(`
        <text x="90" y="76" class="chart-title">ONE MOVE, DESCRIBED</text>
        <g class="pop">
          <rect x="90" y="150" width="82" height="116" rx="9" fill="url(#g-card)"
            stroke="#4a352b"/>
          <circle cx="131" cy="186" r="14" fill="#150e0c" opacity=".6"/>
          <path d="M110 240Q131 212 152 240" fill="#150e0c" opacity=".6"/>
        </g>
        <path d="M180 208H226" class="connector hot" marker-end="url(#tip)"/>
        ${strip}${rows}
        <text x="236" y="152" class="value-label">
          <tspan data-count-to="${training.actionFeatureSize}" data-count-format="int">0</tspan>
          features</text>
        <line x1="90" y1="470" x2="710" y2="470" class="grid-line"/>
        <text x="90" y="502" class="small-label">EVERY COPY OF THE SAME CARD PRODUCES THE SAME NUMBERS</text>`);
    }

    function scoringScene() {
      const rows = [
        { label: "Missed! → block the shot", logit: 2.4 },
        { label: "Colt → front gunslinger", logit: 1.5 },
        { label: "Barrel → equip", logit: .6 },
        { label: "Discard water", logit: -.7 },
        { label: "End turn", logit: -1.4 },
      ];
      const maximum = Math.max(...rows.map(row => row.logit));
      const weights = rows.map(row => Math.exp(row.logit - maximum));
      const total = weights.reduce((sum, value) => sum + value, 0);
      return svg(`
        <text x="80" y="70" class="chart-title">SCORE EVERY LEGAL MOVE, THEN CHOOSE</text>
        ${rows.map((row, index) => {
          const y = 128 + index * 74;
          const probability = weights[index] / total;
          const best = index === 0;
          return `
            <g class="enter" ${delay(index * 90)}>
              <text x="80" y="${y + 14}" class="value-label">${row.label}</text>
              <text x="382" y="${y + 14}" text-anchor="end"
                class="mono-label">${row.logit > 0 ? "+" : ""}${row.logit.toFixed(1)}</text>
              <path d="M398 ${y + 9}H436" class="connector" marker-end="url(#tip-muted)"/>
              <rect x="458" y="${y - 2}" width="248" height="24" rx="6" class="bar-bg"/>
              <rect x="458" y="${y - 2}" width="${Math.max(probability * 248, 3)}" height="24"
                rx="6" fill="url(#g-accent)" class="grow-x" ${delay(320 + index * 90)}/>
              <text x="${470 + probability * 248}" y="${y + 15}"
                class="small-label">${pct(probability)}</text>
              ${best ? `<circle cx="446" cy="${y + 9}" r="5" class="accent-fill glow pulse-dot"/>` : ""}
            </g>`;
        }).join("")}
        <text x="80" y="516" class="small-label">ILLEGAL MOVES ARE NEVER IN THE RACE — THEY CANNOT GET ANY PROBABILITY</text>`);
    }

    /* ---------- act 3 · the network ---------- */

    function networkScene() {
      return svg(`
        ${halo(400, 300, 250)}
        <text x="400" y="48" text-anchor="middle" class="chart-title">THE WHOLE PLAYER</text>
        ${box(78, 78, 220, 66, "BOARD ENCODER", "859 → 128", "", 0)}
        ${box(502, 78, 220, 66, "CARD ENCODER", "368 → 64", "", 90)}
        <path d="M188 144V186H400M612 144V186H400" class="connector draw"/>
        ${box(288, 186, 224, 66, "FUSION", "192 → 128", "hot", 200)}
        <path d="M400 252V288" class="connector hot flow"/>
        ${box(288, 288, 224, 66, "RESIDUAL CORE ×2", "the thinking part", "hot", 300)}
        <path d="M400 354V386H272M400 386H528" class="connector draw"/>
        ${box(168, 396, 200, 62, "SITUATION", "128 → 64", "", 400)}
        ${box(432, 396, 200, 62, "THE MOVE", "91 → 64", "", 470)}
        <path d="M272 458V492H400M528 458V492H400" class="connector"/>
        ${ring(400, 506, 30, "#db4b35", 0)}
        <circle cx="400" cy="506" r="24" fill="url(#g-accent)" class="pop glow"
          ${delay(560)}/>
        <text x="400" y="511" text-anchor="middle" class="small-label"
          style="fill:#17100e">SCORE</text>`);
    }

    function interactionScene() {
      const parts = [
        { label: "s", note: "the situation", hot: false },
        { label: "a", note: "the move", hot: false },
        { label: "s × a", note: "does it fit here?", hot: true },
        { label: "s − a", note: "how far off?", hot: true },
      ];
      return svg(`
        <text x="400" y="62" text-anchor="middle" class="chart-title">WHERE THE MOVE MEETS THE MOMENT</text>
        ${parts.map((part, index) => {
          const x = 78 + index * 168;
          return `
            <g class="pop" ${delay(index * 110)}>
              <rect x="${x}" y="118" width="148" height="104" rx="8"
                class="node ${part.hot ? "hot" : "cool"}"/>
              <text x="${x + 74}" y="166" text-anchor="middle" class="node-label"
                style="font-size:19px">${part.label}</text>
              <text x="${x + 74}" y="192" text-anchor="middle"
                class="small-label">${part.note}</text>
            </g>
            <path d="M${x + 74} 222V264" class="connector ${part.hot ? "hot" : ""}"/>`;
        }).join("")}
        <rect x="78" y="264" width="644" height="48" rx="8" fill="url(#g-accent)"
          opacity=".2" stroke="#db4b35" class="grow-x" ${delay(440)}/>
        <text x="400" y="294" text-anchor="middle" class="node-label">ALL FOUR, TOGETHER</text>
        <path d="M400 312V360" class="connector hot flow" marker-end="url(#tip)"/>
        ${ring(400, 396, 40, "#c79b46", 200)}
        <circle cx="400" cy="396" r="34" class="plate" stroke="#c79b46"/>
        <text x="400" y="401" text-anchor="middle" class="value-label">score</text>
        <text x="400" y="478" text-anchor="middle" class="value-label">multiplying is what makes a card good <tspan style="font-style:italic">here</tspan></text>
        <text x="400" y="510" text-anchor="middle" class="small-label">NOT "GOOD CARD" PLUS "GOOD BOARD" — GOOD CARD, THIS BOARD</text>`);
    }

    function criticScene() {
      return svg(`
        <text x="400" y="56" text-anchor="middle" class="chart-title">THE COACH IS ALLOWED TO CHEAT</text>
        ${box(88, 112, 214, 92, "THE PLAYER", "sees what you see", "hot", 0)}
        ${box(498, 112, 214, 92, "THE COACH", "predicts the ending", "", 110)}
        <path d="M302 158H498" class="connector draw"/>
        <text x="400" y="146" text-anchor="middle" class="small-label">SHARED BRAIN</text>
        <g class="pop" ${delay(240)}>
          <rect x="498" y="300" width="214" height="104" rx="7" class="node"/>
          <text x="605" y="330" text-anchor="middle" class="node-label">SECRET PEEK</text>
          <text x="605" y="364" text-anchor="middle" class="big-number"
            style="font-size:24px" data-count-to="${training.privilegedSize}"
            data-count-format="int">0</text>
          <text x="605" y="386" text-anchor="middle" class="small-label">HIDDEN NUMBERS · BOTH HANDS, BOTH DECKS</text>
        </g>
        <path d="M605 204V300" class="connector dashed flow"/>
        ${ring(190, 352, 62, "#db4b35", 0)}
        <rect x="112" y="322" width="156" height="60" rx="30" class="plate"
          stroke="#db4b35"/>
        <text x="190" y="348" text-anchor="middle" class="small-label">SHIPPED TO YOU</text>
        <text x="190" y="368" text-anchor="middle" class="node-label">PLAYER ONLY</text>
        <path d="M190 204V322" class="connector hot draw"/>
        <path d="M336 352H462" class="connector"/>
        <path d="M366 324L438 380M438 324L366 380" stroke="#db4b35" stroke-width="3"
          class="pop" ${delay(400)}/>
        <text x="400" y="430" text-anchor="middle" class="small-label">THE SECRET PATH IS CUT BEFORE EXPORT</text>
        <text x="400" y="486" text-anchor="middle" class="value-label">the coach knows everything · the gunslinger walks out blind</text>`);
    }

    /* ---------- act 4 · the run ---------- */

    function scaleScene() {
      const stats = [
        { value: training.environmentSteps, format: "compact", label: "game steps" },
        { value: training.games, format: "int", label: "duels played" },
        { value: training.learnerDecisions, format: "compact", label: "decisions made" },
        { value: training.elapsedSeconds / 3600, format: "hours", label: "hours, one machine" },
      ];
      const rollout = training.rolloutSeconds || 0;
      const learn = training.updateSeconds || 0;
      const total = rollout + learn;
      const width = 620;
      const rolloutWidth = total ? rollout / total * width : 0;
      return svg(`
        ${stats.map((stat, index) => {
          const x = index % 2 ? 430 : 90;
          const y = 148 + (index < 2 ? 0 : 168);
          return `
            <g class="enter" ${delay(index * 130)}>
              <text x="${x}" y="${y}" class="big-number" data-count-to="${stat.value}"
                data-count-format="${stat.format}">0</text>
              <rect x="${x}" y="${y + 20}" width="262" height="2" rx="1"
                fill="url(#g-accent)" class="grow-x" ${delay(index * 130 + 200)}/>
              <text x="${x}" y="${y + 50}" class="chart-title">${stat.label.toUpperCase()}</text>
            </g>`;
        }).join("")}
        <text x="90" y="452" class="chart-title">WHERE THE WALL CLOCK WENT</text>
        <rect x="90" y="468" width="${width}" height="28" rx="7" class="bar-bg"/>
        <rect x="90" y="468" width="${rolloutWidth}" height="28" rx="7"
          fill="url(#g-law)" class="grow-x" ${delay(560)}/>
        <rect x="${90 + rolloutWidth}" y="468" width="${width - rolloutWidth}"
          height="28" rx="7" fill="url(#g-accent)" class="grow-x" ${delay(680)}/>
        <text x="90" y="524" class="small-label">${(rollout / 3600).toFixed(0)} H PLAYING THE GAMES</text>
        <text x="710" y="524" text-anchor="end" class="small-label">${(learn / 3600).toFixed(0)} H LEARNING FROM THEM</text>`);
    }

    function lineChart(series, options) {
      const x0 = 100;
      const y0 = 84;
      const width = 596;
      const height = 348;
      const xValue = options.x || (point => point.iteration);
      const xs = series.flatMap(item => item.points.map(xValue));
      const xMin = Math.min(...xs);
      const xMax = Math.max(...xs);
      const x = value => x0 + ((value - xMin) / Math.max(1, xMax - xMin)) * width;
      const y = value =>
        y0 + height - ((value - options.min) / (options.max - options.min)) * height;
      const grid = [0, .25, .5, .75, 1].map(fraction => {
        const value = options.min + fraction * (options.max - options.min);
        const py = y(value);
        return `
          <line x1="${x0}" y1="${py}" x2="${x0 + width}" y2="${py}" class="grid-line"/>
          <text x="${x0 - 13}" y="${py + 4}" text-anchor="end"
            class="small-label">${options.formatY(value)}</text>`;
      }).join("");
      const drawn = series.map((item, index) => {
        const usable = item.points.filter(point => Number.isFinite(item.value(point)));
        const coordinates = usable
          .map(point => `${x(xValue(point)).toFixed(1)},${y(item.value(point)).toFixed(1)}`);
        const last = usable.at(-1);
        const area = item.area
          ? `<path d="M${coordinates[0]} L${coordinates.join(" L")} L${x(xValue(last)).toFixed(1)},${y0 + height} L${x(xValue(usable[0])).toFixed(1)},${y0 + height} Z"
               fill="url(#${item.area})" class="fade-in" ${delay(500 + index * 120)}/>`
          : "";
        return `
          ${area}
          <polyline points="${coordinates.join(" ")}" class="${item.className} draw"
            ${delay(index * 180)}/>
          ${ring(x(xValue(last)), y(item.value(last)), 16, item.color, 900 + index * 200)}
          <circle cx="${x(xValue(last))}" cy="${y(item.value(last))}" r="5.5"
            fill="${item.color}" class="pop glow" ${delay(950 + index * 120)}/>`;
      }).join("");
      const legend = series.map((item, index) => `
        <circle cx="${x0 + index * 250}" cy="512" r="5" fill="${item.color}"/>
        <text x="${x0 + 14 + index * 250}" y="516" class="small-label">${item.label}</text>`).join("");
      return svg(`
        <text x="${x0}" y="48" class="chart-title">${options.title}</text>
        ${grid}
        <line x1="${x0}" y1="${y0 + height}" x2="${x0 + width}" y2="${y0 + height}"
          class="axis-line"/>
        <text x="${x0}" y="${y0 + height + 28}" class="small-label">${options.xStart}</text>
        <text x="${x0 + width}" y="${y0 + height + 28}" text-anchor="end"
          class="small-label">${options.xEnd}</text>
        ${drawn}${legend}`);
    }

    function trainingScene() {
      const points = training.points.filter(point => Number.isFinite(point.winRate));
      return lineChart([{
        points,
        value: point => point.winRate,
        className: "line-accent",
        color: "#db4b35",
        area: "g-area",
        label: "WIN RATE INSIDE THE LEAGUE",
      }], {
        title: "IT KEEPS WINNING ABOUT 55% — FOREVER",
        min: .42,
        max: .72,
        formatY: pct,
        x: point => point.environmentSteps,
        xStart: "FIRST GAMES",
        xEnd: `${compact.format(training.environmentSteps)} STEPS`,
      });
    }

    function benchmarkScene() {
      const results = training.opponentResults || {};
      const rows = [
        { key: "random", label: "RANDOM LEGAL PLAYER", note: "cannot improve", fill: "url(#g-muted)" },
        { key: "heuristic", label: "SCRIPTED TACTICAL BOT", note: "cannot improve", fill: "url(#g-law)" },
        { key: "pfspHistory", label: "ITS OWN PAST SELVES", note: "improve alongside it", fill: "url(#g-gold)" },
        { key: "frozenCurrent", label: "A FROZEN COPY OF ITSELF", note: "improves with it exactly", fill: "url(#g-accent)" },
      ].filter(row => results[row.key]);
      const x0 = 250;
      const width = 420;
      return svg(`
        <text x="60" y="56" class="chart-title">WIN RATE AGAINST EACH KIND OF OPPONENT</text>
        <line x1="${x0 + width / 2}" y1="88" x2="${x0 + width / 2}" y2="428"
          class="grid-line"/>
        <text x="${x0 + width / 2}" y="80" text-anchor="middle" class="small-label">EVEN</text>
        ${rows.map((row, index) => {
          const y = 116 + index * 84;
          const result = results[row.key];
          return `
            <g class="enter" ${delay(index * 110)}>
              <text x="60" y="${y + 6}" class="value-label">${row.label}</text>
              <text x="60" y="${y + 26}" class="small-label">${row.note} · ${number.format(result.games)} games</text>
              <rect x="${x0}" y="${y + 34}" width="${width}" height="22" rx="11"
                class="bar-bg"/>
              <rect x="${x0}" y="${y + 34}" width="${Math.max(result.winRate * width, 6)}"
                height="22" rx="11" fill="${row.fill}" class="grow-x"
                ${delay(index * 110 + 200)}/>
              <text x="${x0 - 12}" y="${y + 50}" text-anchor="end"
                class="mono-label">${pct(result.winRate)}</text>
            </g>`;
        }).join("")}
        <text x="60" y="474" class="value-label">near-perfect against anything that stands still</text>
        <text x="60" y="510" class="small-label">POOLED OVER THE LAST ${number.format(style.windowIterations)} UPDATES OF THE RUN</text>`);
    }

    function confidenceScene() {
      const points = training.points.filter(point =>
        Number.isFinite(point.confidence) && Number.isFinite(point.entropy));
      return lineChart([
        {
          points,
          value: point => point.confidence,
          className: "line-accent",
          color: "#db4b35",
          area: "g-area",
          label: "CONFIDENCE IN ITS FAVOURITE MOVE",
        },
        {
          points,
          value: point => point.entropy,
          className: "line-law",
          color: "#68a2b9",
          label: "HOW MUCH IT HEDGES",
        },
      ], {
        title: "HESITATION TURNS INTO OPINION",
        min: .25,
        max: .9,
        formatY: pct,
        x: point => point.environmentSteps,
        xStart: "EARLY",
        xEnd: "FINAL CHECKPOINT",
      });
    }

    function deployScene() {
      return svg(`
        <text x="400" y="54" text-anchor="middle" class="chart-title">FROM TRAINING RIG TO YOUR TAB</text>
        ${box(84, 104, 194, 80, "CHECKPOINT", `${number.format(training.parameterCount)} weights`, "", 0)}
        <path d="M278 144H344" class="connector flow" marker-end="url(#tip-muted)"/>
        ${box(344, 104, 194, 80, "CUT THE COACH", "player only", "hot", 130)}
        <path d="M538 144H604" class="connector hot flow" marker-end="url(#tip)"/>
        ${box(604, 104, 122, 80, "JSON", "small file", "", 240)}
        <path d="M665 184V244" class="connector flow" marker-end="url(#tip-muted)"/>
        ${box(344, 244, 382, 80, "RULES ENGINE → WEBASSEMBLY", "the same C++ it trained on", "hot", 330)}
        <path d="M344 284H240" class="connector hot flow" marker-end="url(#tip)"/>
        ${ring(160, 284, 66, "#db4b35", 0)}
        ${box(84, 244, 152, 80, "YOUR TAB", "one pass per move", "", 420)}
        <text x="400" y="404" text-anchor="middle" class="value-label">no server · no telemetry · nothing to phone home to</text>
        <text x="400" y="452" text-anchor="middle" class="small-label">WHEN IT SHOOTS YOU, THAT DECISION HAPPENED ON YOUR MACHINE</text>`);
    }

    /* ---------- act 5 · the meta ---------- */

    function sidesScene() {
      const lawWidth = latest.lawWinRate * 604;
      const outlawWidth = latest.outlawWinRate * 604;
      const timeline = evaluation.points;
      const x = point => 96 + (point.iteration - timeline[0].iteration) /
        Math.max(1, timeline.at(-1).iteration - timeline[0].iteration) * 604;
      const y = value => 470 - (value - .3) / .3 * 88;
      const lawPoints = timeline.map(point => `${x(point)},${y(point.lawWinRate)}`).join(" ");
      return svg(`
        <text x="96" y="58" class="chart-title">SAME WEIGHTS ON BOTH SIDES · ${number.format(latest.games)} GAMES</text>
        <text x="96" y="122" class="node-label">LAW</text>
        <rect x="96" y="142" width="604" height="56" rx="7" class="bar-bg"/>
        <rect x="96" y="142" width="${lawWidth}" height="56" rx="7"
          fill="url(#g-law)" class="grow-x"/>
        <text x="${112 + lawWidth}" y="177" class="value-label"
          style="font-size:15px">${pct(latest.lawWinRate)}</text>
        <text x="96" y="248" class="node-label">OUTLAW</text>
        <rect x="96" y="268" width="604" height="56" rx="7" class="bar-bg"/>
        <rect x="96" y="268" width="${outlawWidth}" height="56" rx="7"
          fill="url(#g-accent)" class="grow-x" ${delay(160)}/>
        <text x="${112 + outlawWidth}" y="303" class="value-label"
          style="font-size:15px">${pct(latest.outlawWinRate)}</text>
        <text x="96" y="382" class="chart-title">AND IT NEVER FLIPS · ${timeline.length} CHECKPOINTS</text>
        <line x1="96" y1="${y(.5)}" x2="700" y2="${y(.5)}" class="grid-line"/>
        <polyline points="${lawPoints}" class="line-law draw" ${delay(360)}/>
        <text x="706" y="${y(.5) + 4}" class="small-label">50%</text>
        <text x="96" y="516" class="small-label">LAW WIN RATE, ITERATION ${timeline[0].iteration} → ${timeline.at(-1).iteration}</text>`);
    }

    function charactersScene() {
      const sorted = [...latest.characters].sort((a, b) => b.uplift - a.uplift);
      const rows = [...sorted.slice(0, 4), ...sorted.slice(-4)];
      const scale = Math.max(...rows.map(row => Math.abs(row.uplift))) || .1;
      return svg(`
        <text x="110" y="54" class="chart-title">BETTER OR WORSE THAN AN AVERAGE PICK ON THAT SIDE</text>
        <line x1="424" y1="78" x2="424" y2="486" class="axis-line"/>
        ${rows.map((character, index) => {
          const y = 94 + index * 50;
          const width = Math.abs(character.uplift) / scale * 224;
          const positive = character.uplift >= 0;
          const x = positive ? 424 : 424 - width;
          const fill = character.side === "law" ? "url(#g-law)" : "url(#g-accent)";
          return `
            <text x="110" y="${y + 18}" class="value-label">${esc(character.name)}</text>
            <rect x="${x}" y="${y}" width="${width}" height="26" rx="5" fill="${fill}"
              class="grow-x"
              style="--delay:${index * 60}ms;transform-origin:424px center"/>
            <text x="${positive ? x + width + 10 : x - 10}" y="${y + 18}"
              text-anchor="${positive ? "start" : "end"}"
              class="small-label">${character.uplift > 0 ? "+" : ""}${pct(character.uplift)}</text>`;
        }).join("")}
        <text x="414" y="514" text-anchor="end" class="small-label">DRAGS ITS SIDE DOWN</text>
        <text x="434" y="514" class="small-label">CARRIES IT</text>`);
    }

    function pairsScene() {
      const pairs = latest.pairs || [];
      const axis = 470;
      const half = 190;
      const scale = Math.max(
        ...pairs.map(pair => Math.max(Math.abs(pair.uplift), Math.abs(pair.parts))),
        .05);
      return svg(`
        <text x="56" y="52" class="chart-title">EVERY DUO AGAINST THE SUM OF ITS TWO HALVES</text>
        <line x1="${axis}" y1="76" x2="${axis}" y2="428" class="axis-line"/>
        ${pairs.map((pair, index) => {
          const y = 96 + index * 56;
          const width = Math.abs(pair.uplift) / scale * half;
          const positive = pair.uplift >= 0;
          const x = positive ? axis : axis - width;
          const parts = axis + pair.parts / scale * half;
          const fill = pair.side === "law" ? "url(#g-law)" : "url(#g-accent)";
          return `
            <g class="enter" ${delay(index * 80)}>
              <text x="56" y="${y + 19}" class="value-label"
                style="font-size:11.5px">${esc(pair.first)} + ${esc(pair.second)}</text>
              <rect x="${x}" y="${y}" width="${Math.max(width, 2)}" height="26" rx="5"
                fill="${fill}" class="grow-x"
                style="--delay:${index * 80}ms;transform-origin:${axis}px center"/>
              <line x1="${parts}" y1="${y - 5}" x2="${parts}" y2="${y + 31}"
                stroke="#e8bf6a" stroke-width="2" stroke-dasharray="3 3"/>
              <text x="${positive ? x + width + 12 : x - 12}" y="${y + 19}"
                text-anchor="${positive ? "start" : "end"}"
                class="small-label">${pair.uplift > 0 ? "+" : ""}${pct(pair.uplift)}</text>
            </g>`;
        }).join("")}
        <g class="enter" ${delay(560)}>
          <rect x="56" y="466" width="13" height="12" rx="3" fill="url(#g-law)"/>
          <rect x="71" y="466" width="13" height="12" rx="3" fill="url(#g-accent)"/>
          <text x="96" y="477" class="small-label">THE PAIR, MEASURED TOGETHER</text>
          <line x1="330" y1="462" x2="330" y2="482" stroke="#e8bf6a" stroke-width="2"
            stroke-dasharray="3 3"/>
          <text x="346" y="477" class="small-label">SUM OF THE TWO ALONE</text>
        </g>
        <text x="56" y="516" class="value-label">the bar lands on the tick — synergy is worth a point or two, no more</text>`);
    }

    function matchupsScene() {
      const matchups = latest.matchups || [];
      const axis = 470;
      const half = 180;
      const scale = Math.max(
        ...matchups.map(matchup => Math.abs(matchup.lawWinRate - .5)), .05);
      return svg(`
        <text x="56" y="48" class="chart-title">THE MOST ONE-SIDED CELLS OF THE 144-MATCHUP GRID</text>
        <line x1="${axis}" y1="72" x2="${axis}" y2="440" class="axis-line"/>
        <text x="${axis}" y="66" text-anchor="middle" class="mono-label">even</text>
        ${matchups.map((matchup, index) => {
          const y = 86 + index * 44;
          const edge = matchup.lawWinRate - .5;
          const width = Math.abs(edge) / scale * half;
          const positive = edge >= 0;
          const x = positive ? axis : axis - width;
          return `
            <g class="enter" ${delay(index * 70)}>
              <text x="56" y="${y + 16}" class="value-label"
                style="font-size:11.5px">${esc(matchup.law)} vs ${esc(matchup.outlaw)}</text>
              <rect x="${x}" y="${y}" width="${Math.max(width, 2)}" height="22" rx="5"
                fill="url(#${positive ? "g-law" : "g-accent"})" class="grow-x"
                style="--delay:${index * 70}ms;transform-origin:${axis}px center"/>
              <text x="${positive ? x + width + 12 : x - 12}" y="${y + 16}"
                text-anchor="${positive ? "start" : "end"}"
                class="small-label">${pct(matchup.lawWinRate)} LAW</text>
            </g>`;
        }).join("")}
        <text x="${axis - 16}" y="466" text-anchor="end" class="small-label">OUTLAW RUNS AWAY WITH IT</text>
        <text x="${axis + 16}" y="466" class="small-label">LAW RUNS AWAY WITH IT</text>
        <text x="56" y="514" class="value-label">${Math.round((latest.matchupSpread || 0) * 100)} points of win rate, decided at character selection</text>`);
    }

    function styleScene() {
      const items = [
        { label: "Blocks a shot when it can", value: style.defenseRate, fill: "url(#g-law)" },
        { label: "Uses a character ability", value: style.abilityRate, fill: "url(#g-gold)" },
        { label: "Attacks when it can", value: style.attackRate, fill: "url(#g-accent)" },
        { label: "Swaps position", value: style.swapRate, fill: "url(#g-muted)" },
        { label: "Shoots the back row", value: style.rearTargetRate, fill: "#4a352b" },
      ];
      const maximum = Math.max(...items.map(item => item.value));
      return svg(`
        <text x="104" y="62" class="chart-title">HOW OFTEN IT TAKES THE OPTION IT HAS</text>
        ${items.map((item, index) => {
          const y = 116 + index * 76;
          const width = Math.max(item.value / maximum * 470, 6);
          return `
            <text x="104" y="${y + 14}" class="value-label">${esc(item.label)}</text>
            <rect x="104" y="${y + 26}" width="500" height="20" rx="10" class="bar-bg"/>
            <rect x="104" y="${y + 26}" width="${width}" height="20" rx="10"
              fill="${item.fill}" class="grow-x" ${delay(index * 90)}/>
            <text x="622" y="${y + 42}" class="value-label">${pct(item.value)}</text>`;
        }).join("")}
        <text x="104" y="520" class="small-label">DEFENSIVE, PATIENT, AND ALLERGIC TO LONG-RANGE HEROICS</text>`);
    }

    function cardMetaScene() {
      const ranked = style.cards;
      const selected = [...ranked.slice(0, 4), ...ranked.slice(-4).reverse()];
      const maximum = selected[0].pickRate;
      return svg(`
        <text x="104" y="52" class="chart-title">HOW OFTEN A CARD GETS PICKED WHEN OFFERED</text>
        ${selected.map((cardItem, index) => {
          const y = 84 + index * 54;
          const width = cardItem.pickRate / maximum * 396;
          const premium = index < 4;
          return `
            <text x="104" y="${y + 17}" class="value-label">${esc(labelCard(cardItem.name))}</text>
            <rect x="300" y="${y}" width="400" height="24" rx="6" class="bar-bg"/>
            <rect x="300" y="${y}" width="${width}" height="24" rx="6"
              fill="${premium ? "url(#g-accent)" : "url(#g-muted)"}" class="grow-x"
              ${delay(index * 60)}/>
            <text x="${312 + width}" y="${y + 17}" class="small-label">${pct(cardItem.pickRate)}</text>`;
        }).join("")}
        <text x="104" y="524" class="small-label">TOP FOUR · BOTTOM FOUR · OUT OF 31 CARD NAMES</text>`);
    }

    function caveatScene() {
      const notes = [
        { label: "DETERMINISTIC TESTS", note: "gaps look bigger" },
        { label: "ONE SET OF DEALS", note: "≈2,048 of them" },
        { label: "SHARED BLIND SPOTS", note: "nothing punishes them" },
      ];
      return svg(`
        ${halo(400, 254, 190)}
        ${ring(400, 254, 118, "#db4b35", 0)}
        ${ring(400, 254, 118, "#c79b46", 1400)}
        <circle cx="400" cy="254" r="96" class="plate" stroke="#db4b35" stroke-width="2"/>
        <text x="400" y="248" text-anchor="middle" class="big-number"
          style="font-size:34px">NOT</text>
        <text x="400" y="286" text-anchor="middle" class="value-label">SOLVED</text>
        <g class="orbit" style="transform-origin:400px 254px">
          <circle cx="400" cy="136" r="6" class="accent-fill glow"/>
          <circle cx="502" cy="313" r="6" class="law-fill"/>
          <circle cx="298" cy="313" r="6" class="gold-fill"/>
        </g>
        ${notes.map((item, index) => `
          <g class="enter" ${delay(index * 140)}>
            <text x="${124 + index * 224}" y="438" class="value-label">${item.label}</text>
            <text x="${124 + index * 224}" y="462" class="small-label">${item.note}</text>
          </g>`).join("")}
        <text x="400" y="518" text-anchor="middle" class="small-label">A STRONG OPINION ABOUT THE GAME — NOT A PROOF</text>`);
    }

    /* ---------- wiring ---------- */

    const scenes = {
      chapter: chapterScene,
      // 01 · the game
      duel: duelScene,
      roster: rosterScene,
      turn: turnScene,
      combat: combatScene,
      hidden: hiddenScene,
      objective: objectiveScene,
      // 02 · the algorithm
      search: searchScene,
      policy: policyScene,
      gradient: gradientScene,
      clip: clipScene,
      update: updateScene,
      nontransitivity: nontransitivityScene,
      league: leagueScene,
      pfsp: pfspScene,
      // 03 · the network
      observation: observationScene,
      action: actionScene,
      scoring: scoringScene,
      network: networkScene,
      interaction: interactionScene,
      critic: criticScene,
      // 04 · the run
      scale: scaleScene,
      training: trainingScene,
      benchmark: benchmarkScene,
      confidence: confidenceScene,
      deploy: deployScene,
      // 05 · the meta
      sides: sidesScene,
      characters: charactersScene,
      pairs: pairsScene,
      matchups: matchupsScene,
      style: styleScene,
      cardmeta: cardMetaScene,
      caveat: caveatScene,
    };

    const figureNumbers = new Map();
    const stepParts = new Map();
    let figureCount = 0;
    let currentPart = "1";
    for (const step of steps) {
      if (step.dataset.part) {
        currentPart = step.dataset.part;
      } else {
        figureNumbers.set(step, ++figureCount);
      }
      stepParts.set(step, currentPart);
    }

    let active = null;
    function activate(step) {
      if (!step || step === active) return;
      active?.classList.remove("is-active");
      step.classList.add("is-active");
      active = step;

      const render = scenes[step.dataset.scene];
      stage.innerHTML = render ? render(step) : "";
      runFigureCounters(stage);
      if (figureLabel) {
        figureLabel.textContent = step.dataset.part
          ? `Act ${String(step.dataset.part).padStart(2, "0")}`
          : `Figure ${figureNumbers.get(step)}`;
      }
      if (figureText) figureText.textContent = step.dataset.figureCaption || "";

      const part = stepParts.get(step);
      railLinks.forEach(link => {
        link.classList.toggle("is-current", link.dataset.rail === part);
      });
    }

    function update() {
      const center = window.innerHeight * (window.innerWidth <= 1100 ? .72 : .5);
      const nearest = steps.reduce((best, step) => {
        const rect = step.getBoundingClientRect();
        const distance = Math.abs(rect.top + rect.height / 2 - center);
        return !best || distance < best.distance ? { step, distance } : best;
      }, null);
      activate(nearest?.step);
    }

    let scheduled = false;
    function scheduleUpdate() {
      if (scheduled) return;
      scheduled = true;
      requestAnimationFrame(() => {
        scheduled = false;
        update();
      });
    }
    window.addEventListener("scroll", scheduleUpdate, { passive: true });
    window.addEventListener("resize", scheduleUpdate);
    update();
  }

  function start() {
    try {
      initialize();
    } catch (error) {
      document.documentElement.classList.remove("js-scenes");
      const stage = document.querySelector("#visual-stage");
      if (stage) stage.textContent = "";
      console.error("about page figures failed to initialise", error);
    }
  }

  if (window.BANG_ABOUT_DATA) {
    start();
  } else {
    window.addEventListener("bang-about-data", start, { once: true });
  }
})();
