import * as THREE from "./vendor/three.module.min.js";

/* --------------------------------------------------------------------------
   The battlefield in the middle of the table.

   Everything the duel is actually about happens here: the four characters in
   play stand as voxel figures on a patch of frontier dust, facing off across
   it, and every card that resolves throws something physical between them —
   a bullet, a stick of dynamite, a beer. The cards themselves stay on the
   2D board around the stage; this is where they land.

   The stage owns its own clock. The board hands it moments ("this character
   is hit 640ms from now") on the same schedule that drives the 2D ceremonies
   and the sound, so the shot cracks, the card flies and the figure staggers
   on one beat.
   -------------------------------------------------------------------------- */

const models = globalThis.BangVoxelModels;

const VOXEL = 1;                 // one voxel is one world unit
const GROUND = 0;                // figures stand on y = 0

/* Where the four seats sit.

   The stage is a letterbox strip across the middle of the board, so the duel
   is staged the way a duel actually looks: the two sides face each other
   across the width of the screen rather than into it. The active character of
   each side stands forward, the rearguard behind and off to the outside, and
   both are close enough together to read as one side.

   `facing` turns the figure to look across the field at its opposite number,
   but only about two thirds of the way: a figure square-on to its enemy is
   edge-on to the camera, and you would never see whose face it is. */
const THREE_QUARTER = 0.62;
const SEATS = {
  opponent_active: { x: -17, z: 12, facing: Math.PI / 2 - THREE_QUARTER },
  opponent_rearguard: { x: -40, z: -14, facing: Math.PI / 2 - THREE_QUARTER },
  own_active: { x: 17, z: 12, facing: -Math.PI / 2 + THREE_QUARTER },
  own_rearguard: { x: 40, z: -14, facing: -Math.PI / 2 + THREE_QUARTER },
};

/* The middle of the field. Both shots look here and both fall back to here
   when nothing more interesting is happening. */
const CENTRE = new THREE.Vector3(0, 8, 0);

/* Two shots, picked by the shape of the hole the board gives the stage.

   WIDE is the letterbox strip a desktop board leaves: an almost side-on view,
   a long lens, and the two sides facing off across the screen.

   TALL is for a phone held upright, where that strip becomes a nearly square
   window. A side-on shot there has to pull back so far that the duel is a
   diorama on a shelf, so the camera moves round to your own end of the field
   and looks down the line of it instead: your character in the foreground,
   the opponent facing you from the far end. It is the same arrangement the
   phone's own board uses — them at the top, you at the bottom — said in three
   dimensions, and it wants a wide lens so the near end reads as near.

   Each shot carries the box it has to fit, because they are not the same box:
   the tall one deliberately stops short of your own rearguard on the near
   side. Framing all of it would push the camera back behind that figure, and
   an over-the-shoulder shot is supposed to have a shoulder in it. */
const SHOTS = {
  wide: {
    view: new THREE.Vector3(0.16, 0.34, 0.93).normalize(),
    fov: 30,
    min: new THREE.Vector3(-47, -1, -22),
    max: new THREE.Vector3(47, 19, 22),
    pad: 1.05,
  },
  tall: {
    view: new THREE.Vector3(0.72, 0.62, 0.31).normalize(),
    fov: 50,
    min: new THREE.Vector3(-46, -1, -14),
    max: new THREE.Vector3(24, 18, 14),
    pad: 1.02,
  },
};
const TALL_BELOW = 1.5;   // container aspect under which the tall shot is used

/* How close the camera may get on the strongest shot there is, as a fraction
   of the framing distance. The stage is a letterbox strip, so the height is
   what runs out first: anything much tighter than this and the closest
   figure's hat is outside the frame. */
const CLOSE_UP = 0.66;

const TONES = {
  danger: 0xe2553b,
  blast: 0xffb347,
  heal: 0xe8bf6a,
  guard: 0x8fc7e0,
  venom: 0x7fbe5e,
  greed: 0xd9a94f,
  gear: 0xcfc1a2,
  neutral: 0xd8cbb0,
  smoke: 0x9c9186,
  brass: 0xc9a44c,
  spark: 0xffd98a,
  water: 0x5a8fa8,
  dust: 0xc2a173,
  ink: 0x8a4636,
};

/* ---------- small helpers ---------- */

const clamp01 = value => (value < 0 ? 0 : value > 1 ? 1 : value);
const easeOut = t => 1 - Math.pow(1 - t, 3);
const easeIn = t => t * t * t;
const easeInOut = t => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2);

function toneOf(name) {
  return TONES[name] ?? TONES.neutral;
}

/* --------------------------------------------------------------------------
   Voxel meshes

   A figure or a prop is one InstancedMesh: a single box geometry drawn once
   per voxel, coloured per instance. Four figures and a handful of props is a
   few draw calls, which is what lets this sit inside a page that is otherwise
   plain DOM.
   -------------------------------------------------------------------------- */

const CUBE = new THREE.BoxGeometry(VOXEL, VOXEL, VOXEL);
const colourCache = new Map();

function colourFor(hex) {
  let colour = colourCache.get(hex);
  if (!colour) {
    colour = new THREE.Color(hex);
    colourCache.set(hex, colour);
  }
  return colour;
}

/* `origin` picks where the model's own zero lands: on its feet, in its middle,
   or — for a piece that is part of a rig — exactly where the modeller put it,
   because the arm and the hand and the muzzle all have to agree about the
   grid they are in. A part built from a figure passes the whole figure's
   `offset` so that taking the arm off never shifts the body. */
function voxelMesh(voxels, { origin = "feet", offset = null } = {}) {
  // Per-voxel colour rides on the instance colour attribute, so the material
  // stays plain white and is tinted per instance. (Asking for vertexColors
  // here instead would look for a colour attribute the box geometry has not
  // got, and every figure would come out black.)
  const material = new THREE.MeshLambertMaterial({ color: 0xffffff });
  const mesh = new THREE.InstancedMesh(CUBE, material, voxels.length);
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  const bounds = models.bounds(voxels);
  // Sit the model on its own feet and centre it left-to-right, so the stage
  // can place it by its seat and spin it about its own spine.
  const [offsetX, offsetY, offsetZ] = offset || [
    -(bounds.min[0] + bounds.max[0]) / 2,
    origin === "feet" ? -bounds.min[1] : -(bounds.min[1] + bounds.max[1]) / 2,
    -(bounds.min[2] + bounds.max[2]) / 2,
  ];
  const matrix = new THREE.Matrix4();
  voxels.forEach((voxel, index) => {
    matrix.makeTranslation(voxel.x + offsetX, voxel.y + offsetY, voxel.z + offsetZ);
    mesh.setMatrixAt(index, matrix);
    mesh.setColorAt(index, colourFor(voxel.color));
  });
  mesh.instanceMatrix.needsUpdate = true;
  if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
  mesh.userData.bounds = bounds;
  mesh.userData.offset = [offsetX, offsetY, offsetZ];
  return mesh;
}

/* Turning a weapon to face the way the hand is facing.

   Weapons are modelled in the gun's own frame — barrel along +x, sights along
   +y — because that is the only frame in which "the muzzle is four voxels
   along" means anything. The hand hangs off the bottom of an arm that hangs
   straight down, so the barrel has to run down -y and the sights forward, and
   when the arm swings up to the horizontal the gun comes level with it. This
   is that change of basis, once, shared by every weapon. */
const WEAPON_BASIS = new THREE.Matrix4().makeBasis(
  new THREE.Vector3(0, -1, 0),
  new THREE.Vector3(0, 0, 1),
  new THREE.Vector3(-1, 0, 0),
);

/* Weapons are modelled at whatever size reads on their own, which is nothing
   like the size they are in somebody's fist: a figure is twenty-three voxels
   tall, so a revolver is about three of them from grip to muzzle and a rifle
   about eight. Chunky beats accurate at this scale, but only up to the point
   where a Colt hangs past a man's boot. */
const WEAPON_SCALE = {
  revolver: 0.5, derringer: 0.55, rifle: 0.64, shotgun: 0.64,
  gatling: 0.7, knife: 0.6, bow: 0.8,
};

/* A weapon on a hand: the voxels, turned into the hand's frame, with a marker
   sitting exactly where the bullet leaves so shots start at the muzzle rather
   than somewhere near the shooter. */
function heldWeaponMesh(cardKey) {
  const built = models.buildHeldWeapon(cardKey);
  if (!built) return null;
  const mesh = voxelMesh(built.voxels, { offset: [0, 0, 0] });
  mesh.setRotationFromMatrix(WEAPON_BASIS);
  mesh.scale.setScalar(WEAPON_SCALE[built.shape] ?? 0.85);
  const muzzle = new THREE.Object3D();
  muzzle.position.set(built.muzzle[0], built.muzzle[1], built.muzzle[2]);
  mesh.add(muzzle);
  mesh.userData.muzzle = muzzle;
  mesh.userData.shape = built.shape;
  return mesh;
}

/* --------------------------------------------------------------------------
   A character on the stage
   -------------------------------------------------------------------------- */

/* Where a piece of equipment hangs off a character, in the figure's own voxel
   grid. The figure is drawn centred on its spine and standing on zero, so an
   x of nought is the middle of the character and a y is a height off the
   ground straight out of the model's own table of heights. */
const H = models.HEIGHTS;

function mountPoint(figure, mount) {
  switch (mount) {
    case "ground": return [0, 0, 0];
    case "crown": return [0, figure.top, 0];
    case "head": return [0, H.crown - 1, 0];
    case "torso": return [0, H.torso, 0];
    case "back": return [0, H.torso + 2, 0];
    case "belt": return [0, H.belt, 0];
    case "side": return [0, H.belt + 2, 0];
    default: return [0, H.torso, 0];
  }
}

class Figure {
  constructor(cid, key, seat) {
    this.cid = cid;
    this.key = key;
    this.seat = seat;
    this.group = new THREE.Group();
    this.body = new THREE.Group();
    this.group.add(this.body);
    const built = models.buildFigure(key);
    this.built = built;
    /* The body and the gun arm are two meshes of one figure, so they have to
       be placed by the *figure's* offsets rather than each finding its own
       middle — an arm that centred itself would hang off the wrong shoulder. */
    const bounds = built.bounds;
    this.offset = [
      -(bounds.min[0] + bounds.max[0]) / 2,
      -bounds.min[1],
      -(bounds.min[2] + bounds.max[2]) / 2,
    ];
    this.mesh = voxelMesh(built.body, { offset: this.offset });
    this.height = bounds.size[1];
    this.top = bounds.max[1] + this.offset[1];
    this.body.add(this.mesh);

    /* The gun arm, on its hinge at the shoulder. It hangs straight down at
       rest and swings up to point at whoever is being shot at. */
    this.armPivot = new THREE.Group();
    /* Yaw outside pitch. The arm hangs straight down, so it is swung up to
       the horizon first and then turned to face the target; done the other
       way round — which is the default order — turning a downward-pointing
       arm about the vertical does nothing at all, and the raise that follows
       sends it off in whatever direction it happened to start in. */
    this.armPivot.rotation.order = "YXZ";
    this.armPivot.position.set(
      built.arm.pivot[0] + this.offset[0],
      built.arm.pivot[1] + this.offset[1],
      built.arm.pivot[2] + this.offset[2],
    );
    this.armPivot.add(voxelMesh(built.arm.voxels, { offset: [0, 0, 0] }));
    this.hand = new THREE.Group();
    this.hand.position.set(built.arm.hand[0], built.arm.hand[1], built.arm.hand[2]);
    this.armPivot.add(this.hand);
    this.body.add(this.armPivot);

    this.idleWeapon = built.idleWeapon;
    this.held = null;
    this.hold(this.idleWeapon);

    // The aim rig: where the arm is pointing now, and where it is being asked
    // to point. Both are eased every frame so a draw is a movement, not a cut.
    this.aim = 0;
    this.aimWant = 0;
    this.aimPitch = 0;
    this.aimYaw = 0;
    this.kickBack = 0;

    this.attachments = new Map();   // card key → { mesh, spec, spark }
    this.phase = Math.random() * Math.PI * 2;   // so figures do not breathe in lockstep
    this.swayPhase = Math.random() * Math.PI * 2;
    this.pose = { recoil: 0, stagger: 0, lift: 0, tilt: 0, sink: 0, spin: 0, turn: 0 };
    this.dead = false;
    this.active = false;
    this.placeAt(seat);
  }

