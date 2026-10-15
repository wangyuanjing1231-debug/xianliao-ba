const iconv = require('iconv-lite');
const fs = require('fs');
const path = require('path');

// Generate GBK lookup table: pairs of (Unicode codepoint, GBK code)
// Sorted by Unicode codepoint for binary search

const entries = []; // each: { uni: number, gbk: number }

for (let lead = 0x81; lead <= 0xFE; lead++) {
  const trails = [];
  for (let t = 0x40; t <= 0x7E; t++) trails.push(t);
  for (let t = 0x80; t <= 0xFE; t++) trails.push(t);
  
  for (const trail of trails) {
    const buf = Buffer.from([lead, trail]);
    const ch = iconv.decode(buf, 'gbk');
    if (ch && ch.length === 1) {
      const cp = ch.charCodeAt(0);
      // Skip replacement char and question mark (unassigned)
      if (cp !== 0xFFFD && cp !== 0x003F) {
        const gbkCode = lead * 256 + trail;
        entries.push({ uni: cp, gbk: gbkCode });
      }
    }
  }
}

// Sort by Unicode codepoint
entries.sort((a, b) => a.uni - b.uni);

// Remove duplicates (same Unicode char mapped from multiple GBK codes)
// Keep first occurrence (which has the lowest GBK code due to iteration order)
const unique = [];
let lastUni = -1;
for (const e of entries) {
  if (e.uni !== lastUni) {
    unique.push(e);
    lastUni = e.uni;
  }
}

console.log('Total unique entries: ' + unique.length);

// Build hex string: 4 bytes per entry (2 Unicode + 2 GBK) = 8 hex chars
let hex = '';
for (const e of unique) {
  hex += e.uni.toString(16).padStart(4, '0');
  hex += e.gbk.toString(16).padStart(4, '0');
}

console.log('Hex string length: ' + hex.length + ' chars (' + (hex.length / 1024).toFixed(1) + ' KB)');

// Verify known characters
function verify(ch, expectedGBK) {
  const cp = ch.charCodeAt(0);
  // Binary search
  let lo = 0, hi = unique.length - 1;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (unique[mid].uni === cp) {
      const gbk = unique[mid].gbk;
      const ok = gbk === expectedGBK ? 'OK' : 'WRONG';
      console.log(`  "${ch}" U+${cp.toString(16).toUpperCase()} -> GBK 0x${gbk.toString(16).toUpperCase()} (expected 0x${expectedGBK.toString(16).toUpperCase()}) [${ok}]`);
      return;
    } else if (unique[mid].uni < cp) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  console.log(`  "${ch}" U+${cp.toString(16).toUpperCase()} -> NOT FOUND`);
}

console.log('\nVerification:');
verify('你', 0xC4E3);  // 50403
verify('好', 0xBAC3);  // 
verify('中', 0xD6D0);  // 
verify('文', 0xCEC4);  // 
verify('浣', 0xBAB4); // the wrong char we were getting

// Write the hex table
fs.writeFileSync(path.join(__dirname, 'gbk_table.hex'), hex);
console.log('\nTable written to gbk_table.hex');
