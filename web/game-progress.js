import { FARM_GOALS, RIVER_GOALS, newGoalProgress } from "./coding-goals.js";
import {upgradeFarmLanguage,upgradeFarmCollection} from "./farm-language-upgrade.js";

export const SAVE_VERSION = 7;
const PREFIX = "dynlex.games.";
export const emptyProgress = () => ({ version: SAVE_VERSION, games: {}, lastGame: null });

// Save-format changes belong here, before either game receives its state.
export function readProgress(text) {
  const save = JSON.parse(text);
  if (save.version === 1) {
    for (const chicken of save.games.farm?.data.world.chickens ?? []) chicken.held = false;
    save.version = 2;
  }
  if (save.version === 2) {
    if (save.games.farm) {
      const world = save.games.farm.data.world;
      for (const actor of [world.player, ...world.workers]) actor.paused = false;
    }
    save.version = 3;
  }
  if (save.version === 3) {
    if (save.games.farm) {
      const { world, roles } = save.games.farm.data;
      for (const actor of [world.player, ...world.workers]) actor.routine.trace = null;
      for (const role of roles) {
        role.needsSourceMap = role.program !== null;
        if (role.program !== null) for (const instruction of role.program) instruction.range = null;
      }
    }
    save.version = 4;
  }
  if (save.version === 4) {
    if (save.games.farm) {
      const {world,roles} = save.games.farm.data;
      world.areas = [];
      world.coding = newGoalProgress(FARM_GOALS);
      for (const role of roles) if (role.program !== null) {
        role.needsSourceMap = true;
        for (const instruction of role.program) {
          if (instruction.op === "test") instruction.amount = 0;
          if (instruction.op === "action" && instruction.key === "walk") instruction.area = "";
        }
      }
    }
    if (save.games.river) save.games.river.data.coding = newGoalProgress(RIVER_GOALS);
    save.version = 5;
  }
  if (save.version === 5) {
    if (save.games.farm) upgradeFarmLanguage(save.games.farm.data);
    save.version=6;
  }
  if (save.version === 6) {
    if (save.games.farm) upgradeFarmCollection(save.games.farm.data);
    save.version=7;
  }
  if (save.version !== SAVE_VERSION) throw new Error(`Unsupported game save version: ${save.version}`);
  if (!save.games || typeof save.games !== "object" || ![null,"river","farm"].includes(save.lastGame)) throw new Error("Invalid game save");
  return save;
}

export function createProgressStore(storage, onError = console.error) {
  const listeners = new Set();
  let profile = "guest", blocked = false;
  const read = id => {
    const text = storage.getItem(PREFIX+id);
    return text === null ? emptyProgress() : readProgress(text);
  };
  let progress = emptyProgress();
  try {
    for (let index = 0; index < storage.length; index++) {
      const key = storage.key(index);
      if (key === `${PREFIX}guest` || key.startsWith(`${PREFIX}user:`) || key.startsWith(`${PREFIX}backup:`)) {
        const text = storage.getItem(key), migrated = JSON.stringify(readProgress(text));
        if (migrated !== text) storage.setItem(key, migrated);
      }
    }
    profile = storage.getItem(`${PREFIX}profile`) ?? "guest";progress = read(profile); }
  catch(error) {blocked=true;onError(error);}
  let suspended = false;
  const notify = reason => listeners.forEach(listener => listener(reason));
  function persist(next) {
    storage.setItem(PREFIX+profile, JSON.stringify(next)); progress = next;
  }
  return {
    get profile() { return profile; },
    get snapshot() { return structuredClone(progress); },
    load(game) { return structuredClone(progress.games[game]?.data ?? null); },
    save(game, data) {
      if (suspended || blocked) return;
      const next = structuredClone(progress);
      const previous = next.games[game];
      if (previous && JSON.stringify(previous.data) === JSON.stringify(data) && next.lastGame === game) return;
      next.games[game] = { updatedAt: Math.max(Date.now(), (previous?.updatedAt ?? 0)+1), data: structuredClone(data) };
      next.lastGame = game;
      try {persist(next);notify("save");}
      catch(error) {onError(error);}
    },
    switchProfile(id) {
      const next = read(id);
      storage.setItem(`${PREFIX}profile`,id);
      profile=id;progress=next;notify("profile");
    },
    replace(next) {
      persist(readProgress(JSON.stringify(next)));notify("replace");
    },
    suspend() { suspended = true; },
    subscribe(listener) { listeners.add(listener);return () => listeners.delete(listener); }
  };
}

export function mergeProgress(local, remote) {
  const merged = emptyProgress();
  let latest = -1;
  for (const game of ["river","farm"]) {
    const a=local.games[game],b=remote.games[game];
    const entry = !a ? b : !b || a.updatedAt >= b.updatedAt ? a : b;
    if (!entry) continue;
    merged.games[game] = structuredClone(entry);
    if (entry.updatedAt > latest) { latest=entry.updatedAt;merged.lastGame=game; }
  }
  return merged;
}
