import { createAreaRenderer } from "./farm-area-renderer.js";
import { FLAGS } from "./farm-vocabulary.js";
import * as THREE from "./vendor/three/three.module.min.js";
import { OrbitControls } from "./vendor/three/OrbitControls.min.js";
import { SIZE, actors, animals, draggableById, canPlaceFlag, tileAt, walkable, unlocked } from "./farm-world.js";
import { createCow, createFarmer, createTree, createHouse, createChicken, createFlag, createItem, createEggs, createCompost, createWaterMaterial, material } from "./farm-art.js";
import { createCrop, cropStage } from "./farm-crops.js";
import { ActorMotion } from "./farm-motion.js";
import { equipFarmer, poseFarmerArms } from "./farm-equipment.js";
import { createFarmFences } from "./farm-fences.js";
import { createSkyReflection } from "./farm-lighting.js";

const OFFSET = (SIZE - 1) / 2;
const COLORS = [0xcd8b49, 0x5e9299, 0xb17c90, 0x8b9460, 0xb49352, 0x778bae];

const worldPoint = (world, position, extra = 0) => {
  const tile = world.tiles[position.y][position.x];
  return new THREE.Vector3(position.x - OFFSET, tile.height - (tile.type === "channel" ? .16 : 0) + extra, position.y - OFFSET);
};

