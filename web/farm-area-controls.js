import { addArea, reshapeArea, removeArea, AreaInputError } from "./farm-areas.js";

export function createAreaControls(mount, getWorld, setDrawing, changed, events) {
  mount.className = "farm-areas";
  mount.innerHTML = `<details><summary>Work areas</summary><p>Name a field, then drag a rectangle across the farm. Workers can search for jobs inside it.</p>
    <div class="farm-area-new"><input data-area-name placeholder="For example: Vegetable patch" aria-label="New work area name" maxlength="40"><button data-area-draw>DRAW AREA</button><button data-area-cancel hidden>CANCEL</button></div>
    <p data-area-status role="status"></p><div data-area-list></div></details>`;
  const name = mount.querySelector("[data-area-name]"), status = mount.querySelector("[data-area-status]"), cancelButton = mount.querySelector("[data-area-cancel]");
  const list = mount.querySelector("[data-area-list]");
  let drawing = null, signature = null;
  function cancel() { drawing = null; setDrawing(false); cancelButton.hidden = true; status.textContent = ""; }
  function begin(id, areaName) {
    if (!areaName) { name.focus(); status.textContent = "Give the area a name first."; return; }
    if (id === null && getWorld().areas.some(area => area.name === areaName)) { status.textContent = "An area already has that name. Use Redraw to move its boundaries."; return; }
    drawing = { id, name: areaName }; setDrawing(true); cancelButton.hidden = false;
    status.textContent = `Drag a rectangle for “${areaName}” on the farm.`;
  }
  mount.querySelector("[data-area-draw]").addEventListener("click", () => begin(null,name.value.trim()), events);
  cancelButton.addEventListener("click", cancel, events);
  list.addEventListener("click", event => {
    const button = event.target.closest("button");
    if (!button) return;
    const area = getWorld().areas.find(area => area.id === button.dataset.areaId);
    if (button.dataset.action === "redraw") begin(area.id,area.name);
    else { if (drawing?.id === area.id) cancel(); removeArea(getWorld(),area.id); changed(); }
  }, events);
  return {
    cancel,
    finish(start,end) {
      try {
        if (drawing.id === null) addArea(getWorld(),drawing.name,start,end);
        else reshapeArea(getWorld(),drawing.id,start,end);
        cancel(); name.value = ""; changed();
      } catch (error) {
        if (error instanceof AreaInputError) status.textContent = error.message;
        else { console.error("Work area failed",error); status.textContent = "An error occurred. Check the browser log."; }
      }
    },
    update() {
      const areas = getWorld().areas, next = JSON.stringify(areas);
      if (next === signature) return;
      signature = next;
      list.replaceChildren(...areas.map(area => {
        const row = document.createElement("div"); row.className = "farm-area-row"; row.dataset.area = area.id;
        row.style.setProperty("--area-color",area.color);
        const title = document.createElement("strong"); title.textContent = `${area.name} · ${area.right-area.left+1} × ${area.bottom-area.top+1}`;
        const code = document.createElement("code"); code.textContent = `in the area ${JSON.stringify(area.name)}`;
        row.append(title,code);
        for (const action of ["redraw","delete"]) {
          const button = document.createElement("button"); button.dataset.areaId = area.id; button.dataset.action = action; button.textContent = action.toUpperCase();
          button.setAttribute("aria-label",`${action} ${area.name}`); row.append(button);
        }
        return row;
      }));
    }
  };
}
