// Vibrium cosmetic-filtering content script.
//
// Evaluated by the browser as `(<this file>)(<JSON text>)` in the page's main
// world, once per document. The argument is the JSON produced by
// vibrium_adblock_cosmetic_payload():
//   { css: string, procedural: [string], generichide: bool }
//
// What it does
//   1. Applies `css` (plain selector hiding) with a constructable stylesheet,
//      which a page's Content-Security-Policy cannot block.
//   2. Runs procedural filters (:has-text, :upward, :xpath, :remove, :style...)
//      on load and whenever the DOM changes.
//   3. Unless `generichide`, reports the class names and ids it sees through
//      window.cefQuery and applies the CSS the browser answers with.
//
// Everything here is defensive: a bad filter or a missing API must never throw
// into the page, and work per DOM change is bounded.
function (cfgText) {
  'use strict';
  var doc = document;
  var win = window;
  if (win.__vibriumCosmetic) { return; }
  var cfg;
  try { cfg = JSON.parse(cfgText); } catch (e) { return; }
  try {
    Object.defineProperty(win, '__vibriumCosmetic', { value: true });
  } catch (e) { return; }

  var MAX_NAMES_PER_QUERY = 500;
  var MAX_QUERIES = 200;
  var MAX_GENERIC_CSS = 1024 * 1024;
  var MAX_ELEMENTS_PER_FILTER = 2000;
  var DEBOUNCE_MS = 100;

  // ---------------------------------------------------------------- styles
  var sheets = [];
  function addSheet(css) {
    if (!css) { return null; }
    try {
      var s = new CSSStyleSheet();
      s.replaceSync(css);
      doc.adoptedStyleSheets = doc.adoptedStyleSheets.concat([s]);
      return s;
    } catch (e) {
      try {
        var st = doc.createElement('style');
        st.textContent = css;
        (doc.head || doc.documentElement).appendChild(st);
        return st;
      } catch (e2) { return null; }
    }
  }
  sheets.push(addSheet(cfg.css));

  // ------------------------------------------------------- procedural parse
  function parseRegexOrText(arg) {
    var m = /^\/(.*)\/([a-z]*)$/s.exec(arg);
    if (m) {
      try { return new RegExp(m[1], m[2].replace(/[^dgimsuvy]/g, '')); } catch (e) { return null; }
    }
    return arg;
  }
  function textMatches(matcher, text) {
    if (matcher === null) { return false; }
    if (typeof matcher === 'string') { return text.indexOf(matcher) !== -1; }
    matcher.lastIndex = 0;
    return matcher.test(text);
  }
  // "name"="value" / name=value / name; each side may be /regex/.
  function parseAttrMatcher(arg) {
    var m = /^\s*(?:"([^"]*)"|([^=]+?))\s*(?:=\s*(?:"([^"]*)"|(.*?)))?\s*$/.exec(arg);
    if (!m) { return null; }
    var name = m[1] !== undefined ? m[1] : m[2];
    var hasValue = m[3] !== undefined || m[4] !== undefined;
    var value = m[3] !== undefined ? m[3] : m[4];
    return {
      name: parseRegexOrText(name),
      value: hasValue ? parseRegexOrText(value) : undefined
    };
  }
  function attrMatches(el, spec) {
    var attrs = el.attributes;
    for (var i = 0; i < attrs.length; i++) {
      var a = attrs[i];
      var nameOk = typeof spec.name === 'string' ? a.name === spec.name : textMatches(spec.name, a.name);
      if (!nameOk) { continue; }
      if (spec.value === undefined) { return true; }
      if (typeof spec.value === 'string' ? a.value === spec.value : textMatches(spec.value, a.value)) {
        return true;
      }
    }
    return false;
  }
  function cssMatches(el, pseudo, arg) {
    var i = arg.indexOf(':');
    if (i < 0) { return false; }
    var prop = arg.slice(0, i).trim();
    var want = arg.slice(i + 1).trim();
    var matcher = parseRegexOrText(want);
    var cs;
    try { cs = win.getComputedStyle(el, pseudo); } catch (e) { return false; }
    if (!cs) { return false; }
    var have = cs.getPropertyValue(prop);
    if (pseudo && (have === '' || have === undefined)) { return false; }
    return typeof matcher === 'string' ? have === matcher : textMatches(matcher, have);
  }

  function unique(list) {
    var seen = new Set();
    var out = [];
    for (var i = 0; i < list.length; i++) {
      if (!seen.has(list[i])) { seen.add(list[i]); out.push(list[i]); }
    }
    return out;
  }

  // Applies one operator to a list of elements; null = no match / bad filter.
  function applyOp(op, els, first) {
    var out = [];
    var i, j, el, found;
    switch (op.type) {
      case 'css-selector':
        if (first) {
          return Array.prototype.slice.call(doc.querySelectorAll(op.arg), 0, MAX_ELEMENTS_PER_FILTER);
        }
        for (i = 0; i < els.length; i++) {
          found = els[i].querySelectorAll(op.arg);
          for (j = 0; j < found.length; j++) { out.push(found[j]); }
        }
        return unique(out).slice(0, MAX_ELEMENTS_PER_FILTER);
      case 'has-text': {
        var textMatcher = parseRegexOrText(op.arg);
        return els.filter(function (e) { return textMatches(textMatcher, e.textContent || ''); });
      }
      case 'min-text-length': {
        var n = parseInt(op.arg, 10);
        if (!(n >= 0)) { return null; }
        return els.filter(function (e) { return (e.textContent || '').length >= n; });
      }
      case 'matches-attr': {
        var spec = parseAttrMatcher(op.arg);
        if (!spec) { return null; }
        return els.filter(function (e) { return attrMatches(e, spec); });
      }
      case 'matches-css':
        return els.filter(function (e) { return cssMatches(e, null, op.arg); });
      case 'matches-css-before':
        return els.filter(function (e) { return cssMatches(e, '::before', op.arg); });
      case 'matches-css-after':
        return els.filter(function (e) { return cssMatches(e, '::after', op.arg); });
      case 'upward': {
        var levels = /^\d+$/.test(op.arg) ? parseInt(op.arg, 10) : -1;
        if (levels === 0 || levels > 50) { return null; }
        for (i = 0; i < els.length; i++) {
          el = els[i];
          if (levels > 0) {
            for (j = 0; j < levels && el; j++) { el = el.parentElement; }
            if (el) { out.push(el); }
          } else {
            var up = els[i].parentElement;
            el = up ? up.closest(op.arg) : null;
            if (el) { out.push(el); }
          }
        }
        return unique(out);
      }
      case 'xpath':
        for (i = 0; i < (first ? 1 : els.length); i++) {
          var ctx = first ? doc : els[i];
          var res = doc.evaluate(op.arg, ctx, null, 7 /* ORDERED_NODE_SNAPSHOT */, null);
          for (j = 0; j < res.snapshotLength && out.length < MAX_ELEMENTS_PER_FILTER; j++) {
            if (res.snapshotItem(j).nodeType === 1) { out.push(res.snapshotItem(j)); }
          }
        }
        return unique(out);
      default:
        return null;  // unknown operator: ignore the whole filter
    }
  }

  function parseStyle(text) {
    // "prop: value [!important]; ..." -> [[prop, value, priority]]
    var out = [];
    String(text).split(';').forEach(function (part) {
      var k = part.indexOf(':');
      if (k < 1) { return; }
      var prop = part.slice(0, k).trim();
      var val = part.slice(k + 1).trim();
      var pri = '';
      var imp = /\s*!important\s*$/i;
      if (imp.test(val)) { val = val.replace(imp, ''); pri = 'important'; }
      if (prop && val) { out.push([prop, val, pri]); }
    });
    return out;
  }

  function act(action, el) {
    if (!action) {
      el.style.setProperty('display', 'none', 'important');
      return;
    }
    switch (action.type) {
      case 'style':
        parseStyle(action.arg).forEach(function (d) { el.style.setProperty(d[0], d[1], d[2]); });
        break;
      case 'remove':
        if (el.parentNode) { el.parentNode.removeChild(el); }
        break;
      case 'remove-attr':
        try { el.removeAttribute(action.arg); } catch (e) { /* invalid name */ }
        break;
      case 'remove-class':
        el.classList.remove(action.arg);
        break;
    }
  }

  var filters = [];
  (cfg.procedural || []).forEach(function (text) {
    try {
      var f = JSON.parse(text);
      if (f && Array.isArray(f.selector) && f.selector.length) { filters.push(f); }
    } catch (e) { /* skip */ }
  });

  function runProcedural() {
    if (!doc.documentElement) { return; }
    for (var i = 0; i < filters.length; i++) {
      try {
        var f = filters[i];
        var els = [];
        var ok = true;
        // :matches-path() is a gate on the page URL, not a step on elements.
        var ops = [];
        for (var g = 0; g < f.selector.length; g++) {
          var op = f.selector[g];
          if (op.type === 'matches-path') {
            if (!textMatches(parseRegexOrText(op.arg), win.location.pathname + win.location.search)) { ok = false; break; }
          } else { ops.push(op); }
        }
        if (!ok || !ops.length) { continue; }
        for (var k = 0; k < ops.length; k++) {
          els = applyOp(ops[k], els, k === 0);
          if (els === null) { ok = false; break; }
          if (!els.length) { break; }
        }
        if (!ok) { continue; }
        for (var m = 0; m < els.length; m++) { act(f.action, els[m]); }
      } catch (e) { /* one bad filter must not stop the others */ }
    }
  }

  // -------------------------------------------------------- generic lookup
  var seenClasses = new Set();
  var seenIds = new Set();
  var pendingClasses = [];
  var pendingIds = [];
  var queries = 0;
  var genericCss = '';
  var genericSheet = null;
  var canQuery = !cfg.generichide && typeof win.cefQuery === 'function';

  function noteElement(el) {
    if (el.id && !seenIds.has(el.id)) { seenIds.add(el.id); pendingIds.push(el.id); }
    var cl = el.classList;
    if (cl) {
      for (var i = 0; i < cl.length; i++) {
        if (!seenClasses.has(cl[i])) { seenClasses.add(cl[i]); pendingClasses.push(cl[i]); }
      }
    }
  }
  function scanSubtree(root) {
    if (root.nodeType !== 1) { return; }
    noteElement(root);
    var all = root.querySelectorAll('[class],[id]');
    for (var i = 0; i < all.length; i++) { noteElement(all[i]); }
  }
  function applyGeneric(css) {
    if (!css || genericCss.length + css.length > MAX_GENERIC_CSS) { return; }
    genericCss += css;
    try {
      if (genericSheet && genericSheet.replaceSync) {
        genericSheet.replaceSync(genericCss);
      } else {
        genericSheet = addSheet(genericCss);
      }
    } catch (e) { /* ignore */ }
  }
  function flushGeneric() {
    while (canQuery && (pendingClasses.length || pendingIds.length) && queries < MAX_QUERIES) {
      var classes = pendingClasses.splice(0, MAX_NAMES_PER_QUERY);
      var ids = pendingIds.splice(0, MAX_NAMES_PER_QUERY);
      queries++;
      try {
        win.cefQuery({
          // Line 1: marker, line 2: class names, line 3: ids (JSON never contains a raw newline).
          request: 'G\n' + JSON.stringify(classes) + '\n' + JSON.stringify(ids),
          persistent: false,
          onSuccess: applyGeneric,
          onFailure: function () {}
        });
      } catch (e) { canQuery = false; }
    }
  }

  // ------------------------------------------------------------- scheduling
  var timer = null;
  var dirtyRoots = [];
  function tick() {
    timer = null;
    if (filters.length) { runProcedural(); }
    if (canQuery) {
      for (var i = 0; i < dirtyRoots.length; i++) { scanSubtree(dirtyRoots[i]); }
      dirtyRoots = [];
      flushGeneric();
    }
  }
  function schedule() {
    if (timer === null) { timer = win.setTimeout(tick, DEBOUNCE_MS); }
  }

  function start() {
    var root = doc.documentElement;
    if (!root) { return; }
    if (canQuery) { scanSubtree(root); flushGeneric(); }
    runProcedural();
    if (!filters.length && !canQuery) { return; }
    try {
      new MutationObserver(function (records) {
        if (canQuery) {
          for (var i = 0; i < records.length; i++) {
            var r = records[i];
            if (r.type === 'attributes') { dirtyRoots.push(r.target); }
            else {
              for (var j = 0; j < r.addedNodes.length; j++) { dirtyRoots.push(r.addedNodes[j]); }
            }
          }
          if (dirtyRoots.length > 5000) { dirtyRoots = [doc.documentElement]; }
        }
        schedule();
      }).observe(root, { subtree: true, childList: true, attributes: true, attributeFilter: ['class', 'id'] });
    } catch (e) { /* no observer: initial pass only */ }
  }

  if (doc.documentElement) { start(); }
  else { doc.addEventListener('readystatechange', start, { once: true }); }
  if (doc.readyState === 'loading') {
    doc.addEventListener('DOMContentLoaded', function () { runProcedural(); schedule(); }, { once: true });
  }
}
