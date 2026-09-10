export const FARM_GOALS = [
  { id: "watering", title: "A watering helper", target: 4, description: "Program a worker to water four different planted beds.", hints: ["Take a bucket from the barn and fill it beside the pond.", "Walk to the nearest thirsty crop, then water the crops. Check whether your bucket is empty inside your loop."] },
  { id: "planting", title: "A field of your own", target: 4, description: "Program a worker to plant four different beds.", hints: ["A hoe plows grass. Seeds go into empty, plowed beds.", "Draw a work area to keep this role in one field. Use a loop to plant more than one bed."] },
  { id: "eggs", title: "Breakfast helper", target: 6, description: "Program a worker to collect six eggs.", hints: ["Chickens need wheat before they lay eggs. They wander, so walk to the nearest chicken.", "Walk to the nearest egg and collect eggs. Return to the barn when your hands are full."] }
];

export const RIVER_GOALS = [
  { id: "passenger", title: "Your first passenger", target: 1, description: "Write a command that gets a passenger into the boat.", hints: ["The boat has room for one passenger. Who cannot be left with either of the others?"] },
  { id: "deliveries", title: "Meet the far shore", target: 3, description: "Take each of the three passengers to the far shore.", hints: ["Rowing moves the boat. Add a command to let your passenger out.", "Sometimes a passenger needs to come back with you to keep everyone safe."] },
  { id: "rescue", title: "Everyone together", target: 1, description: "Finish a plan that leaves everyone safe on the far shore.", hints: ["Step through your plan. After each crossing, check who is alone on each shore.", "Name a repeated group of commands with a function beginning with to."] }
];

export const newGoalProgress = goals => Object.fromEntries(goals.map(goal => [goal.id, { count: 0, seen: [] }]));

export function recordGoal(progress, id, amount = 1, key = null) {
  const goal = progress[id];
  if (amount <= 0 || (key !== null && goal.seen.includes(key))) return;
  if (key !== null) goal.seen.push(key);
  goal.count += amount;
}

// Both games keep completion data in their save and use the same optional hint UI.
export function createCodingGoals(mount, goals) {
  mount.className = "coding-goals";
  const title = document.createElement("h4"); title.textContent = "Optional coding goals";
  const cards = goals.map(goal => {
    const card = document.createElement("article"); card.dataset.codingGoal = goal.id;
    const heading = document.createElement("h5"); heading.textContent = goal.title;
    const description = document.createElement("p"); description.textContent = goal.description;
    const meter = document.createElement("progress"); meter.max = goal.target; meter.value = 0;
    meter.setAttribute("aria-label", goal.title);
    const count = document.createElement("span");
    const hints = document.createElement("details");
    const summary = document.createElement("summary"); summary.textContent = "Need a hint?"; hints.append(summary);
    for (const hint of goal.hints) { const p = document.createElement("p"); p.textContent = hint; hints.append(p); }
    card.append(heading, description, meter, count, hints);
    return { goal, card, meter, count };
  });
  mount.replaceChildren(title, ...cards.map(entry => entry.card));
  return progress => {
    for (const {goal,card,meter,count} of cards) {
      const value = Math.min(goal.target, progress[goal.id].count), complete = value === goal.target;
      meter.value = value; count.textContent = complete ? "✓ Completed" : `${value} / ${goal.target}`;
      card.dataset.complete = String(complete);
    }
  };
}
