(function () {
    "use strict";

    const header = document.getElementById("site-header");
    const progressBar = document.getElementById("scroll-progress-bar");
    const menuToggle = document.getElementById("menu-toggle");
    const navigation = document.getElementById("site-nav");
    const codeTabs = Array.from(document.querySelectorAll("[data-code-tab]"));
    const codePanels = Array.from(document.querySelectorAll("[data-code-panel]"));
    const ideLinks = Array.from(document.querySelectorAll("[data-ide-link]"));
    const copyButton = document.getElementById("copy-install");
    const currentYear = document.getElementById("current-year");
    let activeExample = "readable";

    function updateScrollState() {
        const scrollable = document.documentElement.scrollHeight - window.innerHeight;
        const progress = scrollable > 0 ? Math.min(1, Math.max(0, window.scrollY / scrollable)) : 0;
        if (progressBar) {
            progressBar.style.width = `${progress * 100}%`;
        }
        if (header) {
            header.classList.toggle("scrolled", window.scrollY > 24);
        }
    }

    function setMenu(open) {
        if (!menuToggle || !navigation) {
            return;
        }
        menuToggle.setAttribute("aria-expanded", String(open));
        navigation.classList.toggle("open", open);
        document.body.classList.toggle("menu-open", open);
        const label = menuToggle.querySelector(".sr-only");
        if (label) {
            label.textContent = open ? "Close navigation" : "Open navigation";
        }
    }

    function encodeBase64Url(value) {
        const bytes = new TextEncoder().encode(value);
        let binary = "";
        bytes.forEach((byte) => {
            binary += String.fromCharCode(byte);
        });
        return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
    }

    function activeSource() {
        const panel = codePanels.find((candidate) => candidate.dataset.codePanel === activeExample);
        return panel?.querySelector("code")?.textContent.trim() || "";
    }

    function updateIdeLinks() {
        const source = activeSource();
        const href = source ? `ide/index.html?code64=${encodeBase64Url(source)}` : "ide/index.html";
        ideLinks.forEach((link) => {
            link.href = href;
        });
    }

    function activateExample(name, focusPanel) {
        const selectedTab = codeTabs.find((tab) => tab.dataset.codeTab === name);
        const selectedPanel = codePanels.find((panel) => panel.dataset.codePanel === name);
        if (!selectedTab || !selectedPanel) {
            return;
        }

        activeExample = name;
        codeTabs.forEach((tab) => {
            const selected = tab === selectedTab;
            tab.classList.toggle("active", selected);
            tab.setAttribute("aria-selected", String(selected));
            tab.tabIndex = selected ? 0 : -1;
        });
        codePanels.forEach((panel) => {
            const selected = panel === selectedPanel;
            panel.classList.toggle("active", selected);
            panel.hidden = !selected;
        });
        updateIdeLinks();
        if (focusPanel) {
            selectedPanel.focus({ preventScroll: true });
        }
    }

    window.addEventListener("scroll", updateScrollState, { passive: true });
    updateScrollState();

    if (menuToggle && navigation) {
        menuToggle.addEventListener("click", () => {
            setMenu(menuToggle.getAttribute("aria-expanded") !== "true");
        });
        navigation.addEventListener("click", (event) => {
            if (event.target.closest("a")) {
                setMenu(false);
            }
        });
        window.addEventListener("resize", () => {
            if (window.innerWidth > 840) {
                setMenu(false);
            }
        });
        document.addEventListener("keydown", (event) => {
            if (event.key === "Escape" && menuToggle.getAttribute("aria-expanded") === "true") {
                setMenu(false);
                menuToggle.focus();
            }
        });
    }

    codeTabs.forEach((tab, index) => {
        tab.addEventListener("click", () => activateExample(tab.dataset.codeTab, false));
        tab.addEventListener("keydown", (event) => {
            if (event.key !== "ArrowLeft" && event.key !== "ArrowRight" && event.key !== "Home" && event.key !== "End") {
                return;
            }
            event.preventDefault();
            let nextIndex = index;
            if (event.key === "ArrowLeft") {
                nextIndex = (index - 1 + codeTabs.length) % codeTabs.length;
            } else if (event.key === "ArrowRight") {
                nextIndex = (index + 1) % codeTabs.length;
            } else if (event.key === "Home") {
                nextIndex = 0;
            } else if (event.key === "End") {
                nextIndex = codeTabs.length - 1;
            }
            const nextTab = codeTabs[nextIndex];
            activateExample(nextTab.dataset.codeTab, false);
            nextTab.focus();
        });
    });
    activateExample(activeExample, false);

    if (copyButton) {
        copyButton.addEventListener("click", async () => {
            const value = copyButton.dataset.copy || "";
            const label = copyButton.querySelector("span");
            try {
                await navigator.clipboard.writeText(value);
                if (label) {
                    label.textContent = "Copied";
                }
                window.setTimeout(() => {
                    if (label) {
                        label.textContent = "Copy";
                    }
                }, 1800);
            } catch {
                if (label) {
                    label.textContent = "Copy failed";
                }
            }
        });
    }

    if (currentYear) {
        currentYear.textContent = String(new Date().getFullYear());
    }

    const revealItems = Array.from(document.querySelectorAll(".reveal"));
    if ("IntersectionObserver" in window && !window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
        const observer = new IntersectionObserver((entries) => {
            entries.forEach((entry) => {
                if (!entry.isIntersecting) {
                    return;
                }
                entry.target.classList.add("visible");
                observer.unobserve(entry.target);
            });
        }, { threshold: 0.12, rootMargin: "0px 0px -5%" });
        revealItems.forEach((item) => observer.observe(item));
    } else {
        revealItems.forEach((item) => item.classList.add("visible"));
    }
}());
