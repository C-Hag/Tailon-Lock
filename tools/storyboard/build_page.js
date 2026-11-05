#!/usr/bin/env node
// build_page.js - the storyboard editor as one self-contained page: editor.html with the intro's core, its current
// script (assets/intro/intro_script.json) and the models (build_models.js) put in.
// Usage: node tools/storyboard/build_page.js [out.html]   (default tools/storyboard/out/storyboard.html)
'use strict';
const fs = require('fs'), path = require('path');
const ROOT = path.resolve(__dirname, '../..');
const out = process.argv[2] || path.join(__dirname, 'out/storyboard.html');
const models = path.join(__dirname, 'models.json');
if (!fs.existsSync(models)) require('child_process').execFileSync(process.execPath, [path.join(__dirname, 'build_models.js'), models], { stdio: 'inherit' });
const core = fs.readFileSync(path.join(ROOT, 'assets/intro/src/intro_core.js'), 'utf8');
const script = fs.readFileSync(path.join(ROOT, 'assets/intro/intro_script.json'), 'utf8').trim();
const safe = s => s.replace(/<\/script/gi, '<\\/script');
let page = fs.readFileSync(path.join(__dirname, 'editor.html'), 'utf8');
page = page.replace('//__INTRO_CORE__', () => safe(core))
  .replace('/*__MODELS__*/null', () => safe(fs.readFileSync(models, 'utf8').trim()))
  .replace('/*__SCRIPT__*/null', () => safe(script));
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, page);
console.log(`${out}: ${(page.length / 1e6).toFixed(2)} MB`);
