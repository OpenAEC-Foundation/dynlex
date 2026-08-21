const SVG_NAMESPACE = "http://www.w3.org/2000/svg";
const WOLF_TONGUE_PATH = "M0 0 L30 2 A9 18 0 0 1 30 38 L0 40 Z";
const WOLF_MOUTH_POINTS = Object.freeze([
  [30.801, 61.471, 154.359], [31.563, 62.915, 150.033], [32.421, 64.281, 145.688],
  [33.37, 65.565, 141.32], [34.406, 66.762, 136.925], [35.523, 67.867, 132.498],
  [36.715, 68.877, 128.03], [37.977, 69.787, 123.515], [39.304, 70.592, 118.946],
  [40.691, 71.287, 114.317], [42.13, 71.868, 109.624], [43.616, 72.329, 104.867],
  [45.142, 72.667, 100.048], [46.702, 72.875, 95.175], [48.287, 72.95, 90.258],
  [49.89, 72.888, 85.313], [51.38, 72.704, 82.451], [52.117, 72.63, 82.41],
  [53.047, 72.475, 78.546], [53.946, 72.26, 74.623], [54.813, 71.99, 70.672],
  [55.644, 71.665, 66.724], [56.439, 71.291, 62.81], [57.193, 70.871, 58.959],
  [57.907, 70.408, 55.196], [58.58, 69.908, 51.542], [59.21, 69.375, 48.013],
  [59.799, 68.812, 44.619], [60.348, 68.224, 41.365], [60.856, 67.614, 38.255],
  [61.325, 66.986, 35.285], [61.757, 66.343, 32.451], [62.152, 65.687, 29.745]
]);
const SWEEP_START = 0.18;
const SWEEP_END = 0.82;

function tonguePositionFrame([left, top, angle], offset) {
  return Object.freeze({
    left: `${left}%`,
    top: `${top}%`,
    transform: `translate(-50%, -50%) rotate(${angle}deg)`,
    offset
  });
}

const rightToLeftPoints = [...WOLF_MOUTH_POINTS].reverse();
const WOLF_TONGUE_POSITION_KEYFRAMES = Object.freeze([
  tonguePositionFrame(rightToLeftPoints[0], 0),
  ...rightToLeftPoints.map((point, index) => tonguePositionFrame(
    point,
    SWEEP_START + (SWEEP_END - SWEEP_START) * index / (rightToLeftPoints.length - 1)
  )),
  tonguePositionFrame(rightToLeftPoints.at(-1), 1)
]);
const WOLF_TONGUE_EXTENSION_KEYFRAMES = Object.freeze([
  Object.freeze({ transform: "scaleX(0)", opacity: 0, offset: 0 }),
  Object.freeze({ transform: "scaleX(1)", opacity: 1, offset: SWEEP_START }),
  Object.freeze({ transform: "scaleX(1)", opacity: 1, offset: SWEEP_END }),
  Object.freeze({ transform: "scaleX(0)", opacity: 0, offset: 1 })
]);

export const WOLF_TONGUE_LICK_DURATION = 850;

export function createWolfTongue() {
  const tongue = document.createElement("i");
  tongue.className = "river-wolf-tongue";
  tongue.setAttribute("aria-hidden", "true");
  const image = document.createElementNS(SVG_NAMESPACE, "svg");
  image.setAttribute("viewBox", "0 0 39 40");
  image.setAttribute("focusable", "false");
  const shape = document.createElementNS(SVG_NAMESPACE, "path");
  shape.classList.add("river-wolf-tongue-shape");
  shape.setAttribute("d", WOLF_TONGUE_PATH);
  image.append(shape);
  tongue.append(image);
  return tongue;
}

export function createWolfTongueLickSpecifications(tongue) {
  const image = tongue.querySelector("svg");
  if (image === null) throw new Error("Wolf tongue is missing its SVG image");
  return [
    { element: tongue, keyframes: WOLF_TONGUE_POSITION_KEYFRAMES, easing: "linear" },
    { element: image, keyframes: WOLF_TONGUE_EXTENSION_KEYFRAMES, easing: "linear" }
  ];
}
