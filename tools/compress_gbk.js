const fs = require('fs');

// Read the hex table
const hex = fs.readFileSync('C:/Users/yjw/esp32-bridge/firmware/gbk_table.hex', 'utf8').trim();
console.log('Hex length:', hex.length, 'chars');

// Parse entries
const entries = [];
for (let i = 0; i < hex.length; i += 8) {
  const uni = parseInt(hex.substr(i, 4), 16);
  const gbk = parseInt(hex.substr(i + 4, 4), 16);
  entries.push({ uni, gbk });
}
console.log('Total entries:', entries.length);

// Find consecutive Unicode ranges where GBK is also consecutive
const ranges = [];
let start = 0;
for (let i = 1; i <= entries.length; i++) {
  const uniConsecutive = (i < entries.length) && (entries[i].uni === entries[i-1].uni + 1);
  const gbkConsecutive = (i < entries.length) && (entries[i].gbk === entries[i-1].gbk + 1);
  
  if (!uniConsecutive || !gbkConsecutive) {
    // End of a range
    const len = i - start;
    ranges.push({
      startUni: entries[start].uni,
      len: len,
      startGbk: entries[start].gbk
    });
    start = i;
  }
}

console.log('\nTotal ranges:', ranges.length);

// Calculate compressed size
// Format per range: startUni(4hex) + len(2hex) + startGbk(4hex) = 10 hex chars
let compressedHex = '';
for (const r of ranges) {
  compressedHex += r.startUni.toString(16).padStart(4, '0');
  compressedHex += r.len.toString(16).padStart(2, '0');
  compressedHex += r.startGbk.toString(16).padStart(4, '0');
}
console.log('Compressed hex length:', compressedHex.length, 'chars (' + (compressedHex.length / 1024).toFixed(1) + ' KB)');

// Check if any range has len > 255 (would overflow 1-byte length)
const overflow = ranges.filter(r => r.len > 255);
if (overflow.length > 0) {
  console.log('\nWARNING:', overflow.length, 'ranges have len > 255!');
  for (const o of overflow) {
    console.log('  startUni=' + o.startUni.toString(16) + ' len=' + o.len + ' startGbk=' + o.startGbk.toString(16));
  }
}

// Distribution of range lengths
const lenDist = {};
for (const r of ranges) {
  lenDist[r.len] = (lenDist[r.len] || 0) + 1;
}
console.log('\nRange length distribution:');
const sortedLens = Object.keys(lenDist).map(Number).sort((a,b) => a-b);
for (const l of sortedLens) {
  console.log('  len=' + l + ': ' + lenDist[l] + ' ranges');
}

// Verify a few characters from the compressed table
function lookupCompressed(cp) {
  // Binary search on ranges
  let lo = 0, hi = ranges.length - 1;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    const r = ranges[mid];
    if (cp >= r.startUni && cp < r.startUni + r.len) {
      const offset = cp - r.startUni;
      return r.startGbk + offset;
    } else if (cp < r.startUni) {
      hi = mid - 1;
    } else {
      lo = mid + 1;
    }
  }
  return null; // not found
}

console.log('\nVerification from compressed table:');
const tests = [
  ['你', 0x4F60, 0xC4E3],
  ['好', 0x597D, 0xBAC3],
  ['中', 0x4E2D, 0xD6D0],
  ['文', 0x6587, 0xCEC4],
  ['浣', 0x6D63, 0xBAB4],
];
for (const [ch, cp, expected] of tests) {
  const result = lookupCompressed(cp);
  const ok = result === expected ? 'OK' : 'WRONG';
  console.log(`  "${ch}" U+${cp.toString(16).toUpperCase()} -> GBK 0x${(result||0).toString(16).toUpperCase()} (expected 0x${expected.toString(16).toUpperCase()}) [${ok}]`);
}

// Write compressed table
fs.writeFileSync('C:/Users/yjw/esp32-bridge/firmware/gbk_compressed.hex', compressedHex);
console.log('\nCompressed table written to gbk_compressed.hex');
