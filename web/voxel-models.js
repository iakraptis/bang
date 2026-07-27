(function (root, factory) {
  "use strict";

  const api = factory();
  if (typeof module === "object" && module.exports) {
    module.exports = api;
  } else {
    root.BangVoxelModels = api;
  }
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";

  /* ------------------------------------------------------------------
     Voxel figures for the battlefield in the middle of the table.

     Every character on the cards becomes a chunky little figure on the
     3D stage, and it has to be recognisable as that character at about
     forty pixels tall. That rules out portrait detail, so each figure is
     built from the handful of things you actually read at a glance: the
     shape of the hat, the hair, the beard, the colour of the coat, and
     what they are holding. Those are the traits below — one line per
     character, taken off their own card art — and a single builder turns
     them into voxels.

     The grid is y-up, x to the character's right, z toward the viewer,
     one unit per voxel; a figure stands about 23 voxels tall. Cells are
     written into a map keyed by position, so later parts paint over
     earlier ones: hair over the head, hat over the hair.
     ------------------------------------------------------------------ */

  const SKIN = {
    fair: "#f0cda4",
    tan: "#dcae80",
    weathered: "#c99465",
    deep: "#81583d",
  };

  const LEATHER = "#4a3527";
  const DARK_TROUSER = "#4c4038";

  /* Traits, in card order. Fields:
       build    slim | regular | broad   — silhouette width
       skin     face and hands
       hat      wide | flat | derby | sombrero | null
       hatColor / band
       hair     short | long | curly | slick | wild | braid | bald
       hairColor
       beard    null | stache | goatee | full | stubble
       coat     the main garment colour
       garment  coat | duster | serape | dress | fringe | suit
       trim     scarf, tie, sash or lace — the second colour you notice
       shirt    what shows at the chest
       weapon   revolver | twin | rifle | cards | pipe | cigar
       mask     null | domino | glasses
       trio     three figures abreast rather than one (Dalton Bros) */
  const CHARACTERS = {
    /* ---- Law ---- */
    alan_pinkertoon: {
      build: "regular", skin: SKIN.fair, hat: "derby", hatColor: "#5a4030", band: "#3a2a20",
      hair: "short", hairColor: "#8a8278", beard: "full", coat: "#6d4b33",
      garment: "coat", trim: "#8b6b3f", shirt: "#e0d3b4", weapon: "pipe",
    },
    annie_oakey: {
      build: "slim", skin: "#f2d0ab", hat: "flat", hatColor: "#e8e0c4", band: "#b9a97e",
      hair: "long", hairColor: "#4a2f22", beard: null, coat: "#c98d84",
      garment: "dress", trim: "#ecd9c4", shirt: "#e6d2c4", weapon: "rifle",
    },
    bart_masterson: {
      build: "regular", skin: SKIN.fair, hat: "derby", hatColor: "#4d6b46", band: "#2f4230",
      hair: "short", hairColor: "#2c1c14", beard: "stache", coat: "#4f6e48",
      garment: "suit", trim: "#c9a44c", shirt: "#e4dcc0", weapon: "cards",
    },
    bill_tightman: {
      build: "regular", skin: "#e3c6a4", hat: null, hatColor: null, band: null,
      hair: "short", hairColor: "#b0a898", beard: "stache", coat: "#4a4a44",
      garment: "coat", trim: "#a3542f", shirt: "#ddd2b8", weapon: "revolver",
    },
    buffalo_bell: {
      build: "broad", skin: SKIN.weathered, hat: "wide", hatColor: "#6b4a30", band: "#3d2a1c",
      hair: "long", hairColor: "#cbab6a", beard: "goatee", coat: "#cbb489",
      garment: "fringe", trim: "#8b6b3f", shirt: "#d8c8a0", weapon: "rifle",
    },
    dalon_ranger: {
      build: "regular", skin: SKIN.fair, hat: "wide", hatColor: "#e6ddc0", band: "#a89a72",
      hair: "short", hairColor: "#3a2a1e", beard: null, coat: "#6a7f92",
      garment: "coat", trim: "#3f5164", shirt: "#dcd2ba", weapon: "revolver",
      mask: "domino",
    },
    jango: {
      build: "regular", skin: SKIN.deep, hat: "wide", hatColor: "#3c2420", band: "#241512",
      hair: "short", hairColor: "#241713", beard: "stubble", coat: "#4a2f26",
      garment: "coat", trim: "#b6826b", shirt: "#c4a882",
      weapon: "revolver", mask: "glasses",
    },
    pat_garret: {
      build: "regular", skin: SKIN.fair, hat: null, hatColor: null, band: null,
      hair: "short", hairColor: "#33241b", beard: "stache", coat: "#38271f",
      garment: "suit", trim: "#2a1c16", shirt: "#e6ddc8", weapon: "revolver",
    },
    tex_killer: {
      build: "broad", skin: SKIN.tan, hat: "wide", hatColor: "#b08a58", band: "#6e5334",
      hair: "short", hairColor: "#3a2a1e", beard: "stubble", coat: "#c8a274",
      garment: "coat", trim: "#84594e", shirt: "#d8bd94", weapon: "twin",
    },
    the_stranger: {
      build: "regular", skin: SKIN.tan, hat: "wide", hatColor: "#6b4530", band: "#42291d",
      hair: "short", hairColor: "#4a3a28", beard: "stubble", coat: "#d8c9a4",
      garment: "serape", trim: "#9c4a2c", shirt: "#b8a884",
      weapon: "rifle",
    },
    wild_bill: {
      build: "regular", skin: "#f3cc95", hat: "wide", hatColor: "#7a5a45", band: "#4a3226",
      hair: "long", hairColor: "#c6a262", beard: "goatee", coat: "#ac886e",
      garment: "coat", trim: "#6e3b22", shirt: "#e2d6b8", weapon: "twin",
    },
    wyatt_ear: {
      build: "regular", skin: SKIN.fair, hat: "wide", hatColor: "#2a2420", band: "#191512",
      hair: "short", hairColor: "#3a2b22", beard: "stache", coat: "#9a9078",
      garment: "duster", trim: "#4d6a86", shirt: "#d6ccb2", weapon: "revolver",
    },

    /* ---- Outlaws ---- */
    babe_leroy: {
      build: "slim", skin: "#e8bf9a", hat: null, hatColor: null, band: null,
      hair: "wild", hairColor: "#22181a", beard: null, coat: "#7d3038",
      garment: "coat", trim: "#4a2026", shirt: "#b06a5a", weapon: "revolver",
    },
    bull_anderson: {
      build: "broad", skin: SKIN.weathered, hat: "wide", hatColor: "#b39366", band: "#6f5a3a",
      hair: "short", hairColor: "#5a4230", beard: "stache", coat: "#a8895e",
      garment: "coat", trim: "#6f5334", shirt: "#cbb489", weapon: "rifle",
    },
    cattle_katie: {
      build: "regular", skin: "#f0cba6", hat: null, hatColor: null, band: null,
      hair: "curly", hairColor: "#cbae72", beard: null, coat: "#6b7d8c",
      garment: "dress", trim: "#dcd0b4", shirt: "#c8d0d4", weapon: "revolver",
    },
    dalton_bros: {
      build: "broad", skin: SKIN.tan, hat: "wide", hatColor: "#5c4632", band: "#3a2b20",
      hair: "short", hairColor: "#3a2a1e", beard: "stache", coat: "#5c4632",
      garment: "coat", trim: "#9c3f34", shirt: "#c0b48c", weapon: "rifle",
      trio: [{ hatColor: "#4f5c3c", coat: "#4f5c3c", trim: "#6f8a52" },
             { hatColor: "#6b5238", coat: "#7d6a4a", trim: "#9c3f34" }],
    },
    jack_ransome: {
      build: "regular", skin: SKIN.tan, hat: "wide", hatColor: "#3a2f2a", band: "#241d1a",
      hair: "long", hairColor: "#2e2320", beard: "stubble", coat: "#7d8390",
      garment: "duster", trim: "#4f5560", shirt: "#b4b0a0", weapon: "revolver",
    },
    pearl_hat: {
      build: "slim", skin: "#f4d2ae", hat: null, hatColor: null, band: null,
      hair: "curly", hairColor: "#d9bd7a", beard: null, coat: "#e0cdae",
      garment: "dress", trim: "#8b6440", shirt: "#e8d8bc", weapon: "revolver",
    },
    sid_curry: {
      build: "broad", skin: SKIN.tan, hat: "derby", hatColor: "#3a3230", band: "#231e1c",
      hair: "short", hairColor: "#1e1815", beard: "full", coat: "#cfc8b4",
      garment: "duster", trim: "#8a8272", shirt: "#b0a894", weapon: "rifle",
    },
    slim_poet: {
      build: "slim", skin: SKIN.fair, hat: "derby", hatColor: "#6b6b3c", band: "#474828",
      hair: "short", hairColor: "#4a3524", beard: "goatee", coat: "#6f7043",
      garment: "suit", trim: "#c9b04e", shirt: "#d8cc94", weapon: "revolver",
    },
    soundance_kid: {
      build: "regular", skin: SKIN.tan, hat: "wide", hatColor: "#6b4a33", band: "#42301f",
      hair: "short", hairColor: "#4a3524", beard: null, coat: "#b09a72",
      garment: "duster", trim: "#8a6a44", shirt: "#d0c09a", weapon: "twin",
    },
    toco_ramirez: {
      build: "regular", skin: SKIN.deep, hat: "sombrero", hatColor: "#d8c07e", band: "#a08a52",
      hair: "short", hairColor: "#1e1613", beard: "goatee", coat: "#ddd2b4",
      garment: "serape", trim: "#7d7a48", shirt: "#7d7a48", weapon: "cigar",
    },
    tom_thorn: {
      build: "regular", skin: SKIN.tan, hat: null, hatColor: null, band: null,
      hair: "long", hairColor: "#2e241d", beard: "stubble", coat: "#4a5560",
      garment: "coat", trim: "#4f7d80", shirt: "#7a5335", weapon: "revolver",
    },
    west_harding: {
      build: "slim", skin: SKIN.fair, hat: null, hatColor: null, band: null,
      hair: "slick", hairColor: "#241a14", beard: null, coat: "#2a2622",
      garment: "suit", trim: "#1a1614", shirt: "#e8e0cc", weapon: "twin",
    },
  };

  // A character the roster gains later still gets a figure to stand on.
  const DEFAULT_CHARACTER = {
    build: "regular", skin: SKIN.tan, hat: "wide", hatColor: "#6b4a33", band: "#42301f",
    hair: "short", hairColor: "#3a2a1e", beard: null, coat: "#8a6a48",
    garment: "coat", trim: "#6b5236", shirt: "#cdbd98", weapon: "revolver",
  };

  const GUN_METAL = "#6f7378";
  const GUN_DARK = "#3d4045";
  const WOOD = "#6b4527";
  const BRASS = "#c9a44c";

  /* Every card that reaches the table drops a prop onto the battlefield.
     Shapes are shared — a Colt and a Schofield are both revolvers — and the
     tint is what tells them apart. */
  const CARD_PROPS = {
    colt: { prop: "revolver", tint: GUN_METAL },
    schofield: { prop: "revolver", tint: "#8a8f96" },
    remington: { prop: "revolver", tint: "#5c6066" },
    peacemaker: { prop: "revolver", tint: "#9aa0a6" },
    derringer: { prop: "revolver", tint: "#c9a44c", small: true },
    winchester: { prop: "rifle", tint: WOOD },
    carbine: { prop: "rifle", tint: "#7d5630" },
    shotgun: { prop: "rifle", tint: "#4a3527" },
    gatling: { prop: "gatling", tint: GUN_DARK },
    fanning: { prop: "revolver", tint: "#b0723a" },
    knife: { prop: "knife", tint: "#b9bec4" },
    duel: { prop: "duel", tint: GUN_METAL },
    indians: { prop: "arrow", tint: "#9c4a2c" },
    dynamite: { prop: "dynamite", tint: "#a5352b" },
    rattlesnake: { prop: "snake", tint: "#7fbe5e" },
    beer: { prop: "mug", tint: "#c9922f" },
    strong_ale: { prop: "mug", tint: "#8a4f22" },
    missed: { prop: "slug", tint: "#c8ccd2" },
    return_fire: { prop: "ricochet", tint: "#c98d5a" },
    barrel: { prop: "barrel", tint: "#7d5630" },
    hat: { prop: "hatprop", tint: "#6b4a30" },
    gun_belt: { prop: "belt", tint: LEATHER },
    scope: { prop: "scope", tint: "#4a5560" },
    bank: { prop: "safe", tint: "#4f5560" },
    clock: { prop: "clock", tint: "#c9a44c" },
    stealing: { prop: "hand", tint: "#e0b98c" },
    schoolmarm: { prop: "book", tint: "#8a4636" },
    general_store: { prop: "cards", tint: "#e4dcc0" },
    wagon: { prop: "wagon", tint: "#8a6a44" },
    water: { prop: "bucket", tint: "#5a8fa8" },
    stampede: { prop: "steer", tint: "#6b4a30" },
    /* Not a card — a single card. A great deal of this game is cards moving
       between hands, and on the battlefield that has to be a thing you can
       watch cross the dust. */
    loose_card: { prop: "chit", tint: "#e8dcc0" },
  };

  const DEFAULT_PROP = { prop: "cards", tint: "#d8cbb0" };

  /* ---------- the voxel canvas ---------- */

  function grid() {
    const cells = new Map();
    return {
      set(x, y, z, color) {
        if (!color) return;
        cells.set(`${x}|${y}|${z}`, { x, y, z, color });
      },
      // Inclusive box; later boxes paint over earlier ones.
      box(x0, y0, z0, x1, y1, z1, color) {
        if (!color) return;
        for (let x = Math.min(x0, x1); x <= Math.max(x0, x1); x += 1) {
          for (let y = Math.min(y0, y1); y <= Math.max(y0, y1); y += 1) {
            for (let z = Math.min(z0, z1); z <= Math.max(z0, z1); z += 1) {
              this.set(x, y, z, color);
            }
          }
        }
      },
      // Take a box back out again. A poncho or a duster is painted across the
      // whole span of the shoulders before the arms go on, so a figure whose
      // gun arm is being lifted out into its own piece has to have that column
      // cleared or the coat keeps an arm-shaped stub where the arm used to be.
      clear(x0, y0, z0, x1, y1, z1) {
        for (let x = Math.min(x0, x1); x <= Math.max(x0, x1); x += 1) {
          for (let y = Math.min(y0, y1); y <= Math.max(y0, y1); y += 1) {
            for (let z = Math.min(z0, z1); z <= Math.max(z0, z1); z += 1) {
              cells.delete(`${x}|${y}|${z}`);
            }
          }
        }
      },
      // Mirror a run of cells across the centre line, for paired limbs.
      list() { return Array.from(cells.values()); },
      get size() { return cells.size; },
    };
  }

  /* ---------- figure parts ----------
     Heights are shared by every figure so the stage can rely on them.

     The proportions are deliberately stocky — about three heads tall rather
     than seven. A realistically proportioned figure reads as a totem pole at
     the size these are actually drawn on the battlefield; a chunky one still
     reads as a person wearing a hat. */

  const HEIGHTS = { boot: 0, leg: 2, belt: 7, torso: 8, shoulder: 13, neck: 14, head: 15, crown: 18, hat: 19 };

  function halfWidth(build) {
    return build === "broad" ? 4 : build === "slim" ? 2 : 3;
  }

  function legsAndBoots(out, traits, hw) {
    const gap = hw >= 3 ? 1 : 0;
    for (const side of [-1, 1]) {
      const inner = side * (gap + 1);
      const outer = side * hw;
      // Boots reach a voxel further forward than the leg: toes.
      out.box(inner, HEIGHTS.boot, -1, outer, HEIGHTS.boot + 1, 2, LEATHER);
      out.box(inner, HEIGHTS.leg, -1, outer, HEIGHTS.belt - 1, 1,
              traits.garment === "dress" ? traits.coat : DARK_TROUSER);
    }
    if (traits.garment === "dress") {
      // a skirt closes the gap and flares past the hips
      out.box(-hw - 1, HEIGHTS.leg, -2, hw + 1, HEIGHTS.leg + 2, 2, traits.coat);
      out.box(-hw, HEIGHTS.leg + 3, -1, hw, HEIGHTS.belt - 1, 1, traits.coat);
    }
  }

  /* The body is built five voxels deep, not three. The figures are seen from
     the side on the battlefield — the two rows face each other across it —
     and a three-deep torso under a nine-wide hat brim reads as a table on a
     stick from that angle. */
  function torso(out, traits, hw) {
    out.box(-hw, HEIGHTS.belt, -2, hw, HEIGHTS.belt, 2, LEATHER);
    out.set(0, HEIGHTS.belt, 2, "#c9a44c"); // buckle
    out.box(-hw, HEIGHTS.torso, -2, hw, HEIGHTS.shoulder, 2, traits.coat);
    // the shirt or vest showing down the front
    out.box(-1, HEIGHTS.torso, 2, 1, HEIGHTS.shoulder - 1, 2, traits.shirt);
    if (traits.garment === "suit") {
      out.box(-hw, HEIGHTS.torso, 2, -1, HEIGHTS.shoulder, 2, traits.coat);
      out.box(1, HEIGHTS.torso, 2, hw, HEIGHTS.shoulder, 2, traits.coat);
    }
  }

  function garmentOverlay(out, traits, hw) {
    switch (traits.garment) {
      case "duster":
        // a long coat that falls past the knees and swings wider than the body
        out.box(-hw - 1, HEIGHTS.leg + 1, -1, -hw - 1, HEIGHTS.shoulder, 1, traits.coat);
        out.box(hw + 1, HEIGHTS.leg + 1, -1, hw + 1, HEIGHTS.shoulder, 1, traits.coat);
        out.box(-hw, HEIGHTS.leg + 1, -2, hw, HEIGHTS.belt, -2, traits.coat);
        out.box(-hw, HEIGHTS.leg + 1, 2, -1, HEIGHTS.belt - 1, 2, traits.coat);
        out.box(1, HEIGHTS.leg + 1, 2, hw, HEIGHTS.belt - 1, 2, traits.coat);
        break;
      case "serape": {
        // a poncho: a slab over the shoulders, banded in the trim colour
        for (let y = HEIGHTS.torso; y <= HEIGHTS.shoulder; y += 1) {
          const banded = (y - HEIGHTS.torso) % 3 === 1;
          out.box(-hw - 1, y, -2, hw + 1, y, 2, banded ? traits.trim : traits.coat);
        }
        out.box(-hw - 1, HEIGHTS.belt, -2, hw + 1, HEIGHTS.belt, 2, traits.coat);
        out.box(-1, HEIGHTS.torso, 2, 1, HEIGHTS.shoulder - 1, 2, traits.shirt);
        break;
      }
      case "fringe":
        // buckskin: a fringed yoke across the chest and down the arms
        out.box(-hw, HEIGHTS.shoulder - 1, 2, hw, HEIGHTS.shoulder - 1, 2, traits.trim);
        for (let x = -hw; x <= hw; x += 2) out.set(x, HEIGHTS.torso + 1, 2, traits.trim);
        for (const side of [-1, 1]) {
          out.box(side * (hw + 1), HEIGHTS.torso, 2, side * (hw + 1), HEIGHTS.torso + 2, 2, traits.trim);
        }
        break;
      case "dress":
        out.box(-1, HEIGHTS.shoulder, 2, 1, HEIGHTS.shoulder, 2, traits.trim); // lace collar
        break;
      default:
        break;
    }
  }

  /* One arm is the gun arm, and the stage needs it on a hinge: a figure that
     cannot raise an arm cannot aim, and a Winchester that hangs in mid-air
     beside a static sleeve reads as a bug rather than a draw. `skipArm` leaves
     that side out of the body so `buildArm` can supply it as its own piece,
     pivoting at the shoulder. Everything else about the figure is unchanged,
     and a full build (no skip) still produces the whole character. */
  const ARM_SIDE = 1;

  function armsAndHands(out, traits, hw, skipArm) {
    // The sleeves are a shade off the coat: same cloth, but without the break
    // the arms and the chest merge into one wide slab from any distance.
    const sleeve = shade(traits.coat, -22);
    for (const side of [-1, 1]) {
      if (skipArm && side === ARM_SIDE) continue;
      const x = side * (hw + 1);
      out.box(x, HEIGHTS.torso, -1, x, HEIGHTS.shoulder, 1, sleeve);
      out.box(x, HEIGHTS.belt, -1, x, HEIGHTS.torso - 1, 1, traits.skin);
    }
  }

  /* The gun arm on its own, in the hinge's coordinates: the shoulder joint is
     the origin and the arm hangs straight down from it, so rotating the piece
     about x swings it from resting at the hip to level with the horizon. The
     hand is the last voxel down, and whatever the character is holding hangs
     off that point. */
  function buildArm(traits, hw) {
    const out = grid();
    const sleeve = shade(traits.coat, -22);
    const cuff = HEIGHTS.torso - HEIGHTS.shoulder;              // -5
    const wrist = HEIGHTS.belt - HEIGHTS.shoulder;              // -6
    out.box(0, 0, -1, 0, cuff, 1, sleeve);
    out.box(0, cuff - 1, -1, 0, wrist, 1, traits.skin);
    return {
      voxels: out.list(),
      // Where the arm hangs from, in the figure's own grid.
      pivot: [ARM_SIDE * (hw + 1), HEIGHTS.shoulder, 0],
      // Where the grip of a held weapon sits, in the hinge's coordinates.
      hand: [0, wrist, 1],
      length: -wrist,
    };
  }

  function neckAndHead(out, traits) {
    out.box(-1, HEIGHTS.neck, -1, 1, HEIGHTS.neck, 1, traits.skin);
    if (traits.trim && traits.garment !== "serape") {
      out.box(-2, HEIGHTS.neck, -2, 2, HEIGHTS.neck, 2, traits.trim); // scarf / tie
    }
    out.box(-2, HEIGHTS.head, -2, 2, HEIGHTS.crown, 2, traits.skin);
    // eyes on the front face
    out.set(-1, HEIGHTS.crown - 1, 2, "#241a14");
    out.set(1, HEIGHTS.crown - 1, 2, "#241a14");
    if (traits.mask === "domino") {
      out.box(-2, HEIGHTS.crown - 1, 2, 2, HEIGHTS.crown - 1, 2, "#1d1a19");
    }
    if (traits.mask === "glasses") {
      out.set(-1, HEIGHTS.crown - 1, 2, "#4f7a54");
      out.set(1, HEIGHTS.crown - 1, 2, "#4f7a54");
      out.set(0, HEIGHTS.crown - 1, 2, "#2a2a26");
    }
    switch (traits.beard) {
      case "full":
        out.box(-2, HEIGHTS.head, 2, 2, HEIGHTS.crown - 2, 2, traits.hairColor);
        out.box(-2, HEIGHTS.head - 1, 1, 2, HEIGHTS.head - 1, 2, traits.hairColor);
        break;
      case "goatee":
        out.box(-1, HEIGHTS.head, 2, 1, HEIGHTS.head + 1, 2, traits.hairColor);
        break;
      case "stache":
        out.box(-2, HEIGHTS.head + 2, 2, 2, HEIGHTS.head + 2, 2, traits.hairColor);
        break;
      case "stubble":
        out.box(-2, HEIGHTS.head, 2, 2, HEIGHTS.head, 2, shade(traits.skin, -34));
        break;
      default:
        break;
    }
  }

  function hair(out, traits) {
    const colour = traits.hairColor;
    switch (traits.hair) {
      case "long":
        out.box(-2, HEIGHTS.head, -2, 2, HEIGHTS.crown, -2, colour);
        out.box(-2, HEIGHTS.crown, -1, 2, HEIGHTS.crown, 2, colour);
        for (const side of [-2, 2]) out.box(side, HEIGHTS.neck - 1, -2, side, HEIGHTS.crown, 1, colour);
        break;
      case "curly":
        out.box(-3, HEIGHTS.crown - 1, -2, 3, HEIGHTS.crown + 1, 2, colour);
        out.box(-3, HEIGHTS.head + 1, -2, -3, HEIGHTS.crown, 1, colour);
        out.box(3, HEIGHTS.head + 1, -2, 3, HEIGHTS.crown, 1, colour);
        break;
      case "wild":
        out.box(-3, HEIGHTS.head - 1, -2, 3, HEIGHTS.crown + 1, -1, colour);
        out.box(-2, HEIGHTS.crown, -1, 2, HEIGHTS.crown, 2, colour);
        out.set(-2, HEIGHTS.crown - 1, 2, colour);
        out.set(2, HEIGHTS.crown - 1, 2, colour);
        break;
      case "slick":
        out.box(-2, HEIGHTS.crown, -2, 2, HEIGHTS.crown, 2, colour);
        out.box(-2, HEIGHTS.crown - 1, -2, 2, HEIGHTS.crown - 1, -1, colour);
        break;
      case "bald":
        break;
      default: // short
        out.box(-2, HEIGHTS.crown, -2, 2, HEIGHTS.crown, 2, colour);
        out.box(-2, HEIGHTS.crown - 1, -2, 2, HEIGHTS.crown - 1, -2, colour);
        break;
    }
  }

  function hat(out, traits) {
    if (!traits.hat) return;
    const { hatColor, band } = traits;
    switch (traits.hat) {
      case "wide":
        out.box(-5, HEIGHTS.hat, -4, 5, HEIGHTS.hat, 4, hatColor);
        out.box(-2, HEIGHTS.hat + 1, -2, 2, HEIGHTS.hat + 2, 2, hatColor);
        out.box(-2, HEIGHTS.hat, -2, 2, HEIGHTS.hat, 2, band);
        break;
      case "flat":
        out.box(-5, HEIGHTS.hat, -4, 5, HEIGHTS.hat, 4, hatColor);
        out.box(-2, HEIGHTS.hat + 1, -2, 2, HEIGHTS.hat + 1, 2, hatColor);
        out.box(-2, HEIGHTS.hat, -2, 2, HEIGHTS.hat, 2, band);
        break;
      case "derby":
        out.box(-4, HEIGHTS.hat, -3, 4, HEIGHTS.hat, 3, hatColor);
        out.box(-2, HEIGHTS.hat + 1, -2, 2, HEIGHTS.hat + 2, 2, hatColor);
        out.box(-2, HEIGHTS.hat, -2, 2, HEIGHTS.hat, 2, band);
        break;
      case "sombrero":
        out.box(-6, HEIGHTS.hat, -5, 6, HEIGHTS.hat, 5, hatColor);
        out.box(-4, HEIGHTS.hat + 1, -3, 4, HEIGHTS.hat + 1, 3, hatColor);
        out.box(-2, HEIGHTS.hat + 2, -2, 2, HEIGHTS.hat + 3, 2, hatColor);
        out.box(-2, HEIGHTS.hat + 1, -2, 2, HEIGHTS.hat + 1, 2, band);
        break;
      default:
        break;
    }
  }

  function weapon(out, traits, hw, skipArm) {
    const x = hw + 1;
    const y = HEIGHTS.torso - 1;
    switch (traits.weapon) {
      case "twin":
        for (const side of [-1, 1]) {
          if (skipArm && side === ARM_SIDE) continue;
          out.box(side * x, y, 2, side * x, y, 3, GUN_METAL);
          out.set(side * x, y - 1, 2, GUN_DARK);
        }
        break;
      case "rifle":
        // carried across the chest, muzzle up over the shoulder
        out.box(-hw, HEIGHTS.torso, 3, hw + 1, HEIGHTS.torso, 3, WOOD);
        out.box(hw + 1, HEIGHTS.torso + 1, 3, hw + 1, HEIGHTS.shoulder + 2, 3, GUN_DARK);
        break;
      case "cards":
        out.box(x, y, 2, x, y + 1, 3, "#e4dcc0");
        out.set(x, y + 1, 4, "#a5322b");
        break;
      case "pipe":
        out.box(1, HEIGHTS.head + 1, 3, 2, HEIGHTS.head + 1, 3, "#4a3527");
        out.set(2, HEIGHTS.head + 2, 3, "#2a2420");
        break;
      case "cigar":
        out.box(1, HEIGHTS.head + 1, 3, 2, HEIGHTS.head + 1, 3, "#3d2a1c");
        out.set(3, HEIGHTS.head + 1, 3, "#e2553b");
        break;
      default: // revolver
        if (skipArm) break;   // it goes in the hinged hand instead
        out.box(x, y, 2, x, y, 3, GUN_METAL);
        out.set(x, y - 1, 2, GUN_DARK);
        break;
    }
  }

  /* What a character is holding when nothing is happening. Gunhands keep a
     revolver in the hinged hand so the draw has something to draw; a rifle,
     a hand of cards or a cigar are part of the body and stay there. */
  function idleWeaponFor(traits) {
    return traits.weapon === "revolver" || traits.weapon === "twin" ? "colt" : null;
  }

  function shade(hex, amount) {
    const value = parseInt(String(hex).slice(1), 16);
    const clamp = channel => Math.max(0, Math.min(255, channel + amount));
    return "#" + [
      clamp((value >> 16) & 255), clamp((value >> 8) & 255), clamp(value & 255),
    ].map(channel => channel.toString(16).padStart(2, "0")).join("");
  }

  function figureInto(out, traits, offsetX = 0, skipArm = false) {
    const hw = halfWidth(traits.build);
    const shifted = offsetX
      ? {
          set: (x, y, z, c) => out.set(x + offsetX, y, z, c),
          box: (x0, y0, z0, x1, y1, z1, c) =>
            out.box(x0 + offsetX, y0, z0, x1 + offsetX, y1, z1, c),
          clear: (x0, y0, z0, x1, y1, z1) =>
            out.clear(x0 + offsetX, y0, z0, x1 + offsetX, y1, z1),
        }
      : out;
    legsAndBoots(shifted, traits, hw);
    torso(shifted, traits, hw);
    garmentOverlay(shifted, traits, hw);
    armsAndHands(shifted, traits, hw, skipArm);
    neckAndHead(shifted, traits);
    hair(shifted, traits);
    hat(shifted, traits);
    weapon(shifted, traits, hw, skipArm);
    if (skipArm) {
      // Exactly the arm's own footprint, no wider: a poncho still hangs over
      // the shoulder at the edges, it just no longer fills the sleeve.
      shifted.clear(ARM_SIDE * (hw + 1), HEIGHTS.belt, -1, ARM_SIDE * (hw + 1), HEIGHTS.shoulder, 1);
    }
  }

  function traitsFor(key) {
    return CHARACTERS[key] || DEFAULT_CHARACTER;
  }

  /* A figure, as a flat voxel list plus the bounds the stage needs to sit it
     on the ground and spin it about its own middle.

     Two versions of the same character come back. `voxels` is the whole
     figure, which is what anything drawing a still picture of them wants.
     `body` is the same figure with the gun arm left off, and `arm` is that
     arm on its own hinge — the stage draws those two as separate meshes so
     the character can raise a weapon. The bounds are always the whole
     figure's, so the arm coming off never moves anyone off their mark. */
  function buildFigure(key) {
    const traits = traitsFor(key);
    const build = skipArm => {
      const out = grid();
      if (traits.trio) {
        /* The Daltons ride three abreast: a full figure flanked by two
           brothers, each in his own coat. Only the middle one aims, and the
           brothers stand clear of his gun arm rather than inside it — packed
           any tighter and lifting that arm out onto its hinge would leave it
           swinging through a brother's chest. */
        figureInto(out, traits, 0, skipArm);
        traits.trio.forEach((brother, index) => {
          figureInto(out, { ...traits, ...brother }, (index === 0 ? -1 : 1) * (halfWidth(traits.build) + 7));
        });
      } else {
        figureInto(out, traits, 0, skipArm);
      }
      return out.list();
    };
    const voxels = build(false);
    return {
      key,
      traits,
      voxels,
      bounds: bounds(voxels),
      body: build(true),
      arm: buildArm(traits, halfWidth(traits.build)),
      idleWeapon: idleWeaponFor(traits),
    };
  }

  function bounds(voxels) {
    if (!voxels.length) return { min: [0, 0, 0], max: [0, 0, 0], size: [0, 0, 0] };
    const min = [Infinity, Infinity, Infinity];
    const max = [-Infinity, -Infinity, -Infinity];
    for (const voxel of voxels) {
      min[0] = Math.min(min[0], voxel.x); max[0] = Math.max(max[0], voxel.x);
      min[1] = Math.min(min[1], voxel.y); max[1] = Math.max(max[1], voxel.y);
      min[2] = Math.min(min[2], voxel.z); max[2] = Math.max(max[2], voxel.z);
    }
    return { min, max, size: [max[0] - min[0] + 1, max[1] - min[1] + 1, max[2] - min[2] + 1] };
  }

  /* ---------- props ----------
     The card that was just played lands on the battlefield as an object
     rather than a rectangle: a revolver, a stick of dynamite, a beer mug. */

  const PROP_BUILDERS = {
    revolver(out, tint, small) {
      const length = small ? 2 : 4;
      out.box(0, 2, 0, length, 2, 0, tint);          // barrel
      out.box(0, 1, 0, 1, 1, 0, shade(tint, -30));   // frame
      out.box(0, 0, 0, 0, 1, 0, WOOD);               // grip
      if (!small) out.set(length, 3, 0, shade(tint, 40));
    },
    rifle(out, tint) {
      out.box(0, 1, 0, 8, 1, 0, GUN_DARK);
      out.box(0, 0, 0, 3, 0, 0, tint);
      out.set(8, 2, 0, shade(GUN_DARK, 40));
    },
    gatling(out, tint) {
      for (let z = -1; z <= 1; z += 1) out.box(0, 1 + Math.abs(z) * 0, 0, 6, 1, z, tint);
      out.box(0, 0, -1, 2, 2, 1, shade(tint, -25));
      out.box(2, -2, 0, 2, 0, 0, WOOD);
    },
    knife(out, tint) {
      out.box(0, 0, 0, 4, 0, 0, tint);
      out.box(-2, 0, 0, -1, 0, 0, WOOD);
      out.set(5, 0, 0, shade(tint, 40));
    },
    duel(out, tint) {
      out.box(-4, 1, 0, -2, 1, 0, tint);
      out.box(2, 1, 0, 4, 1, 0, tint);
      out.set(-1, 2, 0, "#e2553b");
      out.set(1, 2, 0, "#e2553b");
    },
    arrow(out, tint) {
      out.box(0, 0, 0, 6, 0, 0, WOOD);
      out.set(7, 0, 0, "#b9bec4");
      out.box(0, 1, 0, 1, 1, 0, tint);
    },
    dynamite(out, tint) {
      out.box(-1, 0, -1, 1, 3, 1, tint);
      out.box(-1, 1, -1, 1, 1, 1, "#e8d7af");
      out.box(0, 4, 0, 0, 6, 0, "#8a7a5a");
      out.set(0, 7, 0, "#ffb347");
    },
    snake(out, tint) {
      const dark = shade(tint, -40);
      for (let i = 0; i < 8; i += 1) {
        out.set(i, 0, Math.round(Math.sin(i * 0.9) * 2), i % 2 ? tint : dark);
      }
      out.set(8, 1, Math.round(Math.sin(8 * 0.9) * 2), tint);
      out.set(9, 1, Math.round(Math.sin(8 * 0.9) * 2), "#e2553b");
    },
    mug(out, tint) {
      out.box(-1, 0, -1, 1, 3, 1, tint);
      out.box(-1, 4, -1, 1, 4, 1, "#f0e6cc"); // foam
      out.box(2, 1, 0, 2, 2, 0, shade(tint, -30)); // handle
    },
    shield(out, tint) {
      for (let y = 0; y <= 4; y += 1) {
        const half = y === 4 ? 0 : 2 - Math.floor(y / 2);
        out.box(-half, y, 0, half, y, 0, tint);
      }
      out.box(-2, 0, 0, 2, 2, 0, tint);
    },
    barrel(out, tint) {
      out.box(-1, 0, -1, 1, 4, 1, tint);
      out.box(-2, 1, -1, -2, 3, 1, tint);
      out.box(2, 1, -1, 2, 3, 1, tint);
      out.box(-1, 1, -2, 1, 3, -2, tint);
      out.box(-1, 1, 2, 1, 3, 2, tint);
      out.box(-2, 2, -2, 2, 2, 2, "#4a4038"); // iron hoop
    },
    hatprop(out, tint) {
      out.box(-3, 0, -2, 3, 0, 2, tint);
      out.box(-1, 1, -1, 1, 2, 1, tint);
    },
    belt(out, tint) {
      out.box(-4, 0, 0, 4, 0, 0, tint);
      out.set(0, 0, 1, "#c9a44c");
      out.box(-3, -1, 0, -3, -1, 0, GUN_METAL);
      out.box(3, -1, 0, 3, -1, 0, GUN_METAL);
    },
    scope(out, tint) {
      out.box(0, 0, 0, 5, 0, 0, tint);
      out.set(6, 0, 0, "#8fc7e0");
      out.set(-1, 0, 0, "#8fc7e0");
    },
    safe(out, tint) {
      out.box(-2, 0, -2, 2, 4, 2, tint);
      out.box(-1, 1, 3, 1, 3, 3, shade(tint, 30));
      out.set(0, 2, 4, "#c9a44c");
    },
    clock(out, tint) {
      out.box(-2, 0, 0, 2, 4, 0, tint);
      out.box(-1, 1, 1, 1, 3, 1, "#e8d7af");
      out.set(0, 2, 2, "#2a2420");
      out.set(0, 3, 2, "#2a2420");
    },
    hand(out, tint) {
      out.box(0, 0, 0, 3, 0, 0, tint);
      out.box(3, 0, -1, 4, 0, 1, tint);
      out.set(4, 1, 0, tint);
      out.set(4, -1, 0, tint);
    },
    cards(out, tint) {
      for (let i = 0; i < 4; i += 1) {
        out.box(i * 2, i, 0, i * 2 + 1, i, 1, i % 2 ? tint : shade(tint, -25));
      }
    },
    wagon(out, tint) {
      out.box(-3, 1, -1, 3, 3, 1, tint);
      out.box(-3, 4, -1, 3, 4, 1, "#e4dcc0"); // canvas top
      out.box(-2, 0, -2, -2, 0, 2, "#4a3527");
      out.box(2, 0, -2, 2, 0, 2, "#4a3527");
    },
    flask(out, tint) {
      out.box(-1, 0, -1, 1, 3, 1, tint);
      out.box(0, 4, 0, 0, 4, 0, "#8a7a5a");
      out.box(-2, 1, 0, -2, 2, 0, shade(tint, -30));
    },
    boot(out, tint) {
      out.box(0, 0, -1, 3, 0, 1, tint);
      out.box(0, 1, -1, 1, 3, 1, tint);
      out.box(0, 4, -1, 1, 4, 1, shade(tint, 30));
    },
    // A bullet going past rather than into: the thing a Missed! is about.
    slug(out, tint) {
      out.box(0, 0, 0, 2, 0, 0, tint);
      out.set(3, 0, 0, shade(tint, 40));
      out.box(-3, 0, 0, -1, 0, 0, "#8a7a5a");   // the streak behind it
      out.set(-4, 1, 0, "#8a7a5a");
      out.set(-4, -1, 0, "#8a7a5a");
    },
    // A shot turned round and sent back, with the spark where it bounced.
    ricochet(out, tint) {
      out.box(0, 0, 0, 2, 0, 0, tint);
      out.box(2, 1, 0, 4, 3, 0, shade(tint, 45));
      out.set(5, 4, 0, "#ffb347");
      out.set(3, -1, 0, "#ffb347");
    },
    chit(out, tint) {
      out.box(-1, -2, 0, 1, 2, 0, tint);
      out.box(-1, 2, 0, 1, 2, 0, shade(tint, -25));
      out.set(0, 0, 1, "#a5322b");
      out.set(0, 0, -1, "#a5322b");
    },
    book(out, tint) {
      out.box(-2, 0, -2, 2, 1, 2, tint);
      out.box(-2, 2, -2, 2, 2, 2, "#e8dcc0");   // pages
      out.box(-2, 0, -2, -2, 2, 2, shade(tint, -30));  // spine
      out.set(2, 2, 0, "#c9a44c");              // clasp
    },
    bucket(out, tint) {
      const pail = "#7d6a4a";
      out.box(-2, 0, -2, 2, 3, 2, pail);
      out.box(-2, 4, -2, 2, 4, 2, tint);        // the water in it
      out.box(-3, 4, 0, 3, 5, 0, shade(pail, -25));    // handle
      out.set(3, 5, 0, tint);
      out.set(4, 6, 0, shade(tint, 35));        // a slop going over the side
    },
    steer(out, tint) {
      out.box(-4, 2, -2, 3, 4, 1, tint);        // body
      out.box(3, 3, -1, 5, 5, 1, shade(tint, 20));     // head
      out.box(5, 6, -3, 6, 6, -2, "#e0d6bc");   // horns
      out.box(5, 6, 2, 6, 6, 3, "#e0d6bc");
      for (const x of [-3, 2]) for (const z of [-2, 2]) out.box(x, 0, z, x, 1, z, shade(tint, -30));
      out.box(-5, 3, 0, -4, 5, 0, shade(tint, -20));   // tail
    },
  };

  function buildProp(cardKey) {
    const spec = CARD_PROPS[cardKey] || DEFAULT_PROP;
    const builder = PROP_BUILDERS[spec.prop] || PROP_BUILDERS.cards;
    const out = grid();
    builder(out, spec.tint, !!spec.small);
    const voxels = out.list();
    return { key: cardKey, prop: spec.prop, tint: spec.tint, voxels, bounds: bounds(voxels) };
  }

  /* ---------- weapons in the hand ----------

     A thrown prop and a held weapon are not the same object. A prop is seen
     tumbling across the field at speed and only has to read as a silhouette;
     a held weapon is seen at rest in somebody's fist, pointed at the person
     they are about to shoot, and it has to be the gun that is printed on the
     card — a Winchester is a long rifle with a lever, a Derringer is a palmed
     stub, a Shotgun has two barrels.

     They are built in the weapon's own frame: the grip is the origin, the
     barrel runs along +x and the sights are +y, so `muzzle` is simply how far
     down +x the bullet leaves from. The stage turns that frame to match
     whichever hand is holding it. */

  const HELD_WEAPONS = {
    colt: { shape: "revolver", tint: GUN_METAL, barrel: 4 },
    schofield: { shape: "revolver", tint: "#8a8f96", barrel: 5 },
    remington: { shape: "revolver", tint: "#5c6066", barrel: 4 },
    peacemaker: { shape: "revolver", tint: "#9aa0a6", barrel: 6 },
    fanning: { shape: "revolver", tint: "#b0723a", barrel: 4 },
    derringer: { shape: "derringer", tint: BRASS, barrel: 2 },
    winchester: { shape: "rifle", tint: WOOD, barrel: 11 },
    carbine: { shape: "rifle", tint: "#7d5630", barrel: 8 },
    shotgun: { shape: "shotgun", tint: "#4a3527", barrel: 8 },
    gatling: { shape: "gatling", tint: GUN_DARK, barrel: 9 },
    knife: { shape: "knife", tint: "#b9bec4", barrel: 5 },
    indians: { shape: "bow", tint: "#7d5630", barrel: 6 },
    duel: { shape: "revolver", tint: GUN_METAL, barrel: 4 },
  };

  const WEAPON_BUILDERS = {
    revolver(out, tint, barrel) {
      out.box(-1, -3, 0, 0, -1, 0, WOOD);                    // grip
      out.set(-1, -4, 0, WOOD);
      out.box(0, -1, 0, 2, 0, 0, shade(tint, -25));          // frame
      out.box(1, 0, -1, 2, 0, 1, tint);                      // cylinder
      out.box(2, 0, 0, barrel + 2, 0, 0, tint);              // barrel
      out.set(0, 1, 0, shade(tint, -45));                    // hammer
      out.set(barrel + 2, 1, 0, shade(tint, 45));            // front sight
      out.set(0, -2, 0, GUN_DARK);                           // trigger guard
      return [barrel + 3, 0, 0];
    },
    derringer(out, tint, barrel) {
      out.box(-1, -2, 0, 0, -1, 0, "#3a2a20");
      out.box(0, 0, 0, barrel + 1, 0, 0, tint);
      out.box(0, 1, 0, barrel + 1, 1, 0, shade(tint, -30));
      return [barrel + 2, 0, 0];
    },
    rifle(out, tint, barrel) {
      out.box(-7, -1, 0, -3, 0, 0, tint);                    // butt stock
      out.box(-8, -2, 0, -7, 0, 0, tint);
      out.box(-3, 0, 0, barrel, 0, 0, GUN_DARK);             // barrel
      out.box(-3, -1, 0, 1, -1, 0, shade(tint, 20));         // fore-end
      out.box(-3, -2, 0, -2, -2, 0, GUN_DARK);               // lever loop
      out.set(-2, 1, 0, GUN_DARK);                           // hammer
      out.set(barrel, 1, 0, shade(GUN_DARK, 55));            // bead
      return [barrel + 1, 0, 0];
    },
    shotgun(out, tint, barrel) {
      out.box(-7, -1, 0, -3, 0, 0, tint);
      out.box(-8, -2, 0, -7, 0, 0, tint);
      out.box(-3, 0, -1, barrel, 0, -1, GUN_DARK);           // side-by-side
      out.box(-3, 0, 1, barrel, 0, 1, GUN_DARK);
      out.box(-3, -1, -1, 0, -1, 1, shade(tint, 20));
      out.set(-2, 1, 0, GUN_DARK);
      return [barrel + 1, 0, 0];
    },
    gatling(out, tint, barrel) {
      for (const z of [-1, 0, 1]) out.box(2, 0, z, barrel, 0, z, tint);
      out.box(2, 1, 0, barrel - 1, 1, 0, shade(tint, 25));   // top barrel
      out.box(0, -1, -1, 2, 1, 1, shade(tint, -25));         // breech
      out.box(-2, 0, 0, 0, 0, 0, WOOD);                      // handle
      out.box(-1, 1, 2, 0, 2, 2, BRASS);                     // crank
      out.box(0, 2, -2, 1, 3, -2, shade(tint, 15));          // hopper
      return [barrel + 1, 0, 0];
    },
    knife(out, tint, barrel) {
      out.box(-3, 0, 0, -1, 0, 0, WOOD);                     // handle
      out.box(-1, 1, 0, -1, -1, 0, BRASS);                   // guard
      out.box(0, 0, 0, barrel, 0, 0, tint);
      out.box(0, 1, 0, barrel - 2, 1, 0, shade(tint, 35));   // edge
      return [barrel + 1, 0, 0];
    },
    bow(out, tint, barrel) {
      for (let y = -barrel; y <= barrel; y += 1) {
        out.set(Math.round(Math.cos((y / barrel) * 1.2) * 2) - 2, y, 0, tint);
      }
      for (let y = -barrel + 1; y <= barrel - 1; y += 1) out.set(-2, y, 0, "#e8d7af");
      out.box(-1, 0, 0, barrel, 0, 0, WOOD);                 // nocked arrow
      out.set(barrel + 1, 0, 0, "#b9bec4");
      return [barrel + 2, 0, 0];
    },
  };

  function buildHeldWeapon(cardKey) {
    const spec = HELD_WEAPONS[cardKey];
    if (!spec) return null;
    const out = grid();
    const muzzle = WEAPON_BUILDERS[spec.shape](out, spec.tint, spec.barrel);
    const voxels = out.list();
    return { key: cardKey, shape: spec.shape, tint: spec.tint, voxels, muzzle, bounds: bounds(voxels) };
  }

  /* ---------- equipment that stays on the character ----------

     A blue card is not an event, it is a thing that is now true about that
     character until somebody takes it away, and the battlefield should say so
     without being asked: the Barrel is standing beside them, the Dynamite is
     strapped to their back with the fuse lit, the Rattlesnake is coiled in the
     dust at their feet. Each one names where it hangs, and the stage resolves
     the mount against the figure it belongs to.

     `mount`  ground | crown | head | torso | back | belt | side
     `origin` feet — the model stands on its mount; hung — it dangles from it
     `motion` how it behaves while it sits there (see the stage) */
  const ATTACHMENTS = {
    /* The four things that stand in the dust each get their own corner of the
       character's patch. Two of them beside a figure that is only ten voxels
       across will otherwise stand inside each other, and inside the boots. */
    barrel: { shape: "barrel_stand", tint: "#7d5630", mount: "ground", offset: [13, 0, 2], scale: 1.05 },
    hat: { shape: "spare_hat", tint: "#5d3f28", mount: "crown", offset: [0, 2, 0], scale: 0.9, motion: "wobble" },
    scope: { shape: "spyglass", tint: "#4a5560", mount: "head", offset: [4, 0, 3], scale: 0.85, motion: "gleam" },
    gun_belt: {
      shape: "bandolier", tint: LEATHER, mount: "torso", offset: [0, 4, 0],
      scale: 1, origin: "hung",
    },
    bank: { shape: "strongbox", tint: "#4f5560", mount: "ground", offset: [-13, 0, 2], scale: 1, motion: "glint" },
    clock: {
      shape: "pocketwatch", tint: BRASS, mount: "side", offset: [5, 5, 4],
      scale: 0.8, origin: "hung", motion: "swing",
    },
    dynamite: { shape: "bundle", tint: "#a5352b", mount: "back", offset: [0, 0, -3], scale: 1, motion: "spark" },
    rattlesnake: { shape: "coiled", tint: "#7fbe5e", mount: "ground", offset: [6, 0, 9], scale: 1, motion: "coil" },
    stampede: { shape: "steer_skull", tint: "#e0d6bc", mount: "ground", offset: [-7, 0, 9], scale: 1, motion: "sway" },
  };

  const ATTACHMENT_BUILDERS = {
    barrel_stand(out, tint) {
      const hoop = "#4a4038";
      out.box(-2, 0, -1, 2, 6, 1, tint);
      out.box(-1, 0, -2, 1, 6, 2, tint);
      out.box(-2, 1, -1, 2, 1, 1, hoop);
      out.box(-1, 1, -2, 1, 1, 2, hoop);
      out.box(-2, 5, -1, 2, 5, 1, hoop);
      out.box(-1, 5, -2, 1, 5, 2, hoop);
      out.box(-1, 7, -1, 1, 7, 1, shade(tint, 25));          // lid
    },
    spare_hat(out, tint) {
      out.box(-4, 0, -3, 4, 0, 3, tint);
      out.box(-2, 1, -2, 2, 2, 2, tint);
      out.box(-2, 1, -2, 2, 1, 2, shade(tint, -35));         // band
    },
    spyglass(out, tint) {
      out.box(0, 0, 0, 4, 0, 0, tint);
      out.box(4, 0, 0, 6, 0, 0, shade(tint, 25));
      out.set(7, 0, 0, "#8fc7e0");                            // lens
      out.set(-1, 0, 0, "#1d1a19");                           // eyepiece
      out.set(2, 1, 0, BRASS);
    },
    bandolier(out, tint) {
      // a cartridge belt worn across the chest, brass showing all the way down
      for (let step = 0; step <= 8; step += 1) {
        const x = -4 + step;
        const y = 4 - step;
        out.set(x, y, 3, tint);
        if (step % 2 === 0) out.set(x, y - 1, 3, BRASS);
      }
      out.box(-5, -1, 2, -5, 1, 3, tint);
    },
    strongbox(out, tint) {
      out.box(-3, 0, -2, 3, 4, 2, tint);
      out.box(-3, 5, -2, 3, 5, 2, shade(tint, -25));
      out.box(-2, 1, 3, 2, 4, 3, shade(tint, 30));           // door
      out.set(0, 2, 4, BRASS);                                // dial
      out.box(-3, 2, -3, 3, 2, -3, BRASS);
    },
    // Hung from a fob at the top rather than stood on the ground, so the
    // swing happens where the chain is rather than around the watch.
    pocketwatch(out, tint) {
      out.box(0, -1, 0, 0, -4, 0, shade(tint, -30));          // chain
      out.box(-1, -5, 0, 1, -7, 0, tint);                     // case
      out.set(0, -6, 1, "#e8d7af");                           // face
    },
    bundle(out, tint) {
      for (const x of [-1, 0, 1]) out.box(x, 0, 0, x, 4, 0, x === 0 ? shade(tint, 20) : tint);
      out.box(-1, 2, 0, 1, 2, 0, "#e8d7af");                  // wrap
      out.box(0, 5, 0, 0, 7, 0, "#8a7a5a");                   // fuse
    },
    coiled(out, tint) {
      const dark = shade(tint, -40);
      for (let step = 0; step < 22; step += 1) {
        const angle = step * 0.62;
        const radius = 4 - step * 0.12;
        out.set(Math.round(Math.cos(angle) * radius), Math.floor(step / 8),
                Math.round(Math.sin(angle) * radius), step % 2 ? tint : dark);
      }
      out.box(0, 3, 0, 0, 5, 0, tint);                        // reared head
      out.set(0, 5, 1, "#e2553b");                            // tongue
      out.set(1, 5, 0, "#241a14");
    },
    steer_skull(out, tint) {
      out.box(-1, 1, -1, 1, 4, 1, tint);
      out.box(-2, 4, 0, 2, 4, 0, tint);
      out.box(-4, 5, 0, -2, 6, 0, shade(tint, -20));          // horns
      out.box(2, 5, 0, 4, 6, 0, shade(tint, -20));
      out.set(-1, 3, 2, "#241a14");
      out.set(1, 3, 2, "#241a14");
    },
  };

  function buildAttachment(cardKey) {
    const spec = ATTACHMENTS[cardKey];
    if (!spec) return null;
    const out = grid();
    ATTACHMENT_BUILDERS[spec.shape](out, spec.tint);
    const voxels = out.list();
    return {
      key: cardKey, shape: spec.shape, tint: spec.tint, mount: spec.mount,
      offset: spec.offset, scale: spec.scale ?? 1, motion: spec.motion || null,
      origin: spec.origin || "feet", voxels, bounds: bounds(voxels),
    };
  }

  return {
    CHARACTERS,
    CARD_PROPS,
    HELD_WEAPONS,
    ATTACHMENTS,
    DEFAULT_CHARACTER,
    HEIGHTS,
    ARM_SIDE,
    buildFigure,
    buildProp,
    buildHeldWeapon,
    buildAttachment,
    traitsFor,
    bounds,
    shade,
  };
});
