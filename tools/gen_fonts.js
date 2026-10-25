#!/usr/bin/env node
/* Generate one LVGL font without passing non-ASCII text through a shell.
 *
 * The charset is read from a UTF-8 file so that Windows PowerShell's ANSI
 * argument encoding can never mangle the glyph set (that produced a font full
 * of wrong code points, which looked like missing glyphs on the panel).
 *
 * Usage: node tools/gen_fonts.js --font <otf> --charset <file> --size 22
 *          --output <c file> [--include lvgl.h]
 */
'use strict';

const fs = require('fs');
const path = require('path');

const TOOL = 'D:/LittleWorld-Main-DSH/tools/fonts/node_modules/lv_font_conv';
const cli = require(path.join(TOOL, 'lib/cli'));

function parseArgs(argv) {
  const out = {};
  for (let i = 0; i < argv.length; i++) {
    const key = argv[i];
    if (!key.startsWith('--')) throw new Error(`unexpected argument ${key}`);
    out[key.slice(2)] = argv[++i];
  }
  return out;
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  const charsetPath = args.charset;
  const charset = fs.readFileSync(charsetPath, 'utf8').replace(/[\r\n]/g, '');
  const argv = [
    '--font', args.font,
    '--range', '0x20-0x7E',
  ];
  if (charset.length > 0) argv.push('--symbols', charset);
  argv.push(
    '--size', args.size,
    '--bpp', '4',
    '--format', 'lvgl',
    '--no-compress',
    '--lv-include', args.include || 'lvgl.h',
    '--output', args.output,
  );
  await cli.run(argv);
  const unique = new Set([...charset].filter(c => c.codePointAt(0) >= 0x7f));
  console.log(`  ${path.basename(args.output)}: ${unique.size} non-ASCII glyphs requested`);
}

main().catch(err => {
  console.error(err && err.message ? err.message : err);
  process.exit(1);
});
