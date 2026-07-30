const navigationMarkup = `
  <a class="brand" href="/index.html#motion" aria-label="DynLex home">
    <img class="brand-logo" src="/icons/dynlex-icon.svg" alt="">
    <span class="brand-name">DynLex</span>
    <span class="brand-mode">LAB</span>
  </a>
  <button class="menu-toggle" type="button" aria-expanded="false" aria-controls="primary-nav">
    <span class="menu-toggle-label">Menu</span>
    <span class="menu-toggle-lines" aria-hidden="true"><span></span><span></span></span>
  </button>
  <nav class="primary-nav" id="primary-nav" aria-label="Primary navigation" data-primary-nav>
    <a href="/index.html#challenges">Challenges</a>
    <a href="/index.html#language">Language</a>
    <a href="/index.html#studio">Studio</a>
    <a href="/wiki/index.html" data-navigation-docs>Docs</a>
    <a class="nav-launch" href="/ide/index.html"><span>Open IDE</span><i aria-hidden="true">↗</i></a>
  </nav>
  <span class="scroll-progress" data-scroll-progress aria-hidden="true"></span>
`;

function requireNavigationElement(selector, scope = document) {
  const element = scope.querySelector(selector);
  if (!element) {
    throw new Error("Site navigation could not initialize");
  }
  return element;
}

export function initializeSiteNavigation() {
  const header = requireNavigationElement("[data-site-header]");
  header.innerHTML = navigationMarkup;

  const menuButton = requireNavigationElement(".menu-toggle", header);
  const menuLabel = requireNavigationElement(".menu-toggle-label", menuButton);
  const primaryNavigation = requireNavigationElement("[data-primary-nav]", header);
  const progress = requireNavigationElement("[data-scroll-progress]", header);
  const docsLink = requireNavigationElement("[data-navigation-docs]", header);
  if (window.location.pathname.includes("/wiki/")) {
    docsLink.setAttribute("aria-current", "page");
  }

  function setMenu(open) {
    menuButton.setAttribute("aria-expanded", String(open));
    primaryNavigation.classList.toggle("is-open", open);
    document.body.classList.toggle("menu-open", open);
    menuLabel.textContent = open ? "Close" : "Menu";
  }

  menuButton.addEventListener("click", () => {
    setMenu(menuButton.getAttribute("aria-expanded") !== "true");
  });
  primaryNavigation.addEventListener("click", (event) => {
    if (event.target.closest("a")) setMenu(false);
  });
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") setMenu(false);
  });

  function updateScrollChrome() {
    const scrollRange = document.documentElement.scrollHeight - window.innerHeight;
    const scrollFraction = scrollRange > 0 ? window.scrollY / scrollRange : 0;
    header.classList.toggle("is-scrolled", window.scrollY > 24);
    progress.style.transform = `scaleX(${Math.min(1, scrollFraction)})`;
    if (window.innerWidth > 940 && menuButton.getAttribute("aria-expanded") === "true") {
      setMenu(false);
    }
  }

  window.addEventListener("scroll", updateScrollChrome, { passive: true });
  window.addEventListener("resize", updateScrollChrome);
  updateScrollChrome();
}
