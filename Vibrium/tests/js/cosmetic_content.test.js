'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { JSDOM } = require('jsdom');

const SOURCE = fs.readFileSync(
  path.join(__dirname, '..', '..', 'c++', 'adblock', 'cosmetic_content.js'), 'utf8');

const open = [];
test.after(() => open.forEach((d) => d.window.close()));

function page(html, { url = 'https://example.com/article?x=1', cefQuery } = {}) {
  const dom = new JSDOM(html, { url, runScripts: 'outside-only', pretendToBeVisual: true });
  open.push(dom);
  if (cefQuery) dom.window.cefQuery = cefQuery;
  return dom;
}
function run(dom, cfg) {
  dom.window.eval('(' + SOURCE + ')(' + JSON.stringify(JSON.stringify(cfg)) + ')');
}
function proc(selector, action) {
  return JSON.stringify(action ? { selector, action } : { selector });
}
const css = (arg) => ({ type: 'css-selector', arg });
function decode(request) {
  const [marker, classes, ids, ...extra] = request.split('\n');
  assert.equal(marker, 'G');
  assert.equal(extra.length, 0, 'exactly three lines');
  return { classes: JSON.parse(classes), ids: JSON.parse(ids) };
}
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const hidden = (el) => el.style.display === 'none';

test('applies plain CSS to the document', () => {
  const dom = page('<div class="ad">x</div>');
  run(dom, { css: '.ad{display:none!important}', procedural: [], generichide: true });
  assert.match(dom.window.document.head.innerHTML + dom.window.document.documentElement.innerHTML, /\.ad\{display:none!important\}/);
});

test('is idempotent per document', () => {
  const dom = page('<div class="ad">x</div>');
  const cfg = { css: '.ad{display:none!important}', procedural: [], generichide: true };
  run(dom, cfg); run(dom, cfg);
  assert.equal(dom.window.document.querySelectorAll('style').length, 1);
});

test('malformed config never throws', () => {
  const dom = page('<p>hi</p>');
  assert.doesNotThrow(() => dom.window.eval('(' + SOURCE + ')("not json")'));
  assert.doesNotThrow(() => run(dom, { css: 5, procedural: ['{bad', '{"selector":"x"}', 7], generichide: true }));
});

test(':has-text hides only matching elements (text and regex)', () => {
  const dom = page('<div class="p" id="a">Sponsored post</div><div class="p" id="b">Real article</div><div class="p" id="c">SPONSORED</div>');
  run(dom, { css: '', generichide: true, procedural: [
    proc([css('.p'), { type: 'has-text', arg: 'Sponsored' }]),
    proc([css('.p'), { type: 'has-text', arg: '/^sponsored$/i' }]),
  ]});
  const d = dom.window.document;
  assert.ok(hidden(d.getElementById('a')));
  assert.ok(!hidden(d.getElementById('b')));
  assert.ok(hidden(d.getElementById('c')));
});

test(':upward by level and by selector', () => {
  const dom = page('<section class="card" id="c1"><div><span class="t">ad</span></div></section><section class="card" id="c2"><div><span class="t">keep</span></div></section>');
  run(dom, { css: '', generichide: true, procedural: [
    proc([css('.t'), { type: 'has-text', arg: 'ad' }, { type: 'upward', arg: '.card' }]),
  ]});
  const d = dom.window.document;
  assert.ok(hidden(d.getElementById('c1')));
  assert.ok(!hidden(d.getElementById('c2')));

  const dom2 = page('<div id="outer"><div id="mid"><b class="x">y</b></div></div>');
  run(dom2, { css: '', generichide: true, procedural: [proc([css('.x'), { type: 'upward', arg: '2' }])] });
  assert.ok(hidden(dom2.window.document.getElementById('outer')));
  assert.ok(!hidden(dom2.window.document.getElementById('mid')));
});

test('actions: style, remove, remove-attr, remove-class', () => {
  const dom = page('<p id="s">s</p><p id="r">r</p><p id="a" data-x="1">a</p><p id="c" class="k z">c</p>');
  run(dom, { css: '', generichide: true, procedural: [
    proc([css('#s')], { type: 'style', arg: 'color: red; width: 5px !important' }),
    proc([css('#r')], { type: 'remove' }),
    proc([css('#a')], { type: 'remove-attr', arg: 'data-x' }),
    proc([css('#c')], { type: 'remove-class', arg: 'k' }),
  ]});
  const d = dom.window.document;
  assert.equal(d.getElementById('s').style.color, 'red');
  assert.equal(d.getElementById('s').style.getPropertyPriority('width'), 'important');
  assert.equal(d.getElementById('r'), null);
  assert.ok(!d.getElementById('a').hasAttribute('data-x'));
  assert.equal(d.getElementById('c').className, 'z');
});

