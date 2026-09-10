const lumberjack = `# Bring logs back, keeping one hand for the axe.
loop forever:
    walk to the barn
    store everything
    grab an axe
    walk to the nearest tree
    if i can reach it:
        chop it down
        pick up nearby items`;

export function createRoles() {
  return [{ id: "lumberjack", name: "Lumberjack", source: lumberjack, applied: "", program: null, generation: 0, needsRebuild: false }];
}

export function createRole(name, source = "") {
  return { id: `custom-${crypto.randomUUID()}`, name, source, applied: "", program: null, generation: 0, needsRebuild: false };
}

export function copyRole(roles, original) {
  let name = `${original.name} copy`, suffix = 2;
  while (roles.some(role => role.name === name)) name = `${original.name} copy ${suffix++}`;
  const role = { ...structuredClone(original), id: `custom-${crypto.randomUUID()}`, name, generation: 0 };
  roles.push(role);
  return role;
}
