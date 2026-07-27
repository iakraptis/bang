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

function voxelMesh(voxels, { origin = "feet" } = {}) {
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
  const offsetX = -(bounds.min[0] + bounds.max[0]) / 2;
  const offsetY = origin === "feet" ? -bounds.min[1] : -(bounds.min[1] + bounds.max[1]) / 2;
  const offsetZ = -(bounds.min[2] + bounds.max[2]) / 2;
  const matrix = new THREE.Matrix4();
  voxels.forEach((voxel, index) => {
    matrix.makeTranslation(voxel.x + offsetX, voxel.y + offsetY, voxel.z + offsetZ);
    mesh.setMatrixAt(index, matrix);
    mesh.setColorAt(index, colourFor(voxel.color));
  });
  mesh.instanceMatrix.needsUpdate = true;
  if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
  mesh.userData.bounds = bounds;
  return mesh;
}

/* --------------------------------------------------------------------------
   A character on the stage
   -------------------------------------------------------------------------- */

class Figure {
  constructor(cid, key, seat) {
    this.cid = cid;
    this.key = key;
    this.seat = seat;
    this.group = new THREE.Group();
    this.body = new THREE.Group();
    this.group.add(this.body);
    const built = models.buildFigure(key);
    this.mesh = voxelMesh(built.voxels);
    this.height = built.bounds.size[1];
    this.body.add(this.mesh);
    this.phase = Math.random() * Math.PI * 2;   // so figures do not breathe in lockstep
    this.swayPhase = Math.random() * Math.PI * 2;
    this.pose = { recoil: 0, stagger: 0, lift: 0, tilt: 0, sink: 0, spin: 0, turn: 0 };
    this.dead = false;
    this.active = false;
    this.placeAt(seat);
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
  update(time) {
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
    this.mesh.geometry === CUBE ? null : this.mesh.geometry.dispose();
    this.mesh.material.dispose();
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
    for (const figure of this.figures.values()) figure.update(this.clock);
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
  sync(seated) {
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
    for (const [cid, figure] of [...this.figures]) {
      // A figure in the middle of falling is already off the board's books;
      // its own ceremony clears it away when it finishes going down.
      if (wanted.has(cid) || figure.dead) continue;
      figure.dispose();
      this.figures.delete(cid);
    }
    this.lightTheActive();
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
  motes(position, tone, delay = 0, count = 12) {
    const material = new THREE.MeshBasicMaterial({ color: toneOf(tone), transparent: true, opacity: 0.9 });
    const mesh = new THREE.InstancedMesh(CUBE, material, count);
    const seeds = Array.from({ length: count }, () => ({
      x: (Math.random() - 0.5) * 9,
      z: (Math.random() - 0.5) * 9,
      rise: 9 + Math.random() * 12,
      scale: 0.3 + Math.random() * 0.5,
    }));
    mesh.position.copy(position);
    this.spawn(mesh, delay + 1400);
    const matrix = new THREE.Matrix4();
    const quaternion = new THREE.Quaternion();
    const scale = new THREE.Vector3();
    const offset = new THREE.Vector3();
    this.tween({
      delay, duration: 1100,
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

  /* ---------- the motifs, staged in three dimensions ----------
     Same vocabulary the 2D ceremonies use, so a card behaves the same way in
     both places: a gunshot is a shot here too, Dynamite still shatters. */

  ceremony(motif, { actorZone, targetZone, cardKey, delay = 0 }) {
    const actor = this.figureAt(actorZone) || this.figureAt("own_active");
    const target = this.figureAt(targetZone) || null;
    const from = actor ? actor.worldPoint(0.62) : new THREE.Vector3(0, 8, 20);
    const to = target ? target.worldPoint(0.55) : new THREE.Vector3(0, 8, 0);

    /* Lean the shot toward whatever this card is about. It is a nudge, not a
       cut — the whole field stays readable, but the camera is visibly
       interested in the half of the table the play is happening on. */
    const lean = target ? to.clone().lerp(from, 0.35) : from;
    this.tween({
      delay, duration: 1, update: () => {},
      done: () => this.focusOn(lean, { strength: 0.3, zoom: 0.93, hold: 1100, priority: 1 }),
    });

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

  dispose() {
    this.stop();
    for (const figure of this.figures.values()) figure.dispose();
    this.figures.clear();
    this.renderer.dispose();
    this.renderer.domElement.remove();
  }
}

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
  sync(seated) {
    stage?.sync(seated || []);
    // A window onto the cast, so the stage can be inspected from outside.
    globalThis.__bangStageFigures = stage
      ? [...stage.figures.values()].map(f => ({ cid: f.cid, key: f.key, seat: f.seat, dead: f.dead }))
      : [];
  },
  arrive(cid, delay) { stage?.arrive(cid, delay); },
  down(cid, delay) { stage?.down(cid, delay); },
  fall(cid, key, zone, delay, swept) { stage?.fall(cid, key, zone, delay, swept); },
  hit(cid, delay) { stage?.hit(cid, delay); },
  heal(cid, delay) { stage?.heal(cid, delay); },
  ceremony(motif, options) { stage?.ceremony(motif, options); },
  dispose() { stage?.dispose(); stage = null; api.ready = false; },
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