  /* ---------- what is in the hand ---------- */

  hold(cardKey) {
    if (this.held?.key === cardKey) return;
    this.dropHeld();
    if (!cardKey) return;
    const mesh = heldWeaponMesh(cardKey);
    if (!mesh) return;
    this.hand.add(mesh);
    this.held = { key: cardKey, mesh, muzzle: mesh.userData.muzzle, shape: mesh.userData.shape };
  }

  dropHeld() {
    if (!this.held) return;
    this.held.mesh.removeFromParent();
    this.held.mesh.material?.dispose?.();
    this.held = null;
  }

  // Back to whatever this character carries when nothing is happening.
  holster() {
    this.hold(this.idleWeapon);
  }

  /* Where the shot comes from. Falls back to the middle of the chest for a
     character with empty hands, which is where a thrown thing leaves from. */
  muzzlePoint(into = new THREE.Vector3()) {
    if (this.held?.muzzle) {
      this.group.updateMatrixWorld(true);
      return this.held.muzzle.getWorldPosition(into);
    }
    return into.copy(this.worldPoint(0.62));
  }

  /* Point the gun arm at a place on the field. Everything is worked out in the
     figure's own frame, so it holds however the figure is turned. */
  aimAt(point) {
    const local = this.group.worldToLocal(point.clone());
    local.sub(this.armPivot.position).normalize();
    this.aimPitch = -Math.acos(Math.max(-1, Math.min(1, -local.y)));
    this.aimYaw = Math.atan2(local.x, local.z);
    this.aimWant = 1;
  }

  lowerAim() {
    this.aimWant = 0;
  }

  /* ---------- equipment that stays ---------- */

  attach(cardKey) {
    if (this.attachments.has(cardKey)) return null;
    const built = models.buildAttachment(cardKey);
    if (!built) return null;
    // Gear that stands sits on its own base; gear that hangs keeps the
    // modeller's zero, which is the point it hangs from.
    const mesh = built.origin === "hung"
      ? voxelMesh(built.voxels, { offset: [0, 0, 0] })
      : voxelMesh(built.voxels, { origin: "feet" });
    mesh.scale.setScalar(built.scale);
    const base = mountPoint(this, built.mount);
    const home = new THREE.Vector3(
      base[0] + built.offset[0] + (built.mount === "ground" ? 0 : this.offset[0]),
      base[1] + built.offset[1],
      base[2] + built.offset[2],
    );
    mesh.position.copy(home);
    // Ground gear stands in the dust and does not breathe with the character;
    // everything worn rides the body.
    (built.mount === "ground" ? this.group : this.body).add(mesh);
    const entry = { mesh, spec: built, home, phase: Math.random() * Math.PI * 2 };
    if (built.motion === "spark") {
      const spark = new THREE.Mesh(
        new THREE.BoxGeometry(1.4, 1.4, 1.4),
        new THREE.MeshBasicMaterial({ color: 0xffb347, transparent: true, opacity: 0.95 }),
      );
      spark.position.set(0, built.bounds.size[1] + 0.5, 0);
      mesh.add(spark);
      entry.spark = spark;
    }
    this.attachments.set(cardKey, entry);
    return entry;
  }

  detach(cardKey) {
    const entry = this.attachments.get(cardKey);
    if (!entry) return null;
    this.attachments.delete(cardKey);
    return entry;
  }

  attachmentPoint(cardKey, into = new THREE.Vector3()) {
    const entry = this.attachments.get(cardKey);
    if (!entry) return into.copy(this.worldPoint(0.5));
    this.group.updateMatrixWorld(true);
    return entry.mesh.getWorldPosition(into);
  }

  placeAt(seat) {
    this.seat = seat;
    const spot = SEATS[seat] || SEATS.own_active;
    this.group.position.set(spot.x, GROUND, spot.z);
    this.group.rotation.y = spot.facing;
  }

  /* Called every frame: idle breathing, a slow weight-shift so nobody stands
     to attention, plus whatever pose the effects set. The active character
     breathes a little harder — the stage says who is up without a label. */
  update(time, delta = 1 / 60) {
    const alive = !this.dead;
    const beat = this.active ? 2.3 : 1.6;
    const breathe = alive ? Math.sin(time * beat + this.phase) * (this.active ? 0.34 : 0.22) : 0;
    const sway = alive ? Math.sin(time * 0.63 + this.swayPhase) : 0;
    const pose = this.pose;
    this.body.position.set(
      pose.stagger + sway * 0.28,
      GROUND + breathe + pose.lift - pose.sink,
      pose.recoil,
    );
    this.body.rotation.x = pose.tilt;
    this.body.rotation.z = pose.spin + (alive ? sway * 0.018 : 0);
    this.body.rotation.y = pose.turn + (alive ? sway * 0.05 : 0);
    this.updateArm(time, delta);
    this.updateAttachments(time);
  }

  /* The arm, eased toward wherever it has been asked to point. A raise takes
     about a sixth of a second whatever the frame rate is doing, and the
     recoil from a shot is a separate kick that decays on top of the aim, so
     firing three times while still aiming reads as three shots rather than
     three re-draws. */
  updateArm(time, delta) {
    const blend = 1 - Math.exp(-delta / 0.055);
    this.aim += (this.aimWant - this.aim) * blend;
    this.kickBack *= Math.exp(-delta / 0.07);
    const hang = this.dead ? 0 : Math.sin(time * 0.63 + this.swayPhase) * 0.04;
    this.armPivot.rotation.x = this.aimPitch * this.aim + this.kickBack + hang * (1 - this.aim);
    this.armPivot.rotation.y = this.aimYaw * this.aim;
    this.armPivot.rotation.z = this.aim * -0.08;
  }

  /* Equipment is never still. A fuse sparks, a pocket watch swings, a snake
     breathes in its coil — so that a blue card on the board reads as a live
     thing on the field rather than scenery someone left there. */
  updateAttachments(time) {
    for (const entry of this.attachments.values()) {
      const { mesh, home, phase } = entry;
      const t = time + phase;
      switch (entry.spec.motion) {
        case "wobble":
          mesh.rotation.z = Math.sin(t * 1.1) * 0.05;
          mesh.position.y = home.y + Math.sin(t * 1.7) * 0.18;
          break;
        case "swing":
          mesh.rotation.z = Math.sin(t * 2.1) * 0.5;
          break;
        case "spark": {
          const flare = 0.5 + Math.abs(Math.sin(t * 9)) * 0.9;
          if (entry.spark) {
            entry.spark.scale.setScalar(flare);
            entry.spark.material.opacity = 0.5 + flare * 0.4;
          }
          break;
        }
        case "coil":
          mesh.rotation.y = Math.sin(t * 0.8) * 0.35;
          mesh.scale.y = entry.spec.scale * (1 + Math.sin(t * 2.4) * 0.05);
          break;
        case "sway":
          mesh.rotation.y = Math.sin(t * 0.5) * 0.25;
          break;
        case "gleam":
          mesh.rotation.x = Math.sin(t * 0.9) * 0.12;
          break;
        case "glint":
          mesh.position.y = home.y + Math.abs(Math.sin(t * 0.7)) * 0.2;
          break;
        default:
          break;
      }
    }
  }

  worldPoint(heightFraction = 0.6) {
    const spot = SEATS[this.seat] || SEATS.own_active;
    return new THREE.Vector3(spot.x, GROUND + this.height * heightFraction, spot.z);
  }

  // Which way this figure is looking, in world space.
  forward() {
    const spot = SEATS[this.seat] || SEATS.own_active;
    return new THREE.Vector3(Math.sin(spot.facing), 0, Math.cos(spot.facing));
  }

  dispose() {
    this.dropHeld();
    for (const entry of this.attachments.values()) {
      entry.spark?.geometry.dispose();
      entry.spark?.material.dispose();
      entry.mesh.material?.dispose?.();
      entry.mesh.removeFromParent();
    }
    this.attachments.clear();
    this.mesh.material.dispose();
    this.group.traverse(child => {
      if (child.isInstancedMesh && child !== this.mesh) child.material?.dispose?.();
    });
    this.group.removeFromParent();
  }
}

/* --------------------------------------------------------------------------
   The stage
   -------------------------------------------------------------------------- */

class Stage {
  constructor(container) {
    this.container = container;
    this.figures = new Map();       // character id → Figure
    this.tweens = [];
    this.transient = [];            // meshes to bin once their tween ends
    this.clock = 0;
    this.running = false;
    this.quality = 1;
    this.build();
  }

