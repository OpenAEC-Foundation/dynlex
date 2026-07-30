const CUES = Object.freeze({
  boat: Object.freeze({
    url: new URL("./media/river-challenge/boat-whoosh.mp3", import.meta.url),
    volume: 0.38,
    pitch: 1
  }),
  idle: Object.freeze({
    url: new URL("./media/river-challenge/sheep-idle.mp3", import.meta.url),
    volume: 0.16,
    pitch: 1
  }),
  anxious: Object.freeze({
    url: new URL("./media/river-challenge/sheep-anxious.mp3", import.meta.url),
    volume: 0.52,
    pitch: 2 ** (3 / 12)
  }),
  rowing: Object.freeze({
    url: new URL("./media/river-challenge/rowing-paddle.mp3", import.meta.url),
    volume: 0.34,
    pitch: 1
  }),
  win: Object.freeze({
    url: new URL("./media/river-challenge/win-level-up.mp3", import.meta.url),
    volume: 0.46,
    pitch: 1
  })
});

const AMBIENCE = Object.freeze({
  url: new URL(
    "./media/river-challenge/forest-river-ambience-loop.mp3",
    import.meta.url
  ),
  volume: 0.28
});

const INITIAL_IDLE_DELAY = 4000;
const MINIMUM_IDLE_INTERVAL = 8000;
const IDLE_INTERVAL_VARIATION = 5000;

async function loadAudioBuffer(context, name, url) {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`River challenge ${name} sound returned HTTP ${response.status}`);
  }
  return context.decodeAudioData(await response.arrayBuffer());
}

async function loadCue(context, name, cue) {
  const buffer = await loadAudioBuffer(context, name, cue.url);
  return [name, Object.freeze({ ...cue, buffer })];
}

function isVisible(element) {
  const bounds = element.getBoundingClientRect();
  return (
    bounds.bottom > 0
    && bounds.right > 0
    && bounds.top < window.innerHeight
    && bounds.left < window.innerWidth
  );
}