test(':matches-attr, :min-text-length, :xpath, :matches-path', () => {
  const dom = page('<a id="a1" href="/ad/1">x</a><a id="a2" href="/news">y</a><p id="long">0123456789</p><p id="short">01</p><div id="xp">z</div>');
  run(dom, { css: '', generichide: true, procedural: [
    proc([css('a'), { type: 'matches-attr', arg: '"href"="/ad/.*"' }].map((o) => o.type === 'matches-attr' ? { type: o.type, arg: '"href"=/\\/ad\\//' } : o)),
    proc([css('p'), { type: 'min-text-length', arg: '5' }]),
    proc([{ type: 'xpath', arg: '//div[@id="xp"]' }]),
    proc([{ type: 'matches-path', arg: '/article' }, css('#short')]),
  ]});
  const d = dom.window.document;
  assert.ok(hidden(d.getElementById('a1')));
  assert.ok(!hidden(d.getElementById('a2')));
  assert.ok(hidden(d.getElementById('long')));
  assert.ok(hidden(d.getElementById('xp')));
  assert.ok(hidden(d.getElementById('short')), 'matches-path gate passes for /article');

  const other = page('<p id="short">01</p>', { url: 'https://example.com/other' });
  run(other, { css: '', generichide: true, procedural: [proc([{ type: 'matches-path', arg: '/article' }, css('#short')])] });
  assert.ok(!hidden(other.window.document.getElementById('short')), 'matches-path gate blocks other paths');
});

test('unknown operators and invalid selectors skip only that filter', () => {
  const dom = page('<p id="a">a</p><p id="b">b</p>');
  run(dom, { css: '', generichide: true, procedural: [
    proc([css('#a'), { type: 'nonsense', arg: '1' }]),
    proc([css('###bad[[')]),
    proc([css('#b')]),
  ]});
  const d = dom.window.document;
  assert.ok(!hidden(d.getElementById('a')));
  assert.ok(hidden(d.getElementById('b')));
});

test('procedural filters also apply to content added later', async () => {
  const dom = page('<div id="root"></div>');
  run(dom, { css: '', generichide: true, procedural: [proc([css('.p'), { type: 'has-text', arg: 'Ad' }])] });
  const d = dom.window.document;
  const el = d.createElement('div'); el.className = 'p'; el.textContent = 'Ad here';
  d.getElementById('root').appendChild(el);
  await wait(400);
  assert.ok(hidden(el));
});

test('generic: reports class names and ids once, applies returned CSS', async () => {
  const calls = [];
  const dom = page('<div class="generic-ad plain" id="sponsor-box"></div>', {
    cefQuery: (q) => { calls.push(decode(q.request)); q.onSuccess('.generic-ad{display:none!important}\n'); },
  });
  run(dom, { css: '', procedural: [], generichide: false });
  assert.equal(calls.length, 1);
    assert.deepEqual(calls[0].classes.sort(), ['generic-ad', 'plain']);
  assert.deepEqual(calls[0].ids, ['sponsor-box']);
  assert.match(dom.window.document.documentElement.innerHTML, /\.generic-ad\{display:none!important\}/);

  // Dynamically added names are reported, already-seen ones are not repeated.
  const d = dom.window.document;
  const el = d.createElement('i'); el.className = 'plain fresh'; d.body.appendChild(el);
  await wait(400);
  assert.equal(calls.length, 2);
  assert.deepEqual(calls[1].classes, ['fresh']);
});

test('generic: generichide and missing cefQuery send nothing and do not throw', () => {
  const calls = [];
  const hide = page('<div class="x"></div>', { cefQuery: (q) => calls.push(q) });
  run(hide, { css: '', procedural: [], generichide: true });
  assert.equal(calls.length, 0);
  const none = page('<div class="x"></div>');
  assert.doesNotThrow(() => run(none, { css: '', procedural: [], generichide: false }));
});

test('generic: requests are chunked and capped', () => {
  const html = Array.from({ length: 1200 }, (_, i) => `<i class="c${i}"></i>`).join('');
  const calls = [];
  const dom = page(html, { cefQuery: (q) => calls.push(decode(q.request)) });
  run(dom, { css: '', procedural: [], generichide: false });
  assert.ok(calls.length >= 3);
  assert.ok(calls.every((c) => c.classes.length <= 500));
  assert.ok(calls.length <= 200);
});

test('generic: a throwing cefQuery disables lookups without breaking the page', () => {
  const dom = page('<div class="x"></div>', { cefQuery: () => { throw new Error('boom'); } });
  assert.doesNotThrow(() => run(dom, { css: '', procedural: [], generichide: false }));
});

test('the embedded (comment-stripped) build behaves like the source', () => {
  const inc = fs.readFileSync(path.join(__dirname, '..', '..', 'c++', 'adblock', 'cosmetic_content_js.inc'), 'utf8');
  const bytes = Buffer.from([...inc.matchAll(/0x([0-9a-f]{2})/g)].map((m) => parseInt(m[1], 16)));
  const embedded = bytes.toString('utf8');
  assert.ok(embedded.length < SOURCE.length, 'comments were stripped');

  const dom = page('<div class="p">Sponsored</div><div class="p">Real</div>');
  dom.window.eval('(' + embedded + ')(' + JSON.stringify(JSON.stringify({
    css: '', generichide: true,
    procedural: [proc([css('.p'), { type: 'has-text', arg: '/^Spons/' }])],
  })) + ')');
  const [a, b] = dom.window.document.querySelectorAll('.p');
  assert.ok(hidden(a));
  assert.ok(!hidden(b));
});
