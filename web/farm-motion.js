import { Vector3 } from "./vendor/three/three.module.min.js";

const headings = [Math.PI, Math.PI / 2, 0, -Math.PI / 2];
const direction = new Vector3();
const angleDifference = (a, b) => Math.atan2(Math.sin(a - b), Math.cos(a - b));

// A small visual delay lets the next simulation step arrive before the current
// step ends. Waypoints keep a steady stride across tiles, including at 2× pace.
export class ActorMotion {
  constructor(position, facing) {
    this.position = position.clone();
    this.target = position.clone();
    this.place(position, facing);
  }

  place(position, facing) {
    this.position.copy(position); this.target.copy(position);
    this.waypoints = [];
    this.yaw = this.heading = headings[facing];
    this.speed = this.weight = this.phase = this.delay = 0;
  }

  retarget(position, facing, duration) {
    this.heading = headings[facing];
    if (position.distanceToSquared(this.target) < 1e-10) return;
    const distance = position.distanceTo(this.target);
    if (!this.waypoints.length) this.delay = .10;
    direction.subVectors(position, this.target);
    const backward = direction.x * Math.sin(this.heading) + direction.z * Math.cos(this.heading) < -distance * .5;
    this.waypoints.push({ position: position.clone(), speed: distance / duration, backward });
    this.target.copy(position);
  }

  advance(elapsed) {
    let active = false;
    while (elapsed > 1e-9) {
      const dt = Math.min(elapsed, 1 / 120);
      const moving = this.advanceStep(dt);
      active = moving || active;
      elapsed -= dt;
      if (!moving && !this.waypoints.length) break;
    }
    return active;
  }

  advanceStep(dt) {
    const before = this.position.clone();
    let heading = this.heading;
    let backward = false;
    if (this.delay > 0) this.delay = Math.max(0, this.delay - dt);
    else if (this.waypoints.length) {
      const first = this.waypoints[0];
      backward = first.backward;
      let remaining = this.position.distanceTo(first.position);
      for (let i = 1; i < this.waypoints.length; i++) remaining += this.waypoints[i - 1].position.distanceTo(this.waypoints[i].position);
      const acceleration = first.speed / .10;
      const wanted = Math.min(first.speed, Math.sqrt(2 * acceleration * remaining));
      this.speed += Math.max(-acceleration * dt, Math.min(acceleration * dt, wanted - this.speed));
      let travel = this.speed * dt;
      while (travel > 0 && this.waypoints.length) {
        const next = this.waypoints[0].position;
        const distance = this.position.distanceTo(next);
        if (travel >= distance) {
          this.position.copy(next); travel -= distance; this.waypoints.shift();
        } else { this.position.lerp(next, travel / distance); travel = 0; }
      }
      if (!this.waypoints.length) this.speed = 0;
    }
    direction.subVectors(this.position, before);
    const distance = direction.length();
    if (distance > 1e-6) heading = Math.atan2(direction.x, direction.z) + (backward ? Math.PI : 0);
    const turn = angleDifference(heading, this.yaw);
    this.yaw += turn * (1 - Math.exp(-dt * 14));
    this.phase += distance * Math.PI * 2 / .85 * (backward ? -1 : 1);
    this.weight += ((distance > 1e-6 ? 1 : 0) - this.weight) * (1 - Math.exp(-dt * 16));
    return distance > 0 || this.weight > .001 || Math.abs(turn) > .001;
  }
}