  build() {
    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true, powerPreference: "high-performance" });
    // Phones pay for every pixel twice over; the stage is a small strip on
    // them, so cap the ratio rather than render a retina battlefield.
    const dense = window.innerWidth > 820;
    renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, dense ? 2 : 1.5));
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer = renderer;
    this.container.appendChild(renderer.domElement);

    const scene = new THREE.Scene();
    scene.fog = new THREE.Fog(0x2b1a12, 90, 210);
    this.scene = scene;

    // A flanking three-quarter view: both rows read at once, and neither side
    // is reduced to the back of its own head. The distance is worked out per
    // resize so the field fits whatever strip of the board it is given.
    // Almost side-on to the duel, lifted just enough to see the ground the
    // figures are standing on and to keep the two rows from overlapping.
    this.camera = new THREE.PerspectiveCamera(SHOTS.wide.fov, 1, 1, 700);
    this.shot = SHOTS.wide;
    this.viewDirection = SHOTS.wide.view.clone();
    this.target = CENTRE.clone();

    /* The camera rig. `focus` and `zoom` are what the camera is actually
       using this frame; the `*Want` values are where the stage has asked it
       to go, and every frame eases one toward the other. That way a kill-cam,
       a punch-in on an impact and the idle drift can all pull on the same
       camera without fighting over it. */
    this.baseDistance = 120;
    this.focus = CENTRE.clone();
    this.focusWant = CENTRE.clone();
    this.zoom = 1;
    this.zoomWant = 1;
    this.shake = 0;
    this.holdUntil = 0;          // while set, nothing may steal the framing
    this.shakeOffset = new THREE.Vector3();

    const sun = new THREE.DirectionalLight(0xffdcae, 2.1);
    sun.position.set(38, 62, 26);
    sun.castShadow = true;
    sun.shadow.mapSize.set(1024, 1024);
    const reach = 70;
    Object.assign(sun.shadow.camera, { left: -reach, right: reach, top: reach, bottom: -reach, near: 1, far: 200 });
    sun.shadow.camera.updateProjectionMatrix();
    sun.shadow.bias = -0.0015;
    scene.add(sun);
    scene.add(new THREE.HemisphereLight(0xffd9a0, 0x6b4a30, 1.15));
    const rim = new THREE.DirectionalLight(0x7aa6cf, 0.55);
    rim.position.set(-40, 26, -44);
    scene.add(rim);

    this.buildGround();
    this.buildDressing();
    this.buildAtmosphere();
    this.buildSpotlights();
  }

  buildGround() {
    // Big enough that its edge is always beyond the far clip and the fog, so
    // the dust reads as open country rather than a rug on a dark floor.
    const ground = new THREE.Mesh(
      new THREE.PlaneGeometry(900, 900),
      new THREE.MeshLambertMaterial({ color: 0xb08a5a }),
    );
    ground.rotation.x = -Math.PI / 2;
    ground.receiveShadow = true;
    this.scene.add(ground);

    // A scuffed patch of dirt between the two sides — the ground the duel is
    // actually fought over.
    const arena = new THREE.Mesh(
      new THREE.CircleGeometry(52, 44),
      new THREE.MeshLambertMaterial({ color: 0x96703f, transparent: true, opacity: 0.85 }),
    );
    arena.rotation.x = -Math.PI / 2;
    arena.position.y = 0.05;
    arena.receiveShadow = true;
    arena.scale.set(1.5, 1, 0.8);
    this.scene.add(arena);
  }

  // Barrels, crates and a fence at the edges, so the field has a horizon and
  // the figures have something to be standing in front of.
  buildDressing() {
    const dressing = new THREE.Group();
    const place = (key, x, z, rotation = 0, scale = 1) => {
      const built = models.buildProp(key);
      const mesh = voxelMesh(built.voxels);
      mesh.position.set(x, GROUND, z);
      mesh.rotation.y = rotation;
      mesh.scale.setScalar(scale);
      dressing.add(mesh);
    };
    // Set back behind the duel: a horizon for the figures to stand against,
    // never something for the camera to look round.
    place("barrel", -52, -34, 0.4, 1.2);
    place("barrel", -46, -39, -0.2, 1.0);
    place("wagon", 8, -46, -0.35, 1.6);
    place("bank", 52, -36, 0.5, 1.4);
    place("barrel", 34, -40, 0.9, 1.0);
    place("barrel", -12, -42, 0.2, 0.9);
    /* The far end of the street. Nothing on a desktop board ever looks this
       way, but the portrait camera stands at your own end and stares straight
       down the line — without these the opponent has nothing behind them but
       fog. */
    // Kept on the same back line as the rest so the letterbox view still
    // reads as one horizon, far enough out that neither shot has to look
    // round them.
    place("bank", -98, -34, -1.5, 1.6);
    place("wagon", -88, -48, 1.4, 1.4);
    place("barrel", -80, -30, 0.6, 1.1);
    this.scene.add(dressing);
    this.dressing = dressing;
  }

  /* Dust hanging in the air over the field. It never reacts to anything — it
     is there so the stage is moving even on a turn where nothing happens, and
     so the light has something to catch. One instanced mesh, updated on a
     wrap-around drift, costs a single draw call. */
  buildAtmosphere() {
    const count = 90;
    const mesh = new THREE.InstancedMesh(
      CUBE,
      new THREE.MeshBasicMaterial({ color: 0xd8bb8a, transparent: true, opacity: 0.32, depthWrite: false }),
      count,
    );
    mesh.frustumCulled = false;
    this.motesInAir = Array.from({ length: count }, () => ({
      x: (Math.random() - 0.5) * 130,
      y: 1 + Math.random() * 34,
      z: (Math.random() - 0.5) * 90,
      drift: 1.2 + Math.random() * 2.6,
      bob: Math.random() * Math.PI * 2,
      scale: 0.16 + Math.random() * 0.4,
    }));
    this.scene.add(mesh);
    this.airMesh = mesh;
  }

  /* A ring of light on the dust under each active character. It is the same
     information the ★ on the card carries, said in the language of the
     battlefield — you can tell who is up without leaving the stage. */
  buildSpotlights() {
    this.spotlights = new Map();
    for (const zone of ["own_active", "opponent_active"]) {
      const spot = SEATS[zone];
      const mesh = new THREE.Mesh(
        new THREE.RingGeometry(5.6, 7.4, 34),
        new THREE.MeshBasicMaterial({
          color: 0xe8bf6a, transparent: true, opacity: 0, side: THREE.DoubleSide, depthWrite: false,
        }),
      );
      mesh.rotation.x = -Math.PI / 2;
      mesh.position.set(spot.x, GROUND + 0.12, spot.z);
      this.scene.add(mesh);
      this.spotlights.set(zone, mesh);
    }
  }

  /* ---------- lifecycle ---------- */

  /* Frame the field for whatever shape the board hands over: back off far
     enough that the field box fits across and up and down, and no further.
     Whichever axis needs more distance wins, so a tall narrow board pulls
     back for the width and a wide strip pulls back for the height. */
  resize() {
    const width = this.container.clientWidth;
    const height = this.container.clientHeight;
    if (!width || !height) return;
    this.renderer.setSize(width, height, false);
    const aspect = width / height;
    const shot = aspect < TALL_BELOW ? SHOTS.tall : SHOTS.wide;
    this.shot = shot;
    this.camera.aspect = aspect;
    this.camera.fov = shot.fov;
    this.viewDirection.copy(shot.view);
    this.baseDistance = this.fitDistance(aspect) * shot.pad;
    this.camera.updateProjectionMatrix();
    this.updateCamera(0);
  }

  /* How far back the camera has to sit for the whole field box to be inside
     the frustum, for the view angle it is currently using.

     Worked out per corner rather than from a width and a height, because the
     two shots point at the field along completely different axes: the same
     box that is 94 units wide across the letterbox view is 94 units *deep*
     down the portrait one, and depth costs the frame nothing. Sliding the
     camera along its own axis moves every corner's depth by the same amount,
     so each corner gives one lower bound on the distance and the answer is
     the largest of them. */
  fitDistance(aspect) {
    const shot = this.shot;
    const halfVertical = THREE.MathUtils.degToRad(shot.fov) / 2;
    const tanVertical = Math.tan(halfVertical);
    const tanHorizontal = tanVertical * aspect;
    const axis = this.viewDirection;
    const right = new THREE.Vector3().crossVectors(new THREE.Vector3(0, 1, 0), axis).normalize();
    const up = new THREE.Vector3().crossVectors(axis, right).normalize();
    const corner = new THREE.Vector3();
    let distance = 30;
    for (const x of [shot.min.x, shot.max.x]) {
      for (const y of [shot.min.y, shot.max.y]) {
        for (const z of [shot.min.z, shot.max.z]) {
          corner.set(x, y, z).sub(CENTRE);
          const depth = corner.dot(axis);
          distance = Math.max(
            distance,
            depth + Math.abs(corner.dot(right)) / tanHorizontal,
            depth + Math.abs(corner.dot(up)) / tanVertical,
          );
        }
      }
    }
    return distance;
  }

  /* Put the camera where the rig says, this frame.

     Three things pull on it and they compose rather than override: the framing
     distance from `resize`, whatever the stage is currently looking at (the
     middle of the field, or a figure during a kill-cam), and a slow drift that
     keeps the shot from ever being a still photograph.

     `delta` is the frame's own length in seconds, and the ease is worked out
     from it rather than being a fixed step per frame: a kill-cam that holds
     for a fixed number of milliseconds would otherwise only ever arrive on a
     machine fast enough, and would push in a fraction of the way on a phone
     dropping frames. Pass 0 to snap, which is what a resize needs. */
  updateCamera(delta = 1 / 60) {
    // Exponential approach with a fixed time constant: about 95% of the way
    // there in a fifth of a second, whatever the frame rate is doing.
    const blend = delta > 0 ? 1 - Math.exp(-delta / 0.07) : 1;
    this.focus.lerp(this.focusWant, blend);
    this.zoom += (this.zoomWant - this.zoom) * blend;

    // The drift: a shallow arc around the field and a gentle rise and fall.
    const t = this.clock;
    const yaw = Math.sin(t * 0.13) * 0.075;
    const rise = Math.sin(t * 0.19 + 1.1) * 0.022;
    const direction = this.viewDirection.clone();
    const cos = Math.cos(yaw);
    const sin = Math.sin(yaw);
    direction.set(
      direction.x * cos + direction.z * sin,
      direction.y + rise,
      -direction.x * sin + direction.z * cos,
    ).normalize();

    this.camera.position.copy(direction)
      .multiplyScalar(this.baseDistance * this.zoom)
      .add(this.focus);

    // Impact shake: a decaying jitter, applied after the framing so it never
    // accumulates into a drifted camera.
    if (this.shake > 0.001) {
      this.shakeOffset.set(
        (Math.random() - 0.5) * this.shake,
        (Math.random() - 0.5) * this.shake,
        (Math.random() - 0.5) * this.shake * 0.5,
      );
      this.camera.position.add(this.shakeOffset);
      this.shake *= 0.86;
    } else {
      this.shake = 0;
    }
    this.camera.lookAt(this.focus);
  }

  /* Ask the camera to look somewhere for a while, then let it drift home.

     `strength` is the pan: how far off the middle of the field the shot moves,
     0 for not at all and 1 for dead on the point. `zoom` is the dolly, and it
     is deliberately a separate dial — most shots want to look *at* something
     without also cropping the other three figures out of the frame, and only a
     death is worth giving the rest of the board up for. A shot already holding
     at an equal or higher priority keeps the camera. */
  focusOn(point, { strength = 0.5, zoom = 1, hold = 700, priority = 0 } = {}) {
    const now = performance.now();
    if (now < this.holdUntil && priority <= (this.holdPriority || 0)) return;
    this.holdUntil = now + hold;
    this.holdPriority = priority;
    this.focusWant.copy(CENTRE).lerp(point, strength);
    this.zoomWant = Math.max(CLOSE_UP, zoom);
    this.tween({
      delay: hold, duration: 1,
      update: () => {},
      done: () => {
        if (performance.now() < this.holdUntil - 4) return;  // a later shot took over
        this.focusWant.copy(CENTRE);
        this.zoomWant = 1;
        this.holdPriority = 0;
      },
    });
  }

  kick(amount = 1.4) {
    this.shake = Math.min(6, this.shake + amount);
  }

  start() {
    if (this.running) return;
    this.running = true;
    this.last = performance.now();
    const loop = now => {
      if (!this.running) return;
      this.frame = requestAnimationFrame(loop);
      const delta = Math.min((now - this.last) / 1000, 0.05);
      this.last = now;
      this.clock += delta;
      this.frames = (this.frames || 0) + 1;
      this.step(now, delta);
      this.renderer.render(this.scene, this.camera);
    };
    this.frame = requestAnimationFrame(loop);
  }

  stop() {
    this.running = false;
    if (this.frame) cancelAnimationFrame(this.frame);
  }

  step(now, delta) {
    for (const figure of this.figures.values()) figure.update(this.clock, delta);
    this.driftAir(delta);
    this.updateCamera(delta);
    for (let index = this.tweens.length - 1; index >= 0; index -= 1) {
      const tween = this.tweens[index];
      if (now < tween.startAt) continue;
      const span = tween.duration || 1;
      const t = clamp01((now - tween.startAt) / span);
      tween.update(t);
      if (t >= 1) {
        this.tweens.splice(index, 1);
        tween.done?.();
      }
    }
  }

  /* The hanging dust, drifting one way across the field and wrapping round
     when it leaves. Nothing depends on it, so it is one matrix write per mote
     and no allocation. */
  driftAir(delta = 0) {
    const mesh = this.airMesh;
    if (!mesh || !this.motesInAir) return;
    const matrix = this._airMatrix || (this._airMatrix = new THREE.Matrix4());
    const offset = this._airOffset || (this._airOffset = new THREE.Vector3());
    const quaternion = this._airQuat || (this._airQuat = new THREE.Quaternion());
    const scale = this._airScale || (this._airScale = new THREE.Vector3());
    this.motesInAir.forEach((mote, index) => {
      mote.x += mote.drift * delta;
      if (mote.x > 65) mote.x = -65;
      offset.set(mote.x, mote.y + Math.sin(this.clock * 0.5 + mote.bob) * 1.4, mote.z);
      scale.setScalar(mote.scale);
      matrix.compose(offset, quaternion, scale);
      mesh.setMatrixAt(index, matrix);
    });
    mesh.instanceMatrix.needsUpdate = true;
  }

  tween({ delay = 0, duration = 400, update, done }) {
    this.tweens.push({ startAt: performance.now() + delay, duration, update, done });
  }

  /* ---------- transient meshes ---------- */

  spawn(mesh, lifetime) {
    this.scene.add(mesh);
    const retire = () => {
      mesh.removeFromParent();
      mesh.material?.dispose?.();
      if (mesh.geometry && mesh.geometry !== CUBE) mesh.geometry.dispose();
    };
    setTimeout(retire, lifetime);
    return mesh;
  }

  /* ---------- the cast ---------- */

  seatFor(zone) {
    return SEATS[zone] ? zone : "own_active";
  }

  figureAt(zone) {
    for (const figure of this.figures.values()) {
      if (figure.seat === zone && !figure.dead) return figure;
    }
    return null;
  }

  /* Bring the stage in line with the board: everyone seated, in the right
     seat, and nobody left standing who is no longer in play. Characters
     arriving and leaving are announced separately so they get a ceremony
     instead of appearing between frames. */
  sync(seated, dressDelay = 0) {
    const wanted = new Set();
    for (const who of seated) {
      wanted.add(who.id);
      const existing = this.figures.get(who.id);
      if (existing) {
        if (existing.seat !== who.zone) existing.placeAt(who.zone);
        continue;
      }
      const figure = new Figure(who.id, who.key, this.seatFor(who.zone));
      this.figures.set(who.id, figure);
      this.scene.add(figure.group);
    }
    for (const who of seated) {
      this.dressFigure(this.figures.get(who.id), who.equipment || [], dressDelay);
    }
    for (const [cid, figure] of [...this.figures]) {
      // A figure in the middle of falling is already off the board's books;
      // its own ceremony clears it away when it finishes going down.
      if (wanted.has(cid) || figure.dead) continue;
      figure.dispose();
      this.figures.delete(cid);
    }
    this.lightTheActive();
  }

  /* Blue cards are not events, they are facts about a character that hold
     until somebody takes them away, so they live on the figure rather than in
     a ceremony: the Barrel is standing in the dust beside them for as long as
     the board says it is there. This is the board's row of equipment and the
     figure's own kit brought into line — one arrives with a drop and a puff,
     one leaves in splinters. */
  dressFigure(figure, keys, delay = 0) {
    if (!figure) return;
    const wanted = new Set(keys.filter(key => models.ATTACHMENTS[key]));
    for (const key of [...figure.attachments.keys()]) {
      // Held back to the beat the card's own scene plays on, so a Barrel is
      // never gone from the field before the bullet it stopped gets there.
      if (!wanted.has(key)) this.at(delay, () => this.stripGear(figure, key));
    }
    for (const key of wanted) {
      if (figure.attachments.has(key)) continue;
      const entry = figure.attach(key);
      if (!entry) continue;
      // Gear that has just landed drops the last few voxels rather than
      // blinking into being.
      const mesh = entry.mesh;
      const restY = entry.home.y;
      mesh.visible = delay <= 0;
      this.tween({
        delay, duration: 340,
        update: t => {
          mesh.visible = true;
          mesh.position.y = restY + (1 - easeIn(t)) * 9;
          mesh.scale.setScalar(entry.spec.scale * (0.5 + easeOut(t) * 0.5));
        },
        done: () => {
          mesh.position.y = restY;
          mesh.scale.setScalar(entry.spec.scale);
          if (entry.spec.mount === "ground") this.dustRing(figure.attachmentPoint(key), "gear", 12);
        },
      });
    }
  }

  // Equipment being taken off, blown off or stolen: it goes in pieces.
  stripGear(figure, key) {
    const entry = figure.detach(key);
    if (!entry) return;
    const where = new THREE.Vector3();
    figure.group.updateMatrixWorld(true);
    entry.mesh.getWorldPosition(where);
    this.shardBurst(where, "gear", 12, 10);
    entry.spark?.geometry.dispose();
    entry.spark?.material.dispose();
    entry.mesh.material?.dispose?.();
    entry.mesh.removeFromParent();
  }

  // Fade the ground rings up under whichever seats are currently active.
  lightTheActive() {
    if (!this.spotlights) return;
    for (const figure of this.figures.values()) figure.active = figure.seat.endsWith("_active");
    for (const [zone, mesh] of this.spotlights) {
      const want = this.figureAt(zone) ? 0.34 : 0;
      const from = mesh.material.opacity;
      if (Math.abs(want - from) < 0.01) continue;
      this.tween({
        duration: 420,
        update: t => { mesh.material.opacity = from + (want - from) * easeOut(t); },
      });
    }
  }

  /* ---------- character moments ---------- */

  arrive(cid, delay = 0) {
    const figure = this.figures.get(cid);
    if (!figure) return;
    const drop = 46;
    figure.pose.lift = drop;
    this.tween({
      delay, duration: 520,
      update: t => { figure.pose.lift = drop * (1 - easeIn(t)); },
      done: () => {
        figure.pose.lift = 0;
        this.dustRing(figure.worldPoint(0), "gear", 18);
        this.thump(figure, 0.6);
      },
    });
  }

  /* A character's life reaching zero — the moment the 2D board topples the
     card, pops the skull and shakes the table. It is the only thing on the
     board that cannot be undone, so the stage stops doing everything else and
     watches it happen: the camera dollies in on the figure, the fall is
     stretched well past a normal pose tween, and the shot holds a beat on the
     body before drifting back to the field.

     The figure is left lying where it went down. It is still in that seat as
     far as the board is concerned, greyed out and waiting to be cleared, and
     the stage should say the same thing. `fall` takes it away later. */
  down(cid, delay = 0) {
    const figure = this.figures.get(cid);
    if (!figure || figure.dead) return;
    figure.dead = true;
    this.focusOn(figure.worldPoint(0.42), { strength: 0.8, zoom: CLOSE_UP, hold: delay + 2100, priority: 3 });
    this.tween({
      delay, duration: 900,
      update: t => {
        figure.pose.tilt = -easeIn(t) * Math.PI * 0.46;
        figure.pose.sink = easeIn(t) * 3;
        figure.pose.stagger = -easeOut(t) * 2.5;
      },
      done: () => {
        this.dustRing(figure.worldPoint(0), "gear", 26);
        this.kick(1.8);
        this.lightTheActive();
      },
    });
    this.tween({
      delay: delay + 220, duration: 300,
      update: () => {},
      done: () => this.dustRing(figure.worldPoint(0), "gear", 14),
    });
  }

  /* The seat being cleared. Usually the body is already lying there from
     `down`, and this is the table being tidied — a puff of dust and it is
     gone, no ceremony, because the ceremony already happened. A character
     that leaves a seat while still standing gets the full treatment here
     instead, since for them this *is* the moment. */
  fall(cid, key, zone, delay = 0, swept = false) {
    let figure = this.figures.get(cid);
    /* `swept` is the board's own word for "this one was already lying at zero
       life". Usually the figure knows that itself, but if the stage only
       mounted after the character went down it has no memory of it — and
       replaying a death ceremony for a body that is being carted off would be
       worse than skipping one. */
    const alreadyDown = !!figure?.dead || swept;
    if (!figure) {
      // Gone from the state before the stage heard about it: stand a
      // replacement up so the exit still happens in the right seat.
      figure = new Figure(cid, key, this.seatFor(zone));
      this.figures.set(cid, figure);
      this.scene.add(figure.group);
    }
    const clear = () => {
      this.shardBurst(figure.worldPoint(alreadyDown ? 0.15 : 0.35), "neutral", alreadyDown ? 14 : 26, 16);
      this.dustRing(figure.worldPoint(0), "gear", alreadyDown ? 18 : 26);
      figure.dispose();
      this.figures.delete(cid);
      this.lightTheActive();
    };
    if (alreadyDown) {
      this.tween({ delay, duration: 200, update: () => {}, done: clear });
      return;
    }
    figure.dead = true;
    this.focusOn(figure.worldPoint(0.42), { strength: 0.8, zoom: CLOSE_UP, hold: delay + 1800, priority: 3 });
    this.tween({
      delay, duration: 780,
      update: t => {
        figure.pose.tilt = -easeIn(t) * Math.PI * 0.48;
        figure.pose.sink = easeIn(t) * 3;
        figure.pose.stagger = -easeOut(t) * 2.5;
      },
      done: () => { this.kick(1.8); clear(); },
    });
  }

  hit(cid, delay = 0) {
    const figure = this.figures.get(cid);
    if (!figure) return;
    this.tween({
      delay, duration: 420,
      update: t => {
        const kick = Math.sin(t * Math.PI) * 1.8;
        figure.pose.stagger = kick * (figure.seat.startsWith("own") ? 1 : -1);
        figure.pose.tilt = -Math.sin(t * Math.PI) * 0.16;
      },
      done: () => { figure.pose.stagger = 0; figure.pose.tilt = 0; },
    });
    this.flash(figure.worldPoint(0.55), "danger", delay, 8);
    this.shardBurst(figure.worldPoint(0.5), "danger", 8, 9, delay);
    this.tween({ delay, duration: 1, update: () => {}, done: () => this.kick(0.9) });
  }

  heal(cid, delay = 0) {
    const figure = this.figures.get(cid);
    if (!figure) return;
    this.tween({
      delay, duration: 620,
      update: t => { figure.pose.lift = Math.sin(t * Math.PI) * 1.6; },
      done: () => { figure.pose.lift = 0; },
    });
    this.motes(figure.worldPoint(0.3), "heal", delay, 14);
  }

  recoil(figure, delay = 0) {
    if (!figure) return;
    const away = figure.seat.startsWith("own") ? 1 : -1;
    this.tween({
      delay, duration: 340,
      update: t => {
        const push = Math.sin(t * Math.PI) * 1.6;
        figure.pose.recoil = push * away;
        figure.pose.tilt = Math.sin(t * Math.PI) * 0.1 * away;
      },
      done: () => { figure.pose.recoil = 0; figure.pose.tilt = 0; },
    });
  }

  thump(figure, strength = 1) {
    if (!figure) return;
    this.tween({
      duration: 260,
      update: t => { figure.pose.sink = Math.sin(t * Math.PI) * strength; },
      done: () => { figure.pose.sink = 0; },
    });
  }

  /* ---------- effect primitives ---------- */

  flash(position, tone, delay = 0, size = 10) {
    const mesh = new THREE.Mesh(
      new THREE.SphereGeometry(size / 2, 10, 8),
      // Translucent and additive: a flare over the figure, not a balloon
      // swallowing it.
      new THREE.MeshBasicMaterial({
        color: toneOf(tone), transparent: true, opacity: 0.6,
        blending: THREE.AdditiveBlending, depthWrite: false,
      }),
    );
    mesh.position.copy(position);
    mesh.visible = false;
    this.spawn(mesh, delay + 520);
    this.tween({
      delay, duration: 300,
      update: t => {
        mesh.visible = true;
        mesh.scale.setScalar(0.3 + t * 1.5);
        mesh.material.opacity = 0.6 * (1 - t);
      },
    });
  }

  tracer(from, to, tone, delay = 0, duration = 180) {
    const direction = new THREE.Vector3().subVectors(to, from);
    const length = direction.length();
    if (length < 0.5) return;
    const mesh = new THREE.Mesh(
      new THREE.CylinderGeometry(0.32, 0.32, length, 6),
      new THREE.MeshBasicMaterial({ color: toneOf(tone), transparent: true, opacity: 0.9 }),
    );
    mesh.position.copy(from).add(direction.clone().multiplyScalar(0.5));
    mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), direction.clone().normalize());
    mesh.visible = false;
    this.spawn(mesh, delay + duration + 260);
    this.tween({
      delay, duration,
      update: t => {
        mesh.visible = true;
        mesh.scale.set(1, 1, 1);
        mesh.material.opacity = 0.9 * (1 - t);
      },
    });
  }

  // A ring of dust kicked off the ground.
  dustRing(position, tone, size = 16) {
    const mesh = new THREE.Mesh(
      new THREE.RingGeometry(size * 0.22, size * 0.3, 20),
      new THREE.MeshBasicMaterial({ color: toneOf(tone), transparent: true, opacity: 0.5, side: THREE.DoubleSide }),
    );
    mesh.rotation.x = -Math.PI / 2;
    mesh.position.set(position.x, GROUND + 0.3, position.z);
    this.spawn(mesh, 720);
    this.tween({
      duration: 620,
      update: t => {
        mesh.scale.setScalar(0.4 + easeOut(t) * 1.8);
        mesh.material.opacity = 0.5 * (1 - t);
      },
    });
  }

  /* A spray of voxel chips — splinters off a blast, dust off a body hitting
     the ground. One instanced mesh per burst, each chip on its own arc. */
  shardBurst(position, tone, count = 18, spread = 12, delay = 0) {
    const material = new THREE.MeshLambertMaterial({ color: toneOf(tone) });
    const mesh = new THREE.InstancedMesh(CUBE, material, count);
    mesh.castShadow = false;
    const chips = [];
    for (let index = 0; index < count; index += 1) {
      const angle = (index / count) * Math.PI * 2 + Math.random() * 0.6;
      const reach = spread * (0.5 + Math.random() * 0.8);
      chips.push({
        vx: Math.cos(angle) * reach,
        vz: Math.sin(angle) * reach,
        vy: spread * (0.7 + Math.random()),
        scale: 0.5 + Math.random() * 0.9,
        spin: Math.random() * 4,
      });
    }
    mesh.position.copy(position);
    this.spawn(mesh, delay + 1100);
    const matrix = new THREE.Matrix4();
    const quaternion = new THREE.Quaternion();
    const euler = new THREE.Euler();
    const scale = new THREE.Vector3();
    const offset = new THREE.Vector3();
    this.tween({
      delay, duration: 900,
      update: t => {
        chips.forEach((chip, index) => {
          const fly = t;
          offset.set(
            chip.vx * fly,
            chip.vy * fly - 34 * fly * fly,
            chip.vz * fly,
          );
          euler.set(chip.spin * fly * 4, chip.spin * fly * 3, 0);
          quaternion.setFromEuler(euler);
          scale.setScalar(chip.scale * (1 - t * 0.55));
          matrix.compose(offset, quaternion, scale);
          mesh.setMatrixAt(index, matrix);
        });
        mesh.instanceMatrix.needsUpdate = true;
      },
    });
  }

  // Slow motes drifting up: healing, smoke, anything that reads as relief.
  motes(position, tone, delay = 0, count = 12, { spread = 9, rise = 9, size = 1, life = 1100 } = {}) {
    const material = new THREE.MeshBasicMaterial({ color: toneOf(tone), transparent: true, opacity: 0.9 });
    const mesh = new THREE.InstancedMesh(CUBE, material, count);
    const seeds = Array.from({ length: count }, () => ({
      x: (Math.random() - 0.5) * spread,
      z: (Math.random() - 0.5) * spread,
      rise: rise + Math.random() * rise * 1.3,
      scale: (0.3 + Math.random() * 0.5) * size,
    }));
    mesh.position.copy(position);
    this.spawn(mesh, delay + life + 300);
    const matrix = new THREE.Matrix4();
    const quaternion = new THREE.Quaternion();
    const scale = new THREE.Vector3();
    const offset = new THREE.Vector3();
    this.tween({
      delay, duration: life,
      update: t => {
        seeds.forEach((seed, index) => {
          offset.set(seed.x * (0.4 + t), seed.rise * easeOut(t), seed.z * (0.4 + t));
          scale.setScalar(seed.scale * (1 - t * 0.7));
          matrix.compose(offset, quaternion, scale);
          mesh.setMatrixAt(index, matrix);
        });
        mesh.instanceMatrix.needsUpdate = true;
        mesh.material.opacity = 0.9 * (1 - t);
      },
    });
  }

  /* A card's prop crossing the field: it rises out of the actor, arcs over
     the dust and arrives on the target on the beat the card lands. */
  flyProp(cardKey, from, to, { delay = 0, duration = 420, spin = 1, arc = 14 } = {}) {
    const built = models.buildProp(cardKey);
    const mesh = voxelMesh(built.voxels, { origin: "centre" });
    mesh.scale.setScalar(0.9);
    mesh.position.copy(from);
    mesh.visible = false;
    this.spawn(mesh, delay + duration + 220);
    this.tween({
      delay, duration,
      update: t => {
        mesh.visible = true;
        const eased = easeInOut(t);
        mesh.position.lerpVectors(from, to, eased);
        mesh.position.y += Math.sin(t * Math.PI) * arc;
        mesh.rotation.y = t * Math.PI * 2 * spin;
        mesh.rotation.z = Math.sin(t * Math.PI) * 0.5 * spin;
      },
      done: () => { mesh.visible = false; },
    });
    return mesh;
  }

  /* A prop held up on the spot — equipment bolting on, a mug raised. */
  presentProp(cardKey, at, { delay = 0, hold = 420 } = {}) {
    const built = models.buildProp(cardKey);
    const mesh = voxelMesh(built.voxels, { origin: "centre" });
    mesh.position.copy(at);
    mesh.visible = false;
    this.spawn(mesh, delay + hold + 520);
    this.tween({
      delay, duration: hold + 400,
      update: t => {
        mesh.visible = true;
        const rise = easeOut(clamp01(t * 3));
        mesh.position.y = at.y + rise * 5;
        mesh.rotation.y = t * Math.PI * 1.4;
        mesh.scale.setScalar(0.4 + rise * 0.7);
        if (t > 0.75) mesh.scale.multiplyScalar(1 - (t - 0.75) * 2);
      },
    });
    return mesh;
  }

  /* ---------- stagecraft ----------
     The vocabulary the card scenes are written in. Everything below is a
     verb the field understands — draw and fire, throw, crawl, splash — and a
     card's scene is a handful of them on a schedule. */

  // Do something at a moment, on the stage's own clock.
  at(delay, done) {
    this.tween({ delay, duration: 1, update: () => {}, done });
  }

  /* The draw. A character raises the gun that is printed on the card, points
     it at whoever it is pointed at, and fires — so a Winchester is a rifle
     coming up to the shoulder and a Derringer is a stub out of a sleeve, and
     the flash and the tracer start at that weapon's own muzzle rather than
     somewhere near the middle of the shooter.

     Returns the moment the first round leaves the barrel, which is the beat
     everything else in the scene hangs off. */
  gunplay({ actor, to, delay = 0, cardKey }, options = {}) {
    if (!actor) return delay;
    const weapon = options.weapon || cardKey;
    const aimTime = options.aimTime ?? 190;
    const shots = options.shots ?? 1;
    const gap = options.gap ?? 90;
    const tone = options.tone || "danger";
    const size = options.flashSize ?? 9;
    const spread = options.spread ?? 0;
    const aimAt = options.aimAt || to;
    this.at(delay, () => { actor.hold(weapon); actor.aimAt(aimAt); });
    const first = delay + aimTime;
    const muzzle = new THREE.Vector3();
    for (let shot = 0; shot < shots; shot += 1) {
      const when = first + shot * gap;
      this.at(when, () => {
        actor.muzzlePoint(muzzle);
        const mark = aimAt.clone();
        if (spread) {
          mark.x += (Math.random() - 0.5) * spread;
          mark.y += (Math.random() - 0.5) * spread * 0.5;
          mark.z += (Math.random() - 0.5) * spread;
        }
        this.flash(muzzle, tone, 0, size);
        this.tracer(muzzle, mark, tone, 0, options.tracerMs ?? 140);
        this.motes(muzzle, "smoke", 0, 4, { spread: 3, rise: 4, size: 0.9, life: 620 });
        this.shardBurst(muzzle, "brass", 2, 5);
        actor.kickBack = options.kick ?? 0.5;
        this.kick(options.shake ?? 0.5);
      });
      if (shot === 0 || shots <= 3) this.recoil(actor, when);
    }
    const last = first + (shots - 1) * gap;
    this.at(last + 240, () => actor.lowerAim());
    this.at(last + 520, () => actor.holster());
    return first;
  }

  // A thing sliding along the ground rather than flying over it: a snake
  // going for an ankle, a steer running the length of the field.
  crawl(cardKey, from, to, { delay = 0, duration = 700, wiggle = 3, spin = 0, scale = 1 } = {}) {
    const built = models.buildProp(cardKey);
    const mesh = voxelMesh(built.voxels, { origin: "feet" });
    mesh.scale.setScalar(scale);
    const start = new THREE.Vector3(from.x, GROUND + 0.4, from.z);
    const end = new THREE.Vector3(to.x, GROUND + 0.4, to.z);
    const heading = Math.atan2(end.x - start.x, end.z - start.z);
    mesh.rotation.y = heading - Math.PI / 2;
    mesh.position.copy(start);
    mesh.visible = false;
    this.spawn(mesh, delay + duration + 260);
    const side = new THREE.Vector3(Math.cos(heading), 0, -Math.sin(heading));
    this.tween({
      delay, duration,
      update: t => {
        mesh.visible = true;
        mesh.position.lerpVectors(start, end, t);
        mesh.position.addScaledVector(side, Math.sin(t * Math.PI * 4) * wiggle);
        mesh.rotation.y = heading - Math.PI / 2 + Math.sin(t * Math.PI * 4) * 0.35 + t * spin;
      },
      done: () => { mesh.visible = false; },
    });
    return mesh;
  }

  /* An expanding wall of dust on the ground — a blast front, a stampede
     passing, water going everywhere. Flat and wide, where `flash` is round
     and small. */
  shockwave(position, tone, delay = 0, size = 40) {
    const mesh = new THREE.Mesh(
      new THREE.RingGeometry(size * 0.1, size * 0.16, 40),
      new THREE.MeshBasicMaterial({
        color: toneOf(tone), transparent: true, opacity: 0.55,
        side: THREE.DoubleSide, depthWrite: false,
      }),
    );
    mesh.rotation.x = -Math.PI / 2;
    mesh.position.set(position.x, GROUND + 0.35, position.z);
    mesh.visible = false;
    this.spawn(mesh, delay + 900);
    this.tween({
      delay, duration: 760,
      update: t => {
        mesh.visible = true;
        mesh.scale.setScalar(0.3 + easeOut(t) * 5.2);
        mesh.material.opacity = 0.55 * (1 - t);
      },
    });
  }

  /* A single card tumbling between two places. Drawn, dropped, stolen, dealt
     — a great deal of this game is cards changing hands, and on the field
     that has to be something you can watch happen. */
  cardFly(from, to, { delay = 0, duration = 420, arc = 10 } = {}) {
    return this.flyProp("loose_card", from, to, { delay, duration, spin: 1.5, arc });
  }

  // A ring drawn on the air rather than the ground: a sight picture, a tick
  // of a clock, a moment being marked out.
  reticle(position, tone, delay = 0, size = 9) {
    const mesh = new THREE.Mesh(
      new THREE.RingGeometry(size * 0.8, size, 28),
      new THREE.MeshBasicMaterial({
        color: toneOf(tone), transparent: true, opacity: 0.85,
        side: THREE.DoubleSide, depthWrite: false, blending: THREE.AdditiveBlending,
      }),
    );
    mesh.position.copy(position);
    mesh.visible = false;
    this.spawn(mesh, delay + 900);
    this.tween({
      delay, duration: 700,
      update: t => {
        mesh.visible = true;
        mesh.lookAt(this.camera.position);
        mesh.scale.setScalar(1.9 - easeOut(t) * 1.1);
        mesh.material.opacity = 0.85 * (1 - t * t);
      },
    });
  }

  // Everyone on the field feeling the same thing at once.
  jolt(strength = 0.6, delay = 0) {
    for (const figure of this.figures.values()) {
      this.at(delay, () => this.thump(figure, strength));
    }
  }

  // The seat opposite whoever is acting: who a shot fired back would hit.
  opposite(figure) {
    if (!figure) return null;
    return this.figureAt(figure.seat.startsWith("own") ? "opponent_active" : "own_active");
  }

  // Everyone except the character doing the thing — what a Gatling sprays and
  // what an Indians! volley falls on.
  bystanders(actor) {
    return [...this.figures.values()].filter(figure => figure !== actor && !figure.dead);
  }

  /* ---------- the motifs, staged in three dimensions ----------
     Same vocabulary the 2D ceremonies use, so a card behaves the same way in
     both places: a gunshot is a shot here too, Dynamite still shatters. */

  motifCeremony(motif, { actor, target, from, to, cardKey, delay = 0 }) {
    switch (motif) {
      case "gunshot":
        this.recoil(actor, delay);
        this.flash(from, "danger", delay, 9);
        this.tracer(from, to, "danger", delay + 60, 160);
        this.flyProp(cardKey, from, to, { delay, duration: 260, spin: 0.5, arc: 5 });
        if (target) this.hit(target.cid, delay + 220);
        break;
      case "barrage":
        this.recoil(actor, delay);
        for (let shot = 0; shot < 4; shot += 1) {
          this.flash(from, "danger", delay + shot * 70, 8);
          this.tracer(from, to, "danger", delay + shot * 70, 140);
        }
        this.flyProp(cardKey, from, to, { delay, duration: 320, spin: 1, arc: 8 });
        if (target) this.hit(target.cid, delay + 260);
        this.shardBurst(to, "danger", 16, 12, delay + 260);
        break;
      case "standoff": {
        this.flyProp(cardKey, from, to, { delay, duration: 380, spin: 2, arc: 16 });
        this.flash(from, "danger", delay + 380, 9);
        this.flash(to, "danger", delay + 460, 9);
        this.tracer(from, to, "danger", delay + 400, 150);
        this.tracer(to, from, "danger", delay + 480, 150);
        this.recoil(actor, delay + 380);
        if (target) this.recoil(target, delay + 460);
        break;
      }
      case "blast":
        this.flyProp(cardKey, from, to, { delay, duration: 460, spin: 2, arc: 22 });
        this.flash(to, "blast", delay + 470, 22);
        this.shardBurst(to, "blast", 26, 20, delay + 470);
        this.dustRing(to, "blast", 34);
        if (target) { this.hit(target.cid, delay + 480); this.thump(target, 1.6); }
        for (const figure of this.figures.values()) this.thump(figure, 0.5);
        this.tween({
          delay: delay + 470, duration: 1, update: () => {},
          done: () => { this.kick(5); this.focusOn(to, { strength: 0.5, zoom: 0.82, hold: 900, priority: 2 }); },
        });
        break;
      case "serpent":
        this.flyProp(cardKey, from, to, { delay, duration: 560, spin: 0, arc: 3 });
        this.flash(to, "venom", delay + 560, 10);
        this.motes(to, "venom", delay + 560, 10);
        if (target) this.hit(target.cid, delay + 560);
        break;
      case "tonic":
        this.presentProp(cardKey, from, { delay, hold: 320 });
        this.motes(from, "heal", delay + 240, 16);
        if (actor) this.heal(actor.cid, delay + 240);
        break;
      case "deflect":
        this.presentProp(cardKey, from, { delay, hold: 260 });
        this.flash(from, "guard", delay + 180, 14);
        this.dustRing(from, "guard", 20);
        if (actor) {
          this.tween({
            delay, duration: 380,
            update: t => { actor.pose.stagger = Math.sin(t * Math.PI) * 2.2; },
            done: () => { actor.pose.stagger = 0; },
          });
        }
        break;
      case "plunder":
        // the prop is dragged the other way: off the target, back to the actor
        this.flyProp(cardKey, to, from, { delay, duration: 460, spin: 1.5, arc: 16 });
        this.flash(to, "greed", delay, 10);
        this.motes(from, "greed", delay + 420, 8);
        break;
      case "bazaar": {
        const centre = new THREE.Vector3(0, 9, 0);
        this.presentProp(cardKey, centre, { delay, hold: 420 });
        for (let index = 0; index < 5; index += 1) {
          const angle = (index / 5) * Math.PI * 2;
          const spot = new THREE.Vector3(Math.cos(angle) * 13, 7, Math.sin(angle) * 13);
          this.flyProp("general_store", centre, spot, { delay: delay + index * 60, duration: 380, spin: 1.5, arc: 8 });
        }
        break;
      }
      case "shuffle":
        this.flyProp(cardKey, from, to, { delay, duration: 420, spin: 2, arc: 18 });
        for (const figure of this.figures.values()) {
          this.tween({
            delay: delay + 200, duration: 520,
            update: t => { figure.pose.lift = Math.sin(t * Math.PI) * 2.4; },
            done: () => { figure.pose.lift = 0; },
          });
        }
        this.dustRing(new THREE.Vector3(0, 0, 0), "neutral", 40);
        break;
      case "rig": {
        const wearer = target || actor;
        const at = wearer ? wearer.worldPoint(0.75) : to;
        this.flyProp(cardKey, from, at, { delay, duration: 380, spin: 1, arc: 10 });
        this.flash(at, "gear", delay + 380, 9);
        if (wearer) this.thump(wearer, 0.5);
        break;
      }
      default:
        this.flyProp(cardKey, from, to, { delay, duration: 400, spin: 1, arc: 12 });
        this.flash(to, "neutral", delay + 400, 8);
        break;
    }
  }

  /* A card resolving, on the battlefield.

     Every card in the deck has a scene of its own below. The motif is still
     what the 2D board and the sound run on — it decides how long the table
     waits and what noise it makes — but out here a Schofield is not "a
     gunshot", it is a nickel revolver coming up, a shot, and a breath of
     relief going back to the man who fired it. A card with no scene of its
     own falls back to its motif, so a card added to the deck tomorrow still
     does something sensible tonight. */
  ceremony(motif, { actorZone, targetZone, cardKey, delay = 0 }) {
    const actor = this.figureAt(actorZone) || this.figureAt("own_active");
    const target = this.figureAt(targetZone) || null;
    const from = actor ? actor.worldPoint(0.62) : new THREE.Vector3(0, 8, 20);
    const to = target ? target.worldPoint(0.55) : new THREE.Vector3(0, 8, 0);
    const context = { actor, target, from, to, cardKey, delay, motif };

    /* Lean the shot toward whatever this card is about. It is a nudge, not a
       cut — the whole field stays readable, but the camera is visibly
       interested in the half of the table the play is happening on. */
    const lean = target ? to.clone().lerp(from, 0.35) : from;
    this.at(delay, () => this.focusOn(lean, { strength: 0.3, zoom: 0.93, hold: 1100, priority: 1 }));

    // A card spent to answer a hit is a different scene from the same card
    // played on your own turn: a Barrel equipped is a barrel rolling in, a
    // Barrel used is a barrel taking a bullet meant for somebody.
    const scene = motif === "deflect"
      ? (BLOCKS[cardKey] || BLOCKS.default)
      : SCENES[cardKey];
    if (scene) scene(this, context);
    else this.motifCeremony(motif, context);
  }

  dispose() {
    this.stop();
    for (const figure of this.figures.values()) figure.dispose();
    this.figures.clear();
    this.renderer.dispose();
    this.renderer.domElement.remove();
  }
}