export function createFarmRenderer(board, initialWorld, callbacks) {
  const canvas = document.createElement("canvas");
  canvas.tabIndex = 0;
  canvas.setAttribute("aria-label", "3D farm. Click a worker to choose their role, drag workers, chickens or flags to place them, click ground to walk. Right-drag to orbit; scroll to zoom.");
  board.append(canvas);
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true, depth: true });
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFShadowMap;
  renderer.shadowMap.autoUpdate = false;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.14;
  const scene = new THREE.Scene();
  scene.fog = new THREE.Fog(0xd4ddc3, 55, 100);
  const camera = new THREE.PerspectiveCamera(35, 1, .1, 150);
  const controls = new OrbitControls(camera, canvas);
  controls.enableDamping = true;
  controls.dampingFactor = .15;
  controls.minDistance = 4;
  controls.maxDistance = 64;
  controls.maxPolarAngle = 1.38;
  controls.minPolarAngle = .05;
  controls.mouseButtons.LEFT = null;
  controls.mouseButtons.RIGHT = THREE.MOUSE.ROTATE;
  controls.touches.ONE = null;
  controls.touches.TWO = THREE.TOUCH.DOLLY_ROTATE;
  const sun = new THREE.DirectionalLight(0xffe4b6, 3.0);
  sun.position.set(-12, 21, 9);
  sun.castShadow = true;
  sun.shadow.mapSize.set(2048, 2048);
  Object.assign(sun.shadow.camera, { left: -16, right: 16, top: 16, bottom: -16, near: .5, far: 60 });
  sun.shadow.bias = -.0002;
  sun.shadow.normalBias = .02;
  scene.add(sun, new THREE.HemisphereLight(0xdceffa, 0x79724f, 2.1));
  const floor = new THREE.Mesh(new THREE.PlaneGeometry(250, 250), material(0xb8c8a6));
  floor.rotation.x = -Math.PI / 2;
  floor.position.y = -.72;
  floor.receiveShadow = true;
  scene.add(floor);
  const base = new THREE.Mesh(new THREE.BoxGeometry(SIZE + .08, .25, SIZE + .08), material(0x665d42));
  base.position.y = -.54; base.castShadow = true; base.receiveShadow = true; scene.add(base);
  const root = new THREE.Group(); scene.add(root);
  const routeLine = new THREE.Line(new THREE.BufferGeometry(), new THREE.LineBasicMaterial({ color: 0xfff0a1 }));
  const routeTarget = new THREE.Mesh(new THREE.RingGeometry(.32, .40, 32), new THREE.MeshBasicMaterial({ color: 0xfff0a1, side: THREE.DoubleSide }));
  routeTarget.rotation.x = -Math.PI / 2;
  routeLine.name = "selected-route"; routeTarget.name = "route-destination";
  root.add(routeLine, routeTarget);
  const ground = new THREE.InstancedMesh(new THREE.BoxGeometry(1, 1, 1), material(0xffffff), SIZE * SIZE);
  ground.castShadow = true; ground.receiveShadow = true;
  root.add(ground);
  const time = { value: 0 };
  const waterMaterial = createWaterMaterial(time);
  const reflection = createSkyReflection(renderer);
  waterMaterial.envMap = reflection.texture;
  waterMaterial.envMapIntensity = .75;
  const water = new THREE.InstancedMesh(new THREE.BoxGeometry(.998, .035, .998, 5, 1, 5), waterMaterial, SIZE * SIZE);
  water.receiveShadow = true; root.add(water);
  const transform = new THREE.Object3D(), color = new THREE.Color();
  const tileGroups = new Map(), cropTemplates = new Map(), cropGroups = new Map(), itemGroups = new Map();
  const trees = new Map(), flagGroups = new Map(), workerGroups = new Map();
  const animalGroups = new Map();
  const markers = new Map();
  const interactables = [];
  let world = initialWorld, selected = "worker-1";
  let frame = 0, disposed = false, visible = true, overview = true;
  let drawingArea = false, displayRoute = null;
  const areaRenderer = createAreaRenderer(root);
  let cameraMotion = null, lastTime = performance.now();
  const abort = new AbortController();
  const options = { signal: abort.signal };

  function entity(group, id) {
    group.userData.entity = id;
    interactables.push(group);
    root.add(group);
    return group;
  }
  function label(id, text, className = "") {
    const marker = document.createElement("button");
    marker.type = "button";
    marker.className = `farm-marker ${className}`;
    marker.textContent = text;
    marker.dataset.farmMarker = id;
    marker.setAttribute("aria-label", `Select ${text}`);
    marker.addEventListener("click", () => callbacks.select(id), options);
    marker.addEventListener("pointerdown", event => beginDrag(event, id), options);
    board.append(marker);
    markers.set(id, marker);
    return marker;
  }

  const barn = createHouse(); barn.scale.multiplyScalar(1.45);
  barn.position.copy(worldPoint(world, world.barn)); entity(barn, "barn");
  const compost = createCompost(); compost.position.copy(worldPoint(world, world.compost)); entity(compost, "compost");
  const cow = createCow(); entity(cow, "cow");
  const chickens = new THREE.Group(); entity(chickens, "chickens");
  for (const animal of world.chickens) {
    const chicken = createChicken(); chicken.scale.setScalar(.75);
    chicken.userData.entity = animal.id; chickens.add(chicken);
    animalGroups.set(animal.id, { group: chicken, motion: new ActorMotion(worldPoint(world,animal),animal.direction) });
  }
  const fences = createFarmFences();root.add(fences.group);

  for (const tile of world.tiles.flat()) {
    const id = tile.y * SIZE + tile.x;
    const group = new THREE.Group(); group.position.copy(worldPoint(world, tile)); root.add(group); tileGroups.set(id, group);
    if (tile.tree) {
      const tree = createTree(id);
      tree.userData.entity = `tile-${id}`; interactables.push(tree);
      const stump = new THREE.Mesh(new THREE.CylinderGeometry(.10, .16, .19, 9), material(0x846241));
      stump.position.y = .095; stump.castShadow = true;
      group.add(tree, stump); trees.set(id, { tree, stump });
    }
    if (tile.type === "pond") {
      for (let edge = 0; edge < 4; edge++) {
        const [dx, dy] = [[0, -1], [1, 0], [0, 1], [-1, 0]][edge];
        if (tileAt(world, tile.x + dx, tile.y + dy)?.type === "pond") continue;
        const rock = new THREE.Mesh(new THREE.IcosahedronGeometry(.13, 0), material(0xaaa891));
        rock.position.set(dx * .51, .26, dy * .51);
        rock.scale.set(1.3, .7, 1);
        rock.castShadow = true; group.add(rock);
      }
    }
  }

  const selection = new THREE.Mesh(new THREE.RingGeometry(.34, .42, 32), new THREE.MeshBasicMaterial({ color: 0xffe4a0, side: THREE.DoubleSide }));
  selection.rotation.x = -Math.PI / 2; root.add(selection);
  const dropPreview = new THREE.Mesh(new THREE.PlaneGeometry(.96, .96), new THREE.MeshBasicMaterial({ color: 0xbcd975, transparent: true, opacity: .65, depthWrite: false }));
  dropPreview.rotation.x = -Math.PI / 2; dropPreview.visible = false; root.add(dropPreview);
  const rainPositions = new Float32Array(600 * 6);
  for (let i = 0; i < 600; i++) {
    const x = Math.sin(i * 54.3) * 11, z = Math.sin(i * 21.7) * 11, y = (i % 53) / 53 * 9;
    rainPositions.set([x, y, z, x - .05, y + .30, z], i * 6);
  }
  const rainGeometry = new THREE.BufferGeometry(); rainGeometry.setAttribute("position", new THREE.BufferAttribute(rainPositions, 3));
  const rain = new THREE.LineSegments(rainGeometry, new THREE.LineBasicMaterial({ color: 0xc9e0dd, transparent: true, opacity: .6 }));
  rain.name = "rain";rain.material.opacity = 0;rain.visible = false;
  let rainfall = 0;
  scene.add(rain);

  function update(nextWorld, selectedId = selected, stepDuration = .5, route = displayRoute) {
    displayRoute = route;
    areaRenderer.update(nextWorld);
    world = nextWorld; selected = selectedId;
    for (const tile of world.tiles.flat()) {
      const id = tile.y * SIZE + tile.x;
      const top = tile.height - (tile.type === "channel" ? .16 : 0);
      transform.position.set(tile.x - OFFSET, (top - .42) / 2, tile.y - OFFSET);
      transform.scale.set(1, top + .42, 1); transform.updateMatrix(); ground.setMatrixAt(id, transform.matrix);
      color.set(tile.type === "plowed" ? tile.moisture > 3 ? 0x725e3c : 0x9c7c4d
        : tile.type === "pond" || tile.type === "channel" ? 0x8e8761 : tile.type === "path" ? 0xbca276 : 0x809b59);
      color.multiplyScalar(.96 + .06 * Math.sin(id * 37.2));
      if (!unlocked(world, tile.x, tile.y)) color.multiplyScalar(.87);
      ground.setColorAt(id, color);
      transform.position.set(tile.x - OFFSET, .2825, tile.y - OFFSET);
      transform.scale.setScalar(tile.water ? 1 : 0); transform.updateMatrix(); water.setMatrixAt(id, transform.matrix);
      const group = tileGroups.get(id);
      if (tile.crop) {
        let entry = cropGroups.get(id);
        const key = `${tile.crop.kind}-${cropStage(tile.crop.growth)}`;
        if (!entry || entry.key !== key) {
          if (entry) group.remove(entry.group);
          if (!cropTemplates.has(key)) cropTemplates.set(key, createCrop(tile.crop.kind, cropStage(tile.crop.growth)));
          const crop = cropTemplates.get(key).clone(true);
          group.add(crop); entry = { group: crop, key }; cropGroups.set(id, entry);
        }
        entry.group.scale.set(1, .30 + tile.crop.growth * .70, 1);
      } else if (cropGroups.has(id)) { group.remove(cropGroups.get(id).group); cropGroups.delete(id); }
      const signature = tile.items.map(item => `${item.kind}:${item.count}`).join(",");
      if (signature !== (itemGroups.get(id)?.signature ?? "")) {
        const old = itemGroups.get(id); if (old) group.remove(old.group);
        const pile = new THREE.Group();
        tile.items.slice(0, 4).forEach((item, i) => {
          const mesh = item.kind === "eggs" ? createEggs(item.count) : createItem(item.kind); mesh.position.set((i % 2 - .5) * .23, 0, Math.floor(i / 2) * .24); pile.add(mesh);
        });
        group.add(pile); itemGroups.set(id, { group: pile, signature });
      }
      const tree = trees.get(id);
      if (tree) { tree.tree.visible = tile.tree.regrow === 0; tree.stump.visible = !tree.tree.visible; }
    }
    ground.instanceMatrix.needsUpdate = true; ground.instanceColor.needsUpdate = true; water.instanceMatrix.needsUpdate = true;
    for (const actor of actors(world)) {
      let entry = workerGroups.get(actor.id);
      if (!entry) {
        const group = createFarmer(actor.id === "player" ? 0xeee3bd : COLORS[world.workers.indexOf(actor) % COLORS.length]);
        group.position.copy(worldPoint(world, actor));
        const motion = new ActorMotion(group.position, actor.direction);
        group.rotation.y = motion.yaw;
        entry = { group: entity(group, actor.id), motion, target: motion.target, signature: "" };
        workerGroups.set(actor.id, entry);
        label(actor.id, actor.name, actor.id === "player" ? "farm-marker-player" : "");
      }
      if (!visible || document.hidden) entry.motion.place(worldPoint(world, actor), actor.direction);
      else entry.motion.retarget(worldPoint(world, actor), actor.direction, stepDuration);
      const signature = actor.inventory.map(item => item.kind).join(",");
      if (signature !== entry.signature) {
        equipFarmer(entry.group, actor.inventory);
        entry.signature = signature;
      }
      markers.get(actor.id).classList.toggle("is-selected", actor.id === selected);
      markers.get(actor.id).title = `${actor.name}: ${actor.status}`;
      markers.get(actor.id).textContent = actor.name;
      markers.get(actor.id).setAttribute("aria-label", `Select ${actor.name}`);
    }
    for (const [id, group] of flagGroups) {
      if (world.flags.some(flag => flag.id === id)) continue;
      if (drag?.id === `flag-${id}`) { drag = null; dropPreview.visible = false; }
      root.remove(group); interactables.splice(interactables.indexOf(group), 1); flagGroups.delete(id);
    }
    for (const flag of world.flags) {
      if (!flagGroups.has(flag.id)) flagGroups.set(flag.id, entity(createFlag(FLAGS.find(color => color.id === flag.id).color), `flag-${flag.id}`));
      if (drag?.id !== `flag-${flag.id}`) flagGroups.get(flag.id).position.copy(worldPoint(world, flag));
    }
    compost.userData.heap.scale.y = .10 + Math.min(.38, (world.compost.input + world.compost.ready) * .045);
    fences.update(world);
    cow.visible = world.cow !== null;
    for (const animal of animals(world)) {
      if (!animalGroups.has(animal.id)) animalGroups.set(animal.id, {group: cow, motion: new ActorMotion(worldPoint(world,animal),animal.direction)});
      const entry = animalGroups.get(animal.id);
      if (!visible || document.hidden) entry.motion.place(worldPoint(world,animal),animal.direction);
      else entry.motion.retarget(worldPoint(world,animal),animal.direction,stepDuration * 1.8);
    }
    renderer.shadowMap.needsUpdate = true;
    routeLine.visible = Boolean(route?.path?.length);
    routeTarget.visible = route !== null;
    if (route) {
      routeTarget.position.copy(worldPoint(world, route.target, .055));
      if (route.path !== null) {
        const actor = actors(world).find(actor => actor.id === selected);
        routeLine.geometry.dispose();
        routeLine.geometry = new THREE.BufferGeometry().setFromPoints([actor, ...route.path].map(point => worldPoint(world, point, .07)));
      }
    }
    board.dataset.tick = String(world.tick);
    requestFrame();
  }

  function focus(id = "farm", instant = false) {
    const target = id === "farm" ? new THREE.Vector3(0, .4, 0) : (workerGroups.get(id)?.target.clone() ?? worldPoint(world, world[id] ?? world.player)).add(new THREE.Vector3(0, .45, 0));
    let position;
    if (id === "farm") {
      let near = 22, far = 80;
      const framing = camera.clone(), point = new THREE.Vector3();
      for (let iteration = 0; iteration < 16; iteration++) {
        const distance = (near + far) / 2;
        position = target.clone().add(new THREE.Vector3(.48, .73, .60).normalize().multiplyScalar(distance));
        framing.position.copy(position); framing.lookAt(target); framing.updateMatrixWorld();
        let extent = 0;
        for (const x of [-10, 10]) for (const z of [-10, 10]) for (const y of [-.5, 2]) {
          point.set(x, y, z).project(framing);
          extent = Math.max(extent, Math.abs(point.x) / .96, Math.abs(point.y) / .86);
        }
        if (extent <= 1) far = distance; else near = distance;
      }
      position = target.clone().add(new THREE.Vector3(.48, .73, .60).normalize().multiplyScalar(far));
    } else {
      const actor = actors(world).find(actor => actor.id === id);
      const angle = actor ? [Math.PI, Math.PI / 2, 0, -Math.PI / 2][actor.direction] : .7;
      const occluders = [barn, compost, ...[...trees.values()].filter(entry => entry.tree.visible).map(entry => entry.tree)];
      const probe = new THREE.Raycaster();
      for (let i = 0; i < 12; i++) {
        const a = angle + i * Math.PI / 6;
        position = target.clone().add(new THREE.Vector3(Math.sin(a), .9, Math.cos(a)).normalize().multiplyScalar(7));
        let blocked = false;
        for (const offset of [-.15, .35]) {
          const subject = target.clone().add(new THREE.Vector3(0, offset, 0));
          probe.set(position, subject.clone().sub(position).normalize()); probe.far = position.distanceTo(subject) - .1;
          if (probe.intersectObjects(occluders, true).length) { blocked = true; break; }
        }
        if (!blocked) break;
      }
    }
    if (instant) { cameraMotion = null; camera.position.copy(position); controls.target.copy(target); controls.update(); }
    else cameraMotion = { start: performance.now(), from: camera.position.clone(), to: position, oldTarget: controls.target.clone(), target };
    overview = id === "farm";
    requestFrame();
  }
  function resize() {
    if (!board.clientWidth || !board.clientHeight) return;
    renderer.setSize(board.clientWidth, board.clientHeight, false);
    camera.aspect = board.clientWidth / board.clientHeight;
    camera.updateProjectionMatrix();
    if (overview) focus("farm", true);
    requestFrame();
  }
  const resizeObserver = new ResizeObserver(resize); resizeObserver.observe(board);
  const visibilityObserver = new IntersectionObserver(([entry]) => {
    visible = entry.isIntersecting;
    if (visible) { lastTime = performance.now(); requestFrame(); }
  });
  visibilityObserver.observe(board);
  const raycaster = new THREE.Raycaster(), pointer = new THREE.Vector2();
  function ray(event) {
    const bounds = canvas.getBoundingClientRect();
    pointer.set((event.clientX - bounds.left) / bounds.width * 2 - 1, 1 - (event.clientY - bounds.top) / bounds.height * 2);
    raycaster.setFromCamera(pointer, camera);
    return raycaster;
  }
  function groundPosition(event) {
    const hit = ray(event).intersectObject(ground, false)[0];
    if (!hit) return null;
    return { x: hit.instanceId % SIZE, y: Math.floor(hit.instanceId / SIZE) };
  }
  function hitEntity(event) {
    const hit = ray(event).intersectObjects(interactables, true).find(hit => {
      let object = hit.object;
      while (object) { if (!object.visible) return false; object = object.parent; }
      return true;
    });
    if (!hit) return null;
    let object = hit.object;
    while (!object.userData.entity) object = object.parent;
    return object.userData.entity;
  }
  const motionEntry = id => workerGroups.get(id) ?? animalGroups.get(id);
  let drag = null;
  function beginDrag(event, id) {
    if (event.button !== 0 || !event.isPrimary) return;
    drag = { pointerId: event.pointerId, id: drawingArea ? null : id, x: event.clientX, y: event.clientY, moved: false, tile: null, area: drawingArea, start: drawingArea ? groundPosition(event) : null };
    canvas.setPointerCapture(event.pointerId);
  }
  canvas.addEventListener("pointerdown", event => beginDrag(event, hitEntity(event)), options);
  canvas.addEventListener("pointermove", event => {
    if (!drag || drag.pointerId !== event.pointerId) return;
    if (drag.area) {
      drag.tile = groundPosition(event);
      if (drag.start && drag.tile) {
        const bounds = {left:Math.min(drag.start.x,drag.tile.x),top:Math.min(drag.start.y,drag.tile.y),right:Math.max(drag.start.x,drag.tile.x),bottom:Math.max(drag.start.y,drag.tile.y)};
        areaRenderer.preview(world,bounds,unlocked(world,drag.start.x,drag.start.y) && unlocked(world,drag.tile.x,drag.tile.y));
        requestFrame();
      }
      return;
    }
    if (Math.hypot(event.clientX - drag.x, event.clientY - drag.y) < 6 && !drag.moved) return;
    if (!drag.moved && draggableById(world, drag.id)) callbacks.grab(drag.id, true);
    drag.moved = true;
    if (!draggableById(world, drag.id) && !drag.id?.startsWith("flag-")) return;
    const position = groundPosition(event);
    drag.tile = position;
    if (!position) { dropPreview.visible = false; return; }
    const actor = draggableById(world, drag.id);
    const valid = actor ? walkable(world, position.x, position.y, actor) : canPlaceFlag(world, drag.id.slice(5), position.x, position.y);
    dropPreview.material.color.set(valid ? 0xb4db79 : 0xda7354);
    dropPreview.position.copy(worldPoint(world, position, .055));
    dropPreview.position.y = Math.max(.355, dropPreview.position.y); dropPreview.visible = true;
    const group = actor ? motionEntry(drag.id).group : flagGroups.get(drag.id.slice(5));
    group.position.copy(worldPoint(world, position, .28));
    requestFrame();
  }, options);
  function finishDrag(event, cancelled = false) {
    if (!drag || drag.pointerId !== event.pointerId) return;
    const ended = drag; drag = null; dropPreview.visible = false;
    if (ended.area) {
      areaRenderer.preview(world,null,false);
      const end = groundPosition(event);
      if (!cancelled && ended.start && end) callbacks.area(ended.start,end);
      requestFrame(); return;
    }
    if (draggableById(world, ended.id)) callbacks.grab(ended.id, false);
    if (!cancelled) {
      if (ended.moved && ended.tile && ended.id) callbacks.drop(ended.id, ended.tile);
      else if (!ended.moved) {
        if (ended.id) callbacks.select(ended.id);
        else { const position = groundPosition(event); if (position) callbacks.walk(position); }
      }
    }
    update(world, selected);
    if (ended.moved && draggableById(world, ended.id)) {
      const actor = draggableById(world, ended.id);
      const entry = motionEntry(ended.id);
      entry.motion.place(worldPoint(world, actor), actor.direction);
      entry.group.position.copy(entry.motion.position);
    }
  }
  canvas.addEventListener("pointerup", event => finishDrag(event), options);
  canvas.addEventListener("pointercancel", event => finishDrag(event, true), options);
  canvas.addEventListener("keydown", event => {
    if (["ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "w", "a", "s", "d"].includes(event.key)) {
      event.preventDefault(); if (!event.repeat) callbacks.key(event.key, true);
    } else if (event.key === "0") focus("farm");
  }, options);
  canvas.addEventListener("keyup", event => callbacks.key(event.key, false), options);
  canvas.addEventListener("blur", () => callbacks.key(null, false), options);
  window.addEventListener("blur", () => callbacks.key(null, false), options);
  controls.addEventListener("start", () => { overview = false; cameraMotion = null; });
  controls.addEventListener("change", requestFrame);
  const projection = new THREE.Vector3();
  function requestFrame() { if (!frame && !disposed && visible) frame = requestAnimationFrame(render); }
  function render(now) {
    frame = 0;
    const elapsed = (now - lastTime) / 1000, dt = Math.min(.08, elapsed);
    lastTime = now; time.value = now / 1000;
    if (cameraMotion) {
      const progress = Math.min(1, (now - cameraMotion.start) / 650), t = progress * progress * (3 - 2 * progress);
      camera.position.lerpVectors(cameraMotion.from, cameraMotion.to, t); controls.target.lerpVectors(cameraMotion.oldTarget, cameraMotion.target, t);
      if (progress === 1) cameraMotion = null;
    }
    let moving = false;
    for (const [id, entry] of workerGroups) {
      const dragging = drag?.id === id && drag.moved;
      const motion = entry.motion;
      if (!dragging) {
        moving = motion.advance(elapsed) || moving;
        entry.group.position.copy(motion.position);
        entry.group.position.y += Math.abs(Math.sin(motion.phase)) * .022 * motion.weight;
        entry.group.rotation.y = motion.yaw;
      }
      const weight = dragging ? 0 : motion.weight;
      entry.group.userData.legs.forEach((leg, i) => {
        leg.rotation.x = Math.sin(motion.phase + i * Math.PI) * .48 * weight;
      });
      const actor = actors(world).find(a => a.id === id);
      moving = poseFarmerArms(entry.group, actor.inventory, {
        time: now / 1000, phase: motion.phase, weight, chopping: actor.status.startsWith("Chopping"), dt
      }) || moving;
      projection.copy(entry.group.position).add(new THREE.Vector3(0, 1.25, 0)).project(camera);
      const marker = markers.get(id);
      marker.hidden = projection.z > 1 || Math.abs(projection.x) > .98 || Math.abs(projection.y) > .98;
      marker.style.left = `${(projection.x * .5 + .5) * 100}%`;
      marker.style.top = `${(-projection.y * .5 + .5) * 100}%`;
    }
    const selectedGroup = workerGroups.get(selected)?.group;
    selection.visible = Boolean(selectedGroup);
    if (selectedGroup) { selection.position.copy(workerGroups.get(selected).motion.position); selection.position.y += .018; }
    for (const [id, entry] of animalGroups) {
      if (drag?.id === id && drag.moved) continue;
      const {motion,group} = entry;
      moving = motion.advance(elapsed) || moving;
      group.position.copy(motion.position);group.rotation.y = motion.yaw;
      group.position.y += Math.abs(Math.sin(motion.phase)) * .012 * motion.weight;
      group.userData.legs.forEach((leg,i) => {leg.rotation.x = Math.sin(motion.phase+i*Math.PI)*.35*motion.weight;});
      if (id === "cow") group.userData.tail.rotation.z = Math.sin(now*.002)*.18;
      else group.userData.head.rotation.x = Math.max(0,Math.sin(now*.003+Number(id.slice(-1))*2))*.45*(1-motion.weight);
    }
    moving = fences.animate([...workerGroups.values()].map(entry=>entry.group.position),dt) || moving;
    const weatherChange = ((world.raining ? 1 : 0)-rainfall)*(1-Math.exp(-elapsed/2));
    rainfall += weatherChange;
    rain.visible = rainfall > .002;rain.material.opacity = rainfall*.6;
    sun.intensity = 3-rainfall*1.5;
    moving ||= Math.abs(weatherChange) > .0001;
    for (const flag of world.flags) {
      const group = flagGroups.get(flag.id);
      if (drag?.id !== `flag-${flag.id}`) group.position.copy(worldPoint(world, flag));
      group.userData.cloth.forEach(cloth => { cloth.rotation.y = Math.sin(now * .002 + flag.x) * .10; });
    }
    if (rain.visible) {
      for (let i = 0; i < 600; i++) {
        let y = rainPositions[i * 6 + 1] - dt * 7;
        if (y < .4) y += 9;
        rainPositions[i * 6 + 1] = y; rainPositions[i * 6 + 4] = y + .3;
      }
      rainGeometry.attributes.position.needsUpdate = true;
    }
    if (moving || drag?.moved) renderer.shadowMap.needsUpdate = true;
    controls.update(); renderer.render(scene, camera); board.dataset.rendered = "true"; requestFrame();
  }
  update(world); resize(); focus("farm", true);
  return {
    update, focus, renderer, scene, camera,
    setAreaDrawing(value) { drawingArea = value; canvas.style.cursor = value ? "crosshair" : ""; board.dataset.drawingArea = String(value); areaRenderer.preview(world,null,false); requestFrame(); },
    dispose() {
      disposed = true; cancelAnimationFrame(frame); abort.abort(); resizeObserver.disconnect(); visibilityObserver.disconnect(); controls.dispose();
      areaRenderer.dispose();
      const geometries = new Set(), materials = new Set();
      const collect = object => { if (object.geometry) geometries.add(object.geometry); if (object.material) materials.add(object.material); };
      scene.traverse(collect); for (const template of cropTemplates.values()) template.traverse(collect);
      for (const geometry of geometries) geometry.dispose(); for (const mat of materials) mat.dispose();
      reflection.dispose(); renderer.dispose(); board.replaceChildren();
    }
  };
}
