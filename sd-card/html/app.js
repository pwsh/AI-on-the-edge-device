/* =============================================================================
 * app.js - shared behaviour for the AI-on-the-edge-device web UI.
 *
 * - App.initNav()         wires the top-bar/hamburger navigation (index shell)
 * - App.initAccordions()  makes every .acc-head toggle its .acc section
 * - App.help(name, text)  builds a name-anchored hover tooltip element
 *
 * All functions are defensive: they no-op when their markup is absent, so any
 * page can include app.js and call only what it needs.
 * ========================================================================== */
var App = (function () {

    // ---- Top navigation -----------------------------------------------------
    function initNav() {
        var nav = document.querySelector('.appnav');
        if (!nav) return;
        var menu = nav.querySelector('.appnav-menu');
        var burger = nav.querySelector('.appnav-burger');

        if (burger && menu) {
            burger.addEventListener('click', function (e) {
                e.stopPropagation();
                var open = menu.classList.toggle('open');
                burger.setAttribute('aria-expanded', open ? 'true' : 'false');
            });
        }

        // Click a top-level group -> toggle its submenu (and close siblings).
        nav.addEventListener('click', function (e) {
            var top = e.target.closest('.appnav-top');
            if (top && nav.contains(top)) {
                e.preventDefault();
                var li = top.parentNode;
                var wasOpen = li.classList.contains('open');
                closeAllSubs();
                if (!wasOpen) li.classList.add('open');
                return;
            }
            // Clicking a leaf link closes the menus (the link's own onclick still runs).
            var leaf = e.target.closest('.appnav-sub a, .appnav-menu > ul > li > a');
            if (leaf) {
                closeAllSubs();
                if (menu) menu.classList.remove('open');
                if (burger) burger.setAttribute('aria-expanded', 'false');
            }
        });

        // Click outside the nav closes everything.
        document.addEventListener('click', function (e) {
            if (!nav.contains(e.target)) {
                closeAllSubs();
                if (menu) menu.classList.remove('open');
                if (burger) burger.setAttribute('aria-expanded', 'false');
            }
        });

        function closeAllSubs() {
            [].forEach.call(nav.querySelectorAll('.appnav-menu > ul > li.open'),
                function (li) { li.classList.remove('open'); });
        }
    }

    // ---- Accordions ---------------------------------------------------------
    // Any element with class .acc that contains a .acc-head becomes collapsible.
    // Add data-collapsed to start collapsed. A chevron is injected if absent.
    function initAccordions(root) {
        root = root || document;
        [].forEach.call(root.querySelectorAll('.acc'), function (acc) {
            if (acc.getAttribute('data-acc-ready')) return;
            var head = acc.querySelector('.acc-head');
            if (!head) return;
            acc.setAttribute('data-acc-ready', '1');
            if (acc.hasAttribute('data-collapsed')) acc.classList.add('collapsed');
            if (!head.querySelector('.acc-chevron')) {
                var chev = document.createElement('span');
                chev.className = 'acc-chevron';
                chev.textContent = '▾'; // down triangle
                head.appendChild(chev);
            }
            head.addEventListener('click', function (e) {
                if (e.target.closest('input, a, label, select, button')) return;
                acc.classList.toggle('collapsed');
            });
        });
    }

    // Collapse / expand every accordion under root (for an "expand all" control).
    function toggleAllAccordions(root) {
        root = root || document;
        var accs = root.querySelectorAll('.acc');
        var anyOpen = [].some.call(accs, function (a) { return !a.classList.contains('collapsed'); });
        [].forEach.call(accs, function (a) { a.classList.toggle('collapsed', anyOpen); });
    }

    // ---- Name-anchored help tooltip ----------------------------------------
    // Returns a <span class="app-nt"> wrapping `label` with a hover `.tooltiptext`.
    function help(label, html) {
        var wrap = document.createElement('span');
        wrap.className = 'app-nt';
        var name = document.createElement('span');
        name.className = 'app-help';
        name.textContent = label;
        var tip = document.createElement('span');
        tip.className = 'tooltiptext';
        tip.innerHTML = html;
        wrap.appendChild(name);
        wrap.appendChild(tip);
        return wrap;
    }

    return {
        initNav: initNav,
        initAccordions: initAccordions,
        toggleAllAccordions: toggleAllAccordions,
        help: help
    };
})();

// Auto-init on DOM ready so a page only needs to include app.js.
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function () {
        App.initNav(); App.initAccordions();
    });
} else {
    App.initNav(); App.initAccordions();
}