/* --------------------------------------------------------------------------
   A scene for every card

   The whole deck, one entry each. A scene is handed the stage and everything
   it needs to know about the play — who did it, to whom, from where, and on
   what beat — and stages it with the verbs above.

   Two rules keep them honest. The first is that the object on the card is the
   object on the field: a Winchester is a rifle in somebody's hands, a Bank is
   a strongbox in the dust, a Rattlesnake crosses the ground on its belly. The
   second is that the scene has to say what the card *does*, not only what it
   looks like — a Peacemaker makes the target drop a card, so on the field a
   card comes out of them; Schofield gives life back, so something warm goes
   home to the shooter.
   -------------------------------------------------------------------------- */

// The rest of a shot: the target takes it, on the beat the round arrives.
function strike(stage, context, when, tone = "danger") {
  if (!context.target) return;
  stage.flash(context.to, tone, when, 8);
  stage.hit(context.target.cid, when);
}

const SCENES = {
  /* ---------- the guns ---------- */

  colt(stage, context) {
    strike(stage, context, stage.gunplay(context, {}) + 110);
  },

  // Nickel-plated, and forgiving: a miss puts life back into the shooter.
  schofield(stage, context) {
    const shot = stage.gunplay(context, { aimTime: 210, flashSize: 10 });
    strike(stage, context, shot + 110);
    stage.motes(context.from, "heal", shot + 320, 10, { spread: 6, rise: 7 });
  },

  // A miss pays you a card, so one comes back down the line of fire.
  remington(stage, context) {
    const shot = stage.gunplay(context, {});
    strike(stage, context, shot + 110);
    stage.cardFly(context.to, context.from, { delay: shot + 260, duration: 420 });
  },

  // The long barrel, and a card shaken out of whoever it was pointed at.
  peacemaker(stage, context) {
    const shot = stage.gunplay(context, { aimTime: 260, flashSize: 12, kick: 0.7, shake: 0.8 });
    strike(stage, context, shot + 120);
    const dropped = context.to.clone().add(new THREE.Vector3(0, -3, 6));
    stage.cardFly(context.to, dropped, { delay: shot + 300, duration: 520, arc: 5 });
  },

  // A rifle: shouldered, held a beat longer, and cycled afterwards.
  winchester(stage, context) {
    const shot = stage.gunplay(context, {
      aimTime: 320, flashSize: 13, kick: 0.9, shake: 1.2, tracerMs: 200,
    });
    strike(stage, context, shot + 130);
    // working the lever: a second brass case out, no second shot
    stage.at(shot + 300, () => {
      const muzzle = context.actor?.muzzlePoint();
      if (muzzle) stage.shardBurst(muzzle, "brass", 3, 6);
      if (context.actor) context.actor.kickBack = 0.22;
    });
  },

  // Short rifle, and it tears gear off whatever it hits.
  carbine(stage, context) {
    const shot = stage.gunplay(context, { aimTime: 280, flashSize: 11, kick: 0.8, shake: 0.9 });
    strike(stage, context, shot + 120);
    stage.shardBurst(context.to, "gear", 12, 13, shot + 200);
  },

  // Both barrels: a cone of shot, and the target goes backwards.
  shotgun(stage, context) {
    const shot = stage.gunplay(context, {
      aimTime: 300, flashSize: 16, kick: 1.3, shake: 1.8, shots: 1, tracerMs: 130,
    });
    stage.at(shot, () => {
      const muzzle = context.actor?.muzzlePoint() || context.from;
      for (let pellet = 0; pellet < 7; pellet += 1) {
        const mark = context.to.clone();
        mark.x += (Math.random() - 0.5) * 9;
        mark.y += (Math.random() - 0.5) * 7;
        mark.z += (Math.random() - 0.5) * 9;
        stage.tracer(muzzle, mark, "danger", 0, 150);
      }
      stage.motes(muzzle, "smoke", 0, 12, { spread: 6, rise: 6, size: 1.4, life: 900 });
    });
    strike(stage, context, shot + 110);
    if (context.target) stage.thump(context.target, 1.4);
  },

  // A crank gun. It does not aim at anybody: it sweeps the field, and
  // everything that is not the man turning the handle is in the way.
  gatling(stage, context) {
    const caught = stage.bystanders(context.actor);
    const marks = caught.map(figure => figure.worldPoint(0.55));
    const sweep = marks.length
      ? marks.reduce((sum, mark) => sum.add(mark), new THREE.Vector3()).divideScalar(marks.length)
      : context.to;
    const shot = stage.gunplay(context, {
      weapon: "gatling", aimAt: sweep, shots: 14, gap: 55, aimTime: 340,
      spread: 16, flashSize: 7, kick: 0.22, shake: 0.4,
    });
    caught.forEach((figure, index) => stage.hit(figure.cid, shot + 160 + index * 120));
    stage.at(shot + 400, () => stage.shardBurst(context.from, "brass", 14, 9));
    stage.at(shot + 200, () => stage.focusOn(sweep, { strength: 0.45, zoom: 0.88, hold: 900, priority: 2 }));
  },

  // One revolver, the hammer slapped rather than cocked: as many shots as the
  // target has gear, all of them in about a second.
  fanning(stage, context) {
    const shot = stage.gunplay(context, {
      shots: 5, gap: 78, aimTime: 200, spread: 3, kick: 0.4, shake: 0.45, flashSize: 8,
    });
    strike(stage, context, shot + 130);
    if (context.target) {
      stage.hit(context.target.cid, shot + 300);
      stage.thump(context.target, 0.9);
    }
    stage.motes(context.from, "smoke", shot + 260, 10, { spread: 7, rise: 6, life: 900 });
  },

  // A hold-out pistol: no draw worth the name, just a pop out of a sleeve.
  derringer(stage, context) {
    const shot = stage.gunplay(context, {
      aimTime: 110, flashSize: 6, kick: 0.3, shake: 0.25, tracerMs: 110,
    });
    strike(stage, context, shot + 90);
  },

  // Thrown, not fired: raised, released, and end over end across the dust.
  knife(stage, context) {
    const { actor, from, to, delay } = context;
    stage.at(delay, () => { actor?.hold("knife"); actor?.aimAt(to); });
    stage.at(delay + 240, () => {
      const grip = actor?.muzzlePoint() || from;
      actor?.hold(null);
      actor?.lowerAim();
      stage.flyProp("knife", grip, to, { duration: 340, spin: 3, arc: 6 });
    });
    stage.at(delay + 800, () => actor?.holster());
    strike(stage, context, delay + 590);
    stage.shardBurst(to, "danger", 8, 8, delay + 590);
  },

  /* ---------- everything else the deck can do ---------- */

  // A volley out of the treeline: everyone but the caller is under it.
  indians(stage, context) {
    const { actor, from, delay } = context;
    const marks = stage.bystanders(actor);
    stage.at(delay, () => { actor?.hold("indians"); actor?.aimAt(from.clone().setY(from.y + 30)); });
    marks.forEach((figure, index) => {
      const when = delay + 260 + index * 130;
      const mark = figure.worldPoint(0.5);
      stage.flyProp("indians", from.clone().setY(from.y + 6), mark,
                    { delay: when, duration: 420, spin: 0, arc: 26 });
      stage.hit(figure.cid, when + 420);
      stage.shardBurst(mark, "danger", 6, 7, when + 420);
    });
    stage.at(delay + 420, () => { actor?.lowerAim(); });
    stage.at(delay + 760, () => actor?.holster());
  },

  // Two people, ten paces, and whoever blinks first. The camera comes in.
  duel(stage, context) {
    const { actor, target, from, to, delay } = context;
    stage.at(delay, () => {
      actor?.hold("duel");
      actor?.aimAt(to);
      target?.hold("duel");
      target?.aimAt(from);
      stage.focusOn(from.clone().lerp(to, 0.5), { strength: 0.6, zoom: 0.78, hold: 1500, priority: 2 });
    });
    for (let round = 0; round < 3; round += 1) {
      const beat = delay + 380 + round * 240;
      stage.at(beat, () => {
        const muzzle = actor?.muzzlePoint() || from;
        stage.flash(muzzle, "danger", 0, 9);
        stage.tracer(muzzle, to, "danger", 0, 130);
        if (actor) actor.kickBack = 0.55;
      });
      stage.recoil(actor, beat);
      if (!target) continue;
      stage.at(beat + 110, () => {
        const muzzle = target.muzzlePoint();
        stage.flash(muzzle, "danger", 0, 9);
        stage.tracer(muzzle, from, "danger", 0, 130);
        target.kickBack = 0.55;
      });
      stage.recoil(target, beat + 110);
    }
    strike(stage, context, delay + 1120);
    stage.at(delay + 1180, () => {
      actor?.lowerAim();
      target?.lowerAim();
      stage.kick(1.6);
    });
    stage.at(delay + 1500, () => { actor?.holster(); target?.holster(); });
  },

  // Lit, thrown, and then the wait — the fuse is most of the scene.
  dynamite(stage, context) {
    const { from, to, delay } = context;
    stage.flash(from, "spark", delay, 5);
    stage.flyProp("dynamite", from, to, { delay: delay + 120, duration: 480, spin: 3, arc: 26 });
    for (let tick = 0; tick < 3; tick += 1) {
      stage.at(delay + 640 + tick * 150, () => {
        stage.flash(to.clone().setY(GROUND + 5), "spark", 0, 4);
      });
    }
    const boom = delay + 1080;
    stage.at(boom, () => {
      stage.flash(to, "blast", 0, 28);
      stage.shardBurst(to, "blast", 30, 24);
      stage.shardBurst(to, "smoke", 18, 16);
      stage.shockwave(to, "dust", 0, 52);
      stage.dustRing(to, "blast", 38);
      stage.kick(5.5);
      stage.focusOn(to, { strength: 0.55, zoom: 0.8, hold: 1000, priority: 2 });
    });
    if (context.target) stage.hit(context.target.cid, boom + 40);
    stage.jolt(0.8, boom);
  },

  // Along the ground, up at the ankle, and it stays where it struck.
  rattlesnake(stage, context) {
    const { from, to, delay } = context;
    stage.crawl("rattlesnake", from, to, { delay, duration: 760, wiggle: 4, scale: 1.1 });
    stage.at(delay + 780, () => {
      stage.flash(to.clone().setY(GROUND + 4), "venom", 0, 9);
      stage.motes(to.clone().setY(GROUND + 3), "venom", 0, 10, { spread: 6, rise: 6 });
    });
    strike(stage, context, delay + 800, "venom");
  },

  // Raised, drunk, and the whole side of the table feels better for it.
  beer(stage, context) {
    const { actor, from, delay } = context;
    stage.presentProp("beer", from.clone().setY(from.y + 3), { delay, hold: 420 });
    stage.motes(from, "heal", delay + 260, 18, { spread: 8, rise: 8 });
    if (actor) {
      stage.tween({
        delay, duration: 700,
        update: t => { actor.pose.tilt = -Math.sin(t * Math.PI) * 0.22; },
        done: () => { actor.pose.tilt = 0; },
      });
      stage.heal(actor.cid, delay + 300);
    }
    // Beer picks up the rearguard too, so the whole side of the field lifts.
    const side = actor ? actor.seat.slice(0, actor.seat.indexOf("_")) : "own";
    for (const figure of stage.figures.values()) {
      if (!figure.seat.startsWith(side) || figure === actor) continue;
      stage.heal(figure.cid, delay + 420);
    }
  },

  // Stronger stuff: it sets a character to three, up or down, so it lands on
  // whoever drinks it rather than drifting over the table.
  strong_ale(stage, context) {
    const mark = context.target || context.actor;
    const at = mark ? mark.worldPoint(0.6) : context.to;
    stage.presentProp("strong_ale", at.clone().setY(at.y + 4), { delay: context.delay, hold: 460 });
    stage.at(context.delay + 320, () => {
      stage.flash(at, "heal", 0, 16);
      stage.motes(at, "heal", 0, 20, { spread: 7, rise: 11 });
      stage.dustRing(at, "heal", 22);
    });
    if (mark) stage.heal(mark.cid, context.delay + 340);
  },

  // A barrel rolls in out of the dust and stands where it will be needed.
  barrel(stage, context) {
    const wearer = context.target || context.actor;
    const at = wearer ? wearer.worldPoint(0) : context.to;
    const rolledFrom = at.clone().add(new THREE.Vector3(0, 0, 34));
    stage.crawl("barrel", rolledFrom, at, { delay: context.delay, duration: 460, wiggle: 0, spin: 7 });
    stage.at(context.delay + 470, () => {
      stage.dustRing(at, "gear", 18);
      stage.kick(0.7);
      if (wearer) stage.thump(wearer, 0.5);
    });
  },

  // Spun in and dropped on the head.
  hat(stage, context) {
    const wearer = context.target || context.actor;
    const at = wearer ? wearer.worldPoint(1.05) : context.to;
    stage.flyProp("hat", at.clone().add(new THREE.Vector3(0, 22, 8)), at,
                  { delay: context.delay, duration: 480, spin: 3, arc: 0 });
    stage.at(context.delay + 490, () => stage.flash(at, "gear", 0, 8));
    if (wearer) stage.thump(wearer, 0.35);
  },

  // A belt of cartridges swung round the waist and buckled.
  gun_belt(stage, context) {
    const wearer = context.target || context.actor;
    const at = wearer ? wearer.worldPoint(0.42) : context.to;
    stage.flyProp("gun_belt", at.clone().add(new THREE.Vector3(16, 8, 10)), at,
                  { delay: context.delay, duration: 420, spin: 2, arc: 4 });
    stage.at(context.delay + 430, () => {
      stage.flash(at, "brass", 0, 9);
      stage.shardBurst(at, "brass", 8, 7);
    });
  },

  // Glass out, and the far end of the street suddenly within reach.
  scope(stage, context) {
    const wearer = context.target || context.actor;
    const at = wearer ? wearer.worldPoint(0.88) : context.from;
    stage.presentProp("scope", at, { delay: context.delay, hold: 320 });
    const far = wearer && wearer.seat.startsWith("own")
      ? SEATS.opponent_rearguard : SEATS.own_rearguard;
    const mark = new THREE.Vector3(far.x, GROUND + 10, far.z);
    stage.reticle(mark, "guard", context.delay + 300, 11);
    stage.tracer(at, mark, "guard", context.delay + 260, 320);
    stage.at(context.delay + 320, () =>
      stage.focusOn(mark, { strength: 0.4, zoom: 0.9, hold: 700, priority: 1 }));
  },

  // A strongbox comes down where the character is standing, and pays out.
  bank(stage, context) {
    const wearer = context.target || context.actor;
    const at = wearer ? wearer.worldPoint(0) : context.to;
    stage.flyProp("bank", at.clone().add(new THREE.Vector3(0, 30, 0)), at,
                  { delay: context.delay, duration: 380, spin: 0.4, arc: 0 });
    stage.at(context.delay + 390, () => {
      stage.dustRing(at, "gear", 22);
      stage.shardBurst(at.clone().setY(at.y + 4), "brass", 14, 11);
      stage.kick(1.1);
      if (wearer) stage.thump(wearer, 0.7);
    });
  },

  // A watch on a chain, and the field marking time with it.
  clock(stage, context) {
    const wearer = context.target || context.actor;
    const at = wearer ? wearer.worldPoint(0.72) : context.to;
    stage.flyProp("clock", at.clone().add(new THREE.Vector3(0, 24, 6)), at,
                  { delay: context.delay, duration: 460, spin: 1, arc: 0 });
    for (let tick = 0; tick < 3; tick += 1) {
      stage.reticle(at, "brass", context.delay + 480 + tick * 200, 8 + tick * 3);
    }
  },

  // Cattle come off the plain and through the middle of the duel; both sides
  // give ground while it passes.
  stampede(stage, context) {
    const start = new THREE.Vector3(-70, GROUND, -4);
    const end = new THREE.Vector3(70, GROUND, -4);
    for (let head = 0; head < 5; head += 1) {
      const lane = (head - 2) * 7;
      stage.crawl("stampede",
                  start.clone().add(new THREE.Vector3(-head * 12, 0, lane)),
                  end.clone().add(new THREE.Vector3(head * 6, 0, lane)),
                  { delay: context.delay + head * 90, duration: 900, wiggle: 1.2, scale: 1.3 });
    }
    stage.at(context.delay + 380, () => {
      stage.shockwave(new THREE.Vector3(0, GROUND, -4), "dust", 0, 60);
      stage.kick(2.4);
    });
    stage.jolt(1.1, context.delay + 400);
    stage.motes(new THREE.Vector3(0, GROUND + 3, -4), "dust", context.delay + 380, 24,
                { spread: 70, rise: 7, size: 1.6, life: 1400 });
  },

  // A hand across the table, and something in it on the way back.
  stealing(stage, context) {
    const { from, to, delay } = context;
    stage.flyProp("stealing", from, to, { delay, duration: 340, spin: 0, arc: 5 });
    stage.at(delay + 350, () => {
      stage.flash(to, "greed", 0, 10);
      if (context.target) stage.thump(context.target, 0.5);
    });
    stage.cardFly(to, from, { delay: delay + 420, duration: 440, arc: 12 });
    stage.motes(from, "greed", delay + 840, 8, { spread: 5, rise: 6 });
  },

  // The schoolmarm does not steal cards, she confiscates equipment.
  schoolmarm(stage, context) {
    const { from, to, delay } = context;
    stage.flyProp("schoolmarm", from, to, { delay, duration: 380, spin: 1, arc: 12 });
    stage.at(delay + 400, () => {
      stage.flash(to, "greed", 0, 11);
      stage.shardBurst(to, "gear", 10, 9);
    });
    stage.flyProp("gun_belt", to, from, { delay: delay + 440, duration: 460, spin: 2, arc: 16 });
  },

  // Four cards face up on the dust between the two sides.
  general_store(stage, context) {
    const middle = new THREE.Vector3(0, 11, 0);
    stage.presentProp("general_store", middle, { delay: context.delay, hold: 300 });
    for (let card = 0; card < 4; card += 1) {
      const angle = (card / 4) * Math.PI * 2 + 0.4;
      const spot = new THREE.Vector3(Math.cos(angle) * 15, 9, Math.sin(angle) * 9);
      stage.cardFly(middle, spot, { delay: context.delay + 220 + card * 80, duration: 420, arc: 7 });
      stage.reticle(spot, "neutral", context.delay + 640 + card * 80, 6);
    }
  },

  // The wagon comes down the back of the street and throws three off.
  wagon(stage, context) {
    const lane = -30;
    const start = new THREE.Vector3(-78, GROUND, lane);
    const end = new THREE.Vector3(78, GROUND, lane);
    stage.crawl("wagon", start, end, { delay: context.delay, duration: 1200, wiggle: 0.6, scale: 1.6 });
    for (let card = 0; card < 3; card += 1) {
      const when = context.delay + 380 + card * 160;
      const dropAt = new THREE.Vector3(-24 + card * 22, 12, lane + 4);
      stage.cardFly(dropAt, context.from, { delay: when, duration: 520, arc: 14 });
    }
    stage.motes(new THREE.Vector3(0, GROUND + 2, lane), "dust", context.delay + 300, 14,
                { spread: 60, rise: 5, life: 1200 });
  },

  // A bucket over the pair of them, and they change places.
  water(stage, context) {
    const mark = context.target || context.actor;
    const side = mark ? mark.seat.slice(0, mark.seat.indexOf("_")) : "own";
    const at = mark ? mark.worldPoint(0.9) : context.to;
    stage.flyProp("water", context.from, at, { delay: context.delay, duration: 420, spin: 2, arc: 18 });
    stage.at(context.delay + 430, () => {
      stage.flash(at, "water", 0, 14);
      stage.motes(at, "water", 0, 22, { spread: 10, rise: 9 });
      stage.shockwave(at, "water", 0, 26);
    });
    // Both characters on that side hop and trade places.
    for (const figure of stage.figures.values()) {
      if (!figure.seat.startsWith(side)) continue;
      stage.tween({
        delay: context.delay + 460, duration: 620,
        update: t => {
          figure.pose.lift = Math.sin(t * Math.PI) * 3.4;
          figure.pose.turn = Math.sin(t * Math.PI * 2) * 0.6;
        },
        done: () => { figure.pose.lift = 0; figure.pose.turn = 0; },
      });
    }
  },
};