export async function createRiverChallengeAudio(section, music, muteButton, sheep) {
  if (!(section instanceof HTMLElement)) {
    throw new TypeError("River challenge audio requires its section");
  }
  if (!(muteButton instanceof HTMLButtonElement)) {
    throw new TypeError("River challenge audio requires its mute button");
  }
  if (!(sheep instanceof HTMLElement) || sheep.dataset.riverCharacter !== "SHEEP") {
    throw new TypeError("River challenge audio requires its sheep");
  }
  const AudioContextClass = window.AudioContext || window.webkitAudioContext;
  const context = music?.context;
  if (!AudioContextClass || !(context instanceof AudioContextClass)) {
    throw new TypeError("River challenge audio requires an AudioContext");
  }

  const [, ambienceBuffer, cueEntries] = await Promise.all([
    music.started,
    loadAudioBuffer(context, "forest-river ambience", AMBIENCE.url),
    Promise.all(Object.entries(CUES).map(([name, cue]) => loadCue(context, name, cue)))
  ]);
  const cues = new Map(cueEntries);
  const effectsGain = context.createGain();
  effectsGain.connect(context.destination);
  const ambienceSource = context.createBufferSource();
  const ambienceLevel = context.createGain();
  const ambienceAudibility = context.createGain();
  ambienceSource.buffer = ambienceBuffer;
  ambienceSource.loop = true;
  ambienceLevel.gain.value = AMBIENCE.volume;
  ambienceAudibility.gain.value = 0;
  ambienceSource.connect(ambienceLevel);
  ambienceLevel.connect(ambienceAudibility);
  ambienceAudibility.connect(context.destination);
  ambienceSource.start();

  let visible = isVisible(section);
  let muted = false;
  let destroyed = false;
  let idleEnabled = true;
  let idleTimer = null;
  let idleHandle = null;
  let tracePaused = true;
  let traceSpeed = 1;
  const activeSources = new Set();
  const activeTraceSources = new Set();
  const bellowingSources = new Set();

  function clearIdleTimer() {
    if (idleTimer !== null) {
      clearTimeout(idleTimer);
      idleTimer = null;
    }
  }

  function audible() {
    return visible && !document.hidden && !muted;
  }

  function cueNamed(name) {
    const cue = cues.get(name);
    if (!cue) {
      throw new Error(`Unknown river challenge sound: ${name}`);
    }
    return cue;
  }

  function syncSheepMouth() {
    sheep.classList.toggle("is-bellowing", bellowingSources.size > 0);
  }

  function playCue(name, trackTrace, ended) {
    if (destroyed) {
      throw new Error("River challenge audio was already destroyed");
    }
    const cue = cueNamed(name);
    const source = context.createBufferSource();
    const cueGain = context.createGain();
    const baseRate = cue.pitch;
    const record = { baseRate, cueGain, finish: null, source, stopped: false };
    source.buffer = cue.buffer;
    source.playbackRate.value = trackTrace
      ? (tracePaused ? 0 : baseRate * traceSpeed)
      : baseRate;
    cueGain.gain.value = cue.volume;
    source.connect(cueGain);
    cueGain.connect(effectsGain);
    activeSources.add(record);
    if (trackTrace) {
      activeTraceSources.add(record);
    }
    if (name === "idle" || name === "anxious") {
      bellowingSources.add(record);
      syncSheepMouth();
    }

    const finish = () => {
      if (record.stopped) {
        return;
      }
      record.stopped = true;
      activeSources.delete(record);
      activeTraceSources.delete(record);
      bellowingSources.delete(record);
      syncSheepMouth();
      source.disconnect();
      cueGain.disconnect();
      ended();
    };
    record.finish = finish;
    source.addEventListener("ended", finish, { once: true });
    source.start();

    return Object.freeze({
      stop() {
        if (!record.stopped) {
          source.stop();
          finish();
        }
      }
    });
  }

  function scheduleIdle(initial = false) {
    clearIdleTimer();
    if (!idleEnabled || idleHandle !== null || !audible() || destroyed) {
      return;
    }
    const delay = initial
      ? INITIAL_IDLE_DELAY
      : MINIMUM_IDLE_INTERVAL + Math.random() * IDLE_INTERVAL_VARIATION;
    idleTimer = window.setTimeout(() => {
      idleTimer = null;
      idleHandle = playCue("idle", false, () => {
        idleHandle = null;
        scheduleIdle();
      });
    }, delay);
  }

  function syncAudibility() {
    const isAudible = audible();
    if (isAudible && context.state === "suspended") {
      void context.resume().catch((error) => {
        console.error("River challenge audio could not resume", error);
      });
    }
    const now = context.currentTime;
    const musicLevel = music.gain.gain;
    musicLevel.cancelScheduledValues(now);
    musicLevel.setValueAtTime(musicLevel.value, now);
    musicLevel.linearRampToValueAtTime(isAudible ? 0.34 : 0, now + 0.45);
    const ambienceState = ambienceAudibility.gain;
    ambienceState.cancelScheduledValues(now);
    ambienceState.setValueAtTime(ambienceState.value, now);
    ambienceState.linearRampToValueAtTime(isAudible ? 1 : 0, now + 0.45);
    effectsGain.gain.cancelScheduledValues(now);
    effectsGain.gain.setValueAtTime(isAudible ? 1 : 0, now);
    section.dataset.musicState = isAudible ? "audible" : "dampened";
    section.dataset.ambienceState = isAudible ? "audible" : "dampened";
    section.dataset.soundState = isAudible ? "audible" : "silent";
    muteButton.setAttribute("aria-pressed", String(muted));
    muteButton.innerHTML = muted
      ? '<span aria-hidden="true">♪</span> AUDIO OFF'
      : '<span aria-hidden="true">♪</span> AUDIO ON';
    if (isAudible) {
      scheduleIdle(true);
    } else {
      clearIdleTimer();
    }
  }

  const observer = new IntersectionObserver(([entry]) => {
    visible = entry.isIntersecting && entry.intersectionRatio >= 0.12;
    syncAudibility();
  }, { threshold: [0, 0.12, 0.4] });
  const visibilityChange = () => syncAudibility();
  const toggleMute = () => {
    muted = !muted;
    syncAudibility();
  };

  function stopRecord(record) {
    if (!record.stopped) {
      record.source.stop();
      record.finish();
    }
  }

  function destroy() {
    if (destroyed) {
      return;
    }
    destroyed = true;
    clearIdleTimer();
    observer.disconnect();
    document.removeEventListener("visibilitychange", visibilityChange);
    muteButton.removeEventListener("click", toggleMute);
    for (const record of [...activeSources]) {
      stopRecord(record);
    }
    ambienceSource.stop();
    music.audio.pause();
    ambienceSource.disconnect();
    ambienceLevel.disconnect();
    ambienceAudibility.disconnect();
    effectsGain.disconnect();
    void context.close();
  }

  observer.observe(section);
  document.addEventListener("visibilitychange", visibilityChange);
  muteButton.addEventListener("click", toggleMute);
  window.addEventListener("pagehide", destroy, { once: true });
  syncAudibility();

  return Object.freeze({
    playOneShot(name) {
      return playCue(name, false, () => undefined);
    },
    playTraceEffect(name, signal) {
      if (!(signal instanceof AbortSignal)) {
        throw new TypeError("A river trace sound requires its abort signal");
      }
      const handle = playCue(name, true, () => {
        signal.removeEventListener("abort", handle.stop);
      });
      signal.addEventListener("abort", handle.stop, { once: true });
      return Object.freeze({
        stop() {
          signal.removeEventListener("abort", handle.stop);
          handle.stop();
        }
      });
    },
    setIdleEnabled(enabled) {
      if (typeof enabled !== "boolean") {
        throw new TypeError("River sheep idle state must be boolean");
      }
      idleEnabled = enabled;
      if (!enabled) {
        clearIdleTimer();
        idleHandle?.stop();
      } else {
        scheduleIdle(true);
      }
    },
    setTracePlayback({ paused, speed }) {
      if (typeof paused !== "boolean" || ![1, 2, 4].includes(speed)) {
        throw new TypeError("Invalid river sound playback state");
      }
      tracePaused = paused;
      traceSpeed = speed;
      const now = context.currentTime;
      for (const record of activeTraceSources) {
        record.source.playbackRate.cancelScheduledValues(now);
        record.source.playbackRate.setValueAtTime(
          paused ? 0 : record.baseRate * speed,
          now
        );
      }
    },
    stopTraceEffects() {
      for (const record of [...activeTraceSources]) {
        stopRecord(record);
      }
    }
  });
}
