/*
 * Dark mode controller for the AI-on-the-edge-device web UI.
 *
 * - Applies the saved theme as early as possible (on parse, before paint) to
 *   avoid a flash of the light theme.
 * - Theme is persisted in localStorage ("aiotedge-theme" = "dark" | "light").
 *   With no saved choice it follows the OS `prefers-color-scheme`.
 * - localStorage is shared across same-origin documents, so each iframe page
 *   picks up the same theme via its own copy of this script. A live toggle in
 *   the parent (index.html) is additionally pushed into the open iframe.
 *
 * Include in <head>, before the page's stylesheets:
 *   <link rel="stylesheet" href="theme.css">
 *   <script src="theme.js"></script>
 */

(function () {
    try {
        var saved = localStorage.getItem('aiotedge-theme');
        var dark = saved
            ? (saved === 'dark')
            : (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches);
        if (dark) {
            document.documentElement.setAttribute('data-theme', 'dark');
        }
    } catch (e) { /* localStorage may be unavailable; default to light */ }
})();

/* Re-apply the saved theme to this document (used for live propagation). */
function applyTheme() {
    var dark = false;
    try { dark = (localStorage.getItem('aiotedge-theme') === 'dark'); } catch (e) {}
    if (dark) {
        document.documentElement.setAttribute('data-theme', 'dark');
    } else {
        document.documentElement.removeAttribute('data-theme');
    }
    updateThemeToggleLabel(dark);
}

function setTheme(dark) {
    try { localStorage.setItem('aiotedge-theme', dark ? 'dark' : 'light'); } catch (e) {}
    applyTheme();
    // Propagate to the content iframe (present on index.html).
    var frame = document.getElementById('maincontent');
    if (frame && frame.contentWindow && typeof frame.contentWindow.applyTheme === 'function') {
        try { frame.contentWindow.applyTheme(); } catch (e) {}
    }
}

function toggleTheme() {
    var dark = document.documentElement.getAttribute('data-theme') !== 'dark';
    setTheme(dark);
}

function updateThemeToggleLabel(dark) {
    var btn = document.getElementById('themeToggle');
    if (btn) {
        btn.textContent = dark ? '☀️' /* sun */ : '🌙' /* moon */;
        btn.setAttribute('aria-label', dark ? 'Switch to light mode' : 'Switch to dark mode');
    }
}

/* Keep in sync if the OS theme changes and the user hasn't chosen explicitly. */
try {
    if (window.matchMedia) {
        window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', function (e) {
            var saved = null;
            try { saved = localStorage.getItem('aiotedge-theme'); } catch (x) {}
            if (!saved) {
                if (e.matches) document.documentElement.setAttribute('data-theme', 'dark');
                else document.documentElement.removeAttribute('data-theme');
                updateThemeToggleLabel(e.matches);
            }
        });
    }
} catch (e) {}

/* Set the toggle button label once the DOM is ready. */
document.addEventListener('DOMContentLoaded', function () {
    updateThemeToggleLabel(document.documentElement.getAttribute('data-theme') === 'dark');
});
