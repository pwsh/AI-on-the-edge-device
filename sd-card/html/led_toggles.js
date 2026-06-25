// led_toggles.js - shared widget: per-LED on/off toggles for the external WS281x strip.
// Arranges `count` toggles as line / grid / circle. Toggling a LED (or All/None) calls
// GET /ledstate?mask=... for a realtime hardware update, and fires onChange({mask,layout,cols})
// so the host page can persist the values to config.
// Used by both the configuration page and the live-stream + camera-setup page so behaviour is identical.
//
// Usage:
//   var w = mountLedToggles(containerEl, {
//       count: 12, mask: "111110111111", layout: "circle", cols: 4,
//       domain: "",                       // base URL for /ledstate (default same-origin)
//       showLayoutControls: true,         // show the line/grid/circle + All/None bar
//       onChange: function(s){ ... }      // s = {mask, layout, cols}
//   });
//   w.getState();   // -> {mask, layout, cols}

(function () {
    if (window.__ledTogglesCss) return;
    window.__ledTogglesCss = true;
    var css = ''
        + '.ledw{margin:6px 0;}'
        + '.ledw-bar{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-bottom:8px;}'
        + '.ledw-bar select,.ledw-bar input{padding:2px 4px;}'
        + '.ledw-btn{padding:3px 9px;cursor:pointer;border:1px solid #888;border-radius:5px;background:transparent;color:inherit;}'
        + '.ledtoggle{width:34px;height:34px;border-radius:7px;border:1px solid #888;margin:3px;cursor:pointer;'
        + 'font-size:12px;font-weight:600;line-height:1;padding:0;}'
        + '.ledtoggle.on{background:#ffce3a;color:#1a1a1a;box-shadow:0 0 6px rgba(255,206,58,.8);border-color:#caa11f;}'
        + '.ledtoggle.off{background:#3a3a3a;color:#9a9a9a;border-color:#555;}'
        + '.ledmatrix{display:grid;gap:3px;justify-content:start;}'
        + '.ledline{display:flex;flex-wrap:wrap;}'
        + '.ledcircle{position:relative;margin:4px auto;}'
        + '.ledcircle .ledtoggle{position:absolute;margin:0;}';
    var s = document.createElement('style');
    s.textContent = css;
    document.head.appendChild(s);
})();

function mountLedToggles(container, opts) {
    opts = opts || {};
    var count = Math.max(0, parseInt(opts.count) || 0);
    var domain = opts.domain || '';
    var onChange = opts.onChange || function () {};
    var showCtrls = opts.showLayoutControls !== false;

    function normMask(m, n) {
        m = (m || '').toString();
        var o = '';
        for (var i = 0; i < n; i++) o += (m[i] === '0') ? '0' : '1';   // default ON
        return o;
    }
    function setc(str, i, c) { return str.substring(0, i) + c + str.substring(i + 1); }

    var state = {
        mask: normMask(opts.mask, count),
        layout: (['line', 'grid', 'circle'].indexOf(opts.layout) >= 0) ? opts.layout : 'line',
        cols: Math.max(1, parseInt(opts.cols) || Math.max(1, Math.round(Math.sqrt(count || 1))))
    };

    container.classList.add('ledw');
    container.innerHTML = '';
    var area;

    function emit(maskChanged) {
        if (maskChanged) { try { fetch(domain + '/ledstate?mask=' + state.mask); } catch (e) {} }
        onChange({ mask: state.mask, layout: state.layout, cols: state.cols });
    }

    function btn(i) {
        var b = document.createElement('button');
        b.type = 'button';
        b.className = 'ledtoggle ' + (state.mask[i] === '1' ? 'on' : 'off');
        b.textContent = (i + 1);
        b.title = 'LED ' + (i + 1);
        b.onclick = function () {
            state.mask = setc(state.mask, i, state.mask[i] === '1' ? '0' : '1');
            b.className = 'ledtoggle ' + (state.mask[i] === '1' ? 'on' : 'off');
            emit(true);
        };
        return b;
    }

    function draw() {
        area.innerHTML = '';
        if (count === 0) { area.textContent = '(set the number of LEDs first)'; return; }
        var i;
        if (state.layout === 'circle') {
            var R = Math.max(60, Math.round(count * 7));   // radius scales so buttons don't overlap
            var c = R + 22;
            var w = document.createElement('div');
            w.className = 'ledcircle';
            w.style.width = w.style.height = (c * 2) + 'px';
            for (i = 0; i < count; i++) {
                var ang = (-90 + i * 360 / count) * Math.PI / 180;   // start at top, clockwise
                var b = btn(i);
                b.style.left = (c + R * Math.cos(ang) - 17) + 'px';
                b.style.top = (c + R * Math.sin(ang) - 17) + 'px';
                w.appendChild(b);
            }
            area.appendChild(w);
        } else if (state.layout === 'grid') {
            var g = document.createElement('div');
            g.className = 'ledmatrix';
            g.style.gridTemplateColumns = 'repeat(' + state.cols + ',auto)';
            for (i = 0; i < count; i++) g.appendChild(btn(i));
            area.appendChild(g);
        } else {
            var l = document.createElement('div');
            l.className = 'ledline';
            for (i = 0; i < count; i++) l.appendChild(btn(i));
            area.appendChild(l);
        }
    }

    if (showCtrls) {
        var bar = document.createElement('div');
        bar.className = 'ledw-bar';

        var lsel = document.createElement('select');
        [['line', 'Line'], ['grid', 'Grid'], ['circle', 'Circle']].forEach(function (v) {
            var o = document.createElement('option');
            o.value = v[0]; o.textContent = v[1];
            if (v[0] === state.layout) o.selected = true;
            lsel.appendChild(o);
        });

        var colWrap = document.createElement('span');
        colWrap.textContent = ' columns ';
        var colIn = document.createElement('input');
        colIn.type = 'number'; colIn.min = '1'; colIn.style.width = '54px'; colIn.value = state.cols;
        colWrap.appendChild(colIn);
        colWrap.style.display = (state.layout === 'grid') ? 'inline' : 'none';

        lsel.onchange = function () {
            state.layout = lsel.value;
            colWrap.style.display = (state.layout === 'grid') ? 'inline' : 'none';
            draw(); emit(false);
        };
        colIn.onchange = function () {
            state.cols = Math.max(1, parseInt(colIn.value) || 1);
            draw(); emit(false);
        };

        var allB = document.createElement('button');
        allB.type = 'button'; allB.className = 'ledw-btn'; allB.textContent = 'All on';
        allB.onclick = function () {
            var m = ''; for (var i = 0; i < count; i++) m += '1';
            state.mask = m; draw(); emit(true);
        };
        var noneB = document.createElement('button');
        noneB.type = 'button'; noneB.className = 'ledw-btn'; noneB.textContent = 'All off';
        noneB.onclick = function () {
            var m = ''; for (var i = 0; i < count; i++) m += '0';
            state.mask = m; draw(); emit(true);
        };

        bar.appendChild(lsel); bar.appendChild(colWrap); bar.appendChild(allB); bar.appendChild(noneB);
        container.appendChild(bar);
    }

    area = document.createElement('div');
    container.appendChild(area);
    draw();

    return { getState: function () { return { mask: state.mask, layout: state.layout, cols: state.cols }; } };
}
