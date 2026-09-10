import * as THREE from "./vendor/three/three.module.min.js";
import { FENCES } from "./farm-boundaries.js";
import { piece } from "./farm-art.js";

export function createFarmFences() {
  const group = new THREE.Group(), gates = [], sections = [], posts = new Map();
  const box = new THREE.BoxGeometry(1,1,1), cap = new THREE.ConeGeometry(1,1,4);
  for (const fence of FENCES) {
    const section = new THREE.Group();
    section.position.set(fence.start[0]-9.5,.43,fence.start[1]-9.5);
    const heading = -Math.atan2(fence.end[1]-fence.start[1],fence.end[0]-fence.start[0]);
    section.rotation.y = heading;
    const rails = new THREE.Group();section.add(rails);
    for (const y of [.22,.43]) piece(rails,box,0xd8c18a,[.5,y,0],[.96,.065,.055]);
    if (fence.gate) {
      piece(rails,box,0xb19a65,[.5,.325,0],[.97,.045,.055],[0,0,.22]);
      piece(rails,box,0x534c36,[.90,.40,.04],[.07,.03,.03]);
      gates.push({rails,fence});
    }
    group.add(section);sections.push({section,fence});
    for (const point of [fence.start,fence.end]) {
      const key = point.join(",");
      let post = posts.get(key);
      if (!post) {
        const mesh = new THREE.Group();mesh.position.set(point[0]-9.5,.43,point[1]-9.5);
        piece(mesh,box,0xc4ae78,[0,.29,0],[.07,.58,.07]);
        piece(mesh,cap,0xe2cca0,[0,.605,0],[.052,.05,.052],[0,Math.PI/4,0]);
        group.add(mesh);post={mesh,fences:[]};posts.set(key,post);
      }
      post.fences.push(fence);
    }
  }
  return {
    group,
    update(world) {
      for (const {section,fence} of sections) section.visible = !fence.locked || !world.expanded;
      for (const {mesh,fences} of posts.values()) mesh.visible = fences.some(f => !f.locked || !world.expanded);
    },
    animate(people,dt) {
      let moving=false;
      for (const {rails,fence} of gates) {
        const x=(fence.start[0]+fence.end[0])/2-9.5, z=(fence.start[1]+fence.end[1])/2-9.5;
        const open = people.some(p=>Math.hypot(p.x-x,p.z-z)<1.25);
        const difference=(open ? -Math.PI/2 : 0)-rails.rotation.y;
        rails.rotation.y+=difference*(1-Math.exp(-dt*8));
        moving ||= Math.abs(difference)>.001;
      }
      return moving;
    }
  };
}