/* A card spent to stay alive. The same object, doing the opposite job: the
   Barrel is not equipment arriving, it is equipment being shot at. */
const BLOCKS = {
  // Nobody blocks a bullet. They get out of its way.
  missed(stage, context) {
    const { actor, from, delay } = context;
    const past = from.clone().add(new THREE.Vector3(0, 2, 0));
    const incoming = stage.opposite(actor);
    const origin = incoming ? incoming.worldPoint(0.6) : from.clone().add(new THREE.Vector3(-40, 4, 0));
    stage.tracer(origin, past.clone().lerp(origin, -0.4), "danger", delay, 190);
    stage.flyProp("missed", origin, past.clone().lerp(origin, -0.3),
                  { delay, duration: 260, spin: 0, arc: 1 });
    if (actor) {
      stage.tween({
        delay, duration: 420,
        update: t => {
          const lean = Math.sin(t * Math.PI);
          actor.pose.stagger = lean * 3.4 * (actor.seat.startsWith("own") ? 1 : -1);
          actor.pose.spin = lean * 0.22;
        },
        done: () => { actor.pose.stagger = 0; actor.pose.spin = 0; },
      });
    }
    stage.dustRing(from, "guard", 16);
    stage.motes(from.clone().setY(GROUND + 2), "dust", delay + 120, 8, { spread: 7, rise: 5 });
  },

  // Blocked, and answered in the same movement.
  return_fire(stage, context) {
    const { actor, from, delay } = context;
    stage.flash(from, "guard", delay, 13);
    stage.presentProp("return_fire", from, { delay, hold: 200 });
    const back = stage.opposite(actor);
    const mark = back ? back.worldPoint(0.55) : new THREE.Vector3(-from.x, from.y, from.z);
    const shot = stage.gunplay({ actor, to: mark, delay: delay + 220, cardKey: "colt" },
                               { aimTime: 170, flashSize: 10, kick: 0.7 });
    if (back) {
      stage.hit(back.cid, shot + 120);
      stage.flash(mark, "danger", shot + 120, 8);
    }
  },

  // The barrel takes it. That is what a barrel is for.
  barrel(stage, context) {
    const { actor, from, delay } = context;
    const at = actor ? actor.attachmentPoint("barrel") : from;
    const incoming = stage.opposite(actor);
    if (incoming) stage.tracer(incoming.worldPoint(0.6), at, "danger", delay, 150);
    stage.at(delay + 150, () => {
      stage.flash(at, "gear", 0, 11);
      stage.shardBurst(at, "gear", 16, 12);
      stage.dustRing(at, "gear", 18);
      stage.kick(1.1);
    });
    if (actor) stage.thump(actor, 0.6);
  },

  // The hat is shot clean off and goes over the fence.
  hat(stage, context) {
    const { actor, from, delay } = context;
    const at = actor ? actor.worldPoint(1.02) : from;
    stage.at(delay, () => stage.flash(at, "guard", 0, 9));
    stage.flyProp("hat", at, at.clone().add(new THREE.Vector3(0, -6, 26)),
                  { delay: delay + 60, duration: 620, spin: 4, arc: 12 });
    if (actor) {
      stage.tween({
        delay, duration: 380,
        update: t => { actor.pose.tilt = -Math.sin(t * Math.PI) * 0.2; },
        done: () => { actor.pose.tilt = 0; },
      });
    }
  },

  // Anything else spent to survive: it goes up, it takes the hit.
  default(stage, context) {
    const { actor, from, cardKey, delay } = context;
    stage.presentProp(cardKey, from, { delay, hold: 260 });
    stage.flash(from, "guard", delay + 160, 14);
    stage.dustRing(from, "guard", 20);
    if (!actor) return;
    stage.tween({
      delay, duration: 380,
      update: t => { actor.pose.stagger = Math.sin(t * Math.PI) * 2.2; },
      done: () => { actor.pose.stagger = 0; },
    });
  },
};

