#!/usr/bin/env node
// Converts uBlock Origin's scriptlets into an adblock-rust `resources.json`.
//
//   node tools/build_ubo_resources.mjs <path-to-uBlock-checkout> <output.json>
//
// uBO registers every scriptlet (and the helper functions it depends on) with
// registerScriptlet(). We import all of them, then write each one out as a
// function-style adblock-rust Resource: scriptlets become
// `application/javascript`, helpers become `fn/javascript`, and a scriptlet
// that needs trust in uBO gets permission bit 1.
//
// uBO is GPL-3.0-or-later; the output contains its code. See
// THIRD_PARTY_NOTICES.md.
import { pathToFileURL } from 'node:url';
import { writeFileSync } from 'node:fs';
import path from 'node:path';

const [, , uboRoot, outFile] = process.argv;
if (!uboRoot || !outFile) {
  console.error('usage: build_ubo_resources.mjs <uBlock checkout> <output.json>');
  process.exit(2);
}

const dir = path.resolve(uboRoot, 'src', 'js', 'resources');
const base = await import(pathToFileURL(path.join(dir, 'base.js')).href);
await import(pathToFileURL(path.join(dir, 'scriptlets.js')).href);

const TRUSTED = 1; // adblock-rust PermissionMask bit for "trusted" scriptlets
const resources = [];
const seen = new Set();
const skipped = [];

for (const details of base.registeredScriptlets) {
  const { name, fn } = details;
  if (typeof name !== 'string' || typeof fn !== 'function') { skipped.push(String(name)); continue; }
  const isHelper = name.endsWith('.fn');
  if (!isHelper && !name.endsWith('.js')) { skipped.push(name); continue; }
  if (seen.has(name)) { continue; }
  seen.add(name);

  const resource = {
    name,
    aliases: Array.isArray(details.aliases) ? [...details.aliases] : [],
    kind: { mime: isHelper ? 'fn/javascript' : 'application/javascript' },
    content: Buffer.from(fn.toString(), 'utf8').toString('base64'),
  };
  const deps = (details.dependencies || []).filter((d) => typeof d === 'string');
  if (deps.length) { resource.dependencies = deps; }
  if (details.requiresTrust) { resource.permission = TRUSTED; }
  resources.push(resource);
}

// Every dependency must itself be a resource, or the scriptlet can never run.
const names = new Set(resources.map((r) => r.name));
const missing = [];
for (const r of resources) {
  for (const d of r.dependencies || []) { if (!names.has(d)) { missing.push(`${r.name} -> ${d}`); } }
}
if (missing.length) {
  console.error('Unresolved dependencies:\n  ' + missing.join('\n  '));
  process.exit(1);
}

writeFileSync(outFile, JSON.stringify(resources));
const scriptlets = resources.filter((r) => r.kind.mime === 'application/javascript').length;
console.log(`wrote ${resources.length} resources (${scriptlets} scriptlets, ${resources.length - scriptlets} helpers, ` +
            `${resources.filter((r) => r.permission).length} trusted) to ${outFile}`);
if (skipped.length) { console.log(`skipped ${skipped.length} non-JS registrations`); }
