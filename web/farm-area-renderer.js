import * as THREE from "./vendor/three/three.module.min.js";
import { SIZE } from "./farm-world.js";

// Small strips follow each tile's elevation and use the scene's depth buffer.
export function createAreaRenderer(root) {
  const saved = new THREE.Group(), preview = new THREE.Group(); root.add(saved,preview);
  const geometry = new THREE.BoxGeometry(1,1,1);
  let signature = "", previewSignature = "";
  function clear(group) {
    for (const material of new Set(group.children.map(mesh => mesh.material))) material.dispose();
    group.clear();
  }
  function draw(group,world,area,color,temporary,layer = 0) {
    const fill = new THREE.MeshBasicMaterial({ color, transparent:true, opacity:temporary ? .3 : .12, depthWrite:false });
    const edge = new THREE.MeshBasicMaterial({ color });
    const offset = (SIZE-1)/2;
    function box(x,y,z,width,height,depth,material) {
      const mesh = new THREE.Mesh(geometry,material); mesh.position.set(x,y,z); mesh.scale.set(width,height,depth); group.add(mesh);
    }
    for (let y=area.top;y<=area.bottom;y++) for(let x=area.left;x<=area.right;x++) {
      const tile = world.tiles[y][x], height = (tile.type === "pond" ? .32 : tile.height-(tile.type === "channel" ? .16 : 0)) + layer*.001 + (temporary ? .008 : 0);
      box(x-offset,height+.018,y-offset,.98,.004,.98,fill);
      if (x===area.left) box(x-offset-.47,height+.033,y-offset,.045,.025,1,edge);
      if (x===area.right) box(x-offset+.47,height+.033,y-offset,.045,.025,1,edge);
      if (y===area.top) box(x-offset,height+.033,y-offset-.47,1,.025,.045,edge);
      if (y===area.bottom) box(x-offset,height+.033,y-offset+.47,1,.025,.045,edge);
    }
  }
  return {
    update(world) {
      const next = JSON.stringify([world.areas,world.tiles.flat().filter(t=>t.type==="channel").map(t=>[t.x,t.y])]);
      if (next === signature) return;
      signature = next; clear(saved);
      world.areas.forEach((area,index) => draw(saved,world,area,area.color,false,index));
    },
    preview(world,bounds,valid) {
      const next = JSON.stringify([bounds,valid]); if (next === previewSignature) return;
      previewSignature = next; clear(preview); if (bounds) draw(preview,world,bounds,valid ? "#739442" : "#ca594c",true);
    },
    dispose() { clear(saved); clear(preview); geometry.dispose(); root.remove(saved,preview); }
  };
}