/* Missed! and Return Fire almost always reach the field as answers to a hit
   rather than as plays, but the engine can resolve either as a play, and when
   it does the card should still look like itself. */
SCENES.missed = BLOCKS.missed;
SCENES.return_fire = BLOCKS.return_fire;

/* --------------------------------------------------------------------------
   What the board talks to
   -------------------------------------------------------------------------- */

let stage = null;

function supported() {
  if (!models) return false;
  try {
    const canvas = document.createElement("canvas");
    return !!(canvas.getContext("webgl2") || canvas.getContext("webgl"));
  } catch {
    return false;
  }
}

const api = {
  supported,
  ready: false,

  mount(container) {
    if (!container || !supported()) return false;
    if (stage) stage.dispose();
    stage = new Stage(container);
    stage.resize();
    stage.start();
    api.ready = true;
    return true;
  },

  resize() { stage?.resize(); },
  sync(seated, dressDelay) {
    stage?.sync(seated || [], dressDelay || 0);
    // A window onto the cast, so the stage can be inspected from outside.
    globalThis.__bangStageFigures = stage
      ? [...stage.figures.values()].map(f => ({ cid: f.cid, key: f.key, seat: f.seat, dead: f.dead }))
      : [];
  },
  // What each figure is holding and where it is pointing, for a harness that
  // wants to check the draw actually happened.
  aiming() {
    return stage ? [...stage.figures.values()].map(figure => ({
      cid: figure.cid,
      seat: figure.seat,
      held: figure.held?.key || null,
      aim: Number(figure.aim.toFixed(2)),
      gear: [...figure.attachments.keys()],
      muzzle: figure.muzzlePoint().toArray().map(value => Math.round(value)),
    })) : [];
  },
  arrive(cid, delay) { stage?.arrive(cid, delay); },
  down(cid, delay) { stage?.down(cid, delay); },
  fall(cid, key, zone, delay, swept) { stage?.fall(cid, key, zone, delay, swept); },
  hit(cid, delay) { stage?.hit(cid, delay); },
  heal(cid, delay) { stage?.heal(cid, delay); },
  ceremony(motif, options) { stage?.ceremony(motif, options); },
  dispose() { stage?.dispose(); stage = null; api.ready = false; },

  /* Which cards have a scene of their own, and which have one for being spent
     to survive. Nothing on the board reads this — it is here so a test can
     check the deck against the stage without a browser. */
  staged() {
    return { plays: Object.keys(SCENES).sort(), blocks: Object.keys(BLOCKS).sort() };
  },
};

globalThis.BangVoxelStage = api;
// A read-only listing of what is currently on the stage, for test harnesses.
globalThis.__bangStageScene = () => (stage ? stage.scene.children.map(child => ({
  type: child.type,
  visible: child.visible,
  at: [Math.round(child.position.x), Math.round(child.position.y), Math.round(child.position.z)],
})) : []);
// A read-only window on the running stage, for driving it from a test harness.
globalThis.__bangStageDebug = () => stage && ({
  clock: Math.round(stage.clock * 1000),
  distance: Math.round(stage.baseDistance),
  zoom: Number(stage.zoom.toFixed(2)),
  aspect: Number((stage.camera.aspect || 0).toFixed(2)),
  canvas: [stage.container.clientWidth, stage.container.clientHeight],
  frames: stage.frames,
  tweens: stage.tweens.length,
  children: stage.scene.children.length,
  figures: stage.figures.size,
});
globalThis.dispatchEvent(new Event("bang-voxel-stage-ready"));

export default api;
