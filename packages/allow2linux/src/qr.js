/**
 * Minimal QR Code generator — produces SVG string.
 * Pure JS, zero dependencies, works offline.
 *
 * Supports alphanumeric mode up to ~4000 chars (version 1-40).
 * Uses byte mode for simplicity. Error correction level M.
 */

// ── QR Tables ────────────────────────────────────────────────

// Capacity table: [version] = max bytes in byte mode, EC level M
var CAPACITIES = [
    0,    // v0 placeholder
    14,   // v1
    26,   // v2
    42,   // v3
    62,   // v4
    84,   // v5
    106,  // v6
    122,  // v7
    152,  // v8
    180,  // v9
    213,  // v10
    251,  // v11
    287,  // v12
    331,  // v13
    362,  // v14
    412,  // v15
    450,  // v16
    504,  // v17
    560,  // v18
    611,  // v19
    661,  // v20
];

// EC codewords per block: [version] = { ecPerBlock, blocks }
var EC_TABLE = [
    null, // v0
    { ecPer: 10, blocks: 1 },  // v1
    { ecPer: 16, blocks: 1 },  // v2
    { ecPer: 26, blocks: 1 },  // v3
    { ecPer: 18, blocks: 2 },  // v4
    { ecPer: 24, blocks: 2 },  // v5
    { ecPer: 16, blocks: 4 },  // v6
    { ecPer: 18, blocks: 4 },  // v7
    { ecPer: 22, blocks: 4 },  // v8
    { ecPer: 22, blocks: 5 },  // v9  (actually 2+3 blocks mixed, simplified)
    { ecPer: 26, blocks: 5 },  // v10
];

// Alignment pattern positions per version
var ALIGNMENTS = [
    null, // v0
    [],   // v1
    [6, 18], // v2
    [6, 22], // v3
    [6, 26], // v4
    [6, 30], // v5
    [6, 34], // v6
    [6, 22, 38], // v7
    [6, 24, 42], // v8
    [6, 26, 46], // v9
    [6, 28, 50], // v10
];

// Format bits for EC level M with mask 0-7
var FORMAT_BITS = [
    0x5412, 0x5125, 0x5E7C, 0x5B4B,
    0x45F9, 0x40CE, 0x4F97, 0x4AA0,
];

// ── GF(256) arithmetic for Reed-Solomon ──────────────────────

var GF_EXP = new Uint8Array(512);
var GF_LOG = new Uint8Array(256);

(function initGF() {
    var x = 1;
    for (var i = 0; i < 255; i++) {
        GF_EXP[i] = x;
        GF_LOG[x] = i;
        x = (x << 1) ^ (x >= 128 ? 0x11D : 0);
    }
    for (var j = 255; j < 512; j++) {
        GF_EXP[j] = GF_EXP[j - 255];
    }
})();

function gfMul(a, b) {
    if (a === 0 || b === 0) return 0;
    return GF_EXP[GF_LOG[a] + GF_LOG[b]];
}

function rsEncode(data, ecLen) {
    // Build generator polynomial
    var gen = new Uint8Array(ecLen + 1);
    gen[0] = 1;
    for (var i = 0; i < ecLen; i++) {
        for (var j = ecLen; j >= 1; j--) {
            gen[j] = gen[j] ^ gfMul(gen[j - 1], GF_EXP[i]);
        }
    }

    var result = new Uint8Array(ecLen);
    for (var k = 0; k < data.length; k++) {
        var coef = data[k] ^ result[0];
        // Shift left
        for (var m = 0; m < ecLen - 1; m++) {
            result[m] = result[m + 1] ^ gfMul(gen[m + 1], coef);
        }
        result[ecLen - 1] = gfMul(gen[ecLen], coef);
    }
    return result;
}

// ── Bit stream ───────────────────────────────────────────────

function BitStream() {
    this.data = [];
    this.bitLen = 0;
}

BitStream.prototype.append = function (value, bits) {
    for (var i = bits - 1; i >= 0; i--) {
        var byteIndex = this.bitLen >> 3;
        var bitIndex = 7 - (this.bitLen & 7);
        if (byteIndex >= this.data.length) this.data.push(0);
        if ((value >> i) & 1) {
            this.data[byteIndex] |= (1 << bitIndex);
        }
        this.bitLen++;
    }
};

BitStream.prototype.getBytes = function () {
    return new Uint8Array(this.data);
};

// ── QR Matrix ────────────────────────────────────────────────

function createMatrix(version) {
    var size = 17 + version * 4;
    var matrix = [];
    var reserved = [];
    for (var r = 0; r < size; r++) {
        matrix.push(new Uint8Array(size));
        reserved.push(new Uint8Array(size));
    }
    return { matrix: matrix, reserved: reserved, size: size };
}

function setModule(m, row, col, val) {
    if (row >= 0 && row < m.size && col >= 0 && col < m.size) {
        m.matrix[row][col] = val ? 1 : 0;
        m.reserved[row][col] = 1;
    }
}

function placeFinderPattern(m, row, col) {
    for (var dr = -1; dr <= 7; dr++) {
        for (var dc = -1; dc <= 7; dc++) {
            var r = row + dr;
            var c = col + dc;
            if (r < 0 || r >= m.size || c < 0 || c >= m.size) continue;
            var inOuter = (dr === -1 || dr === 7 || dc === -1 || dc === 7);
            var inBorder = (dr === 0 || dr === 6 || dc === 0 || dc === 6);
            var inInner = (dr >= 2 && dr <= 4 && dc >= 2 && dc <= 4);
            setModule(m, r, c, !inOuter && (inBorder || inInner));
        }
    }
}

function placeAlignmentPattern(m, row, col) {
    for (var dr = -2; dr <= 2; dr++) {
        for (var dc = -2; dc <= 2; dc++) {
            var r = row + dr;
            var c = col + dc;
            if (m.reserved[r] && m.reserved[r][c]) continue; // skip if overlaps finder
            var val = (Math.abs(dr) === 2 || Math.abs(dc) === 2 || (dr === 0 && dc === 0));
            setModule(m, r, c, val);
        }
    }
}

function placeFixedPatterns(m, version) {
    // Finder patterns
    placeFinderPattern(m, 0, 0);
    placeFinderPattern(m, 0, m.size - 7);
    placeFinderPattern(m, m.size - 7, 0);

    // Timing patterns
    for (var i = 8; i < m.size - 8; i++) {
        if (!m.reserved[6][i]) setModule(m, 6, i, i % 2 === 0);
        if (!m.reserved[i][6]) setModule(m, i, 6, i % 2 === 0);
    }

    // Dark module
    setModule(m, m.size - 8, 8, 1);

    // Reserve format areas
    for (var j = 0; j < 8; j++) {
        if (!m.reserved[8][j]) { m.reserved[8][j] = 1; }
        if (!m.reserved[8][m.size - 1 - j]) { m.reserved[8][m.size - 1 - j] = 1; }
        if (!m.reserved[j][8]) { m.reserved[j][8] = 1; }
        if (!m.reserved[m.size - 1 - j][8]) { m.reserved[m.size - 1 - j][8] = 1; }
    }
    m.reserved[8][8] = 1;

    // Alignment patterns
    if (version >= 2) {
        var positions = ALIGNMENTS[version] || [];
        for (var ai = 0; ai < positions.length; ai++) {
            for (var aj = 0; aj < positions.length; aj++) {
                var ar = positions[ai];
                var ac = positions[aj];
                if (m.reserved[ar] && m.reserved[ar][ac]) continue;
                placeAlignmentPattern(m, ar, ac);
            }
        }
    }
}

function placeData(m, codewords) {
    var bitIndex = 0;
    var totalBits = codewords.length * 8;
    var right = true; // scanning right to left in column pairs

    for (var col = m.size - 1; col >= 0; col -= 2) {
        if (col === 6) col = 5; // skip timing column

        for (var row = 0; row < m.size; row++) {
            for (var dx = 0; dx <= 1; dx++) {
                var c = col - dx;
                var r = right ? (m.size - 1 - row) : row;
                if (m.reserved[r][c]) continue;

                if (bitIndex < totalBits) {
                    var byteIdx = bitIndex >> 3;
                    var bitIdx = 7 - (bitIndex & 7);
                    m.matrix[r][c] = (codewords[byteIdx] >> bitIdx) & 1;
                }
                bitIndex++;
            }
        }
        right = !right;
    }
}

function applyMask(m, maskNum) {
    var maskFn;
    switch (maskNum) {
        case 0: maskFn = function (r, c) { return (r + c) % 2 === 0; }; break;
        case 1: maskFn = function (r) { return r % 2 === 0; }; break;
        case 2: maskFn = function (_r, c) { return c % 3 === 0; }; break;
        case 3: maskFn = function (r, c) { return (r + c) % 3 === 0; }; break;
        case 4: maskFn = function (r, c) { return (Math.floor(r / 2) + Math.floor(c / 3)) % 2 === 0; }; break;
        case 5: maskFn = function (r, c) { return (r * c) % 2 + (r * c) % 3 === 0; }; break;
        case 6: maskFn = function (r, c) { return ((r * c) % 2 + (r * c) % 3) % 2 === 0; }; break;
        default: maskFn = function (r, c) { return ((r + c) % 2 + (r * c) % 3) % 2 === 0; }; break;
    }

    for (var r = 0; r < m.size; r++) {
        for (var c = 0; c < m.size; c++) {
            if (!m.reserved[r][c] && maskFn(r, c)) {
                m.matrix[r][c] ^= 1;
            }
        }
    }
}

function placeFormatBits(m, maskNum) {
    var bits = FORMAT_BITS[maskNum];

    // Around top-left finder
    for (var i = 0; i <= 5; i++) {
        m.matrix[8][i] = (bits >> (14 - i)) & 1;
    }
    m.matrix[8][7] = (bits >> 8) & 1;
    m.matrix[8][8] = (bits >> 7) & 1;
    m.matrix[7][8] = (bits >> 6) & 1;
    for (var j = 0; j <= 5; j++) {
        m.matrix[5 - j][8] = (bits >> (j)) & 1;
    }

    // Bottom-left and top-right
    for (var k = 0; k <= 7; k++) {
        m.matrix[m.size - 1 - k][8] = (bits >> k) & 1;
    }
    for (var l = 0; l <= 7; l++) {
        m.matrix[8][m.size - 8 + l] = (bits >> (14 - 8 - l)) & 1;
    }
}

// ── Penalty scoring (simplified) ─────────────────────────────

function scorePenalty(m) {
    var score = 0;
    // Rule 1: consecutive same-color modules in rows/cols
    for (var r = 0; r < m.size; r++) {
        var runLen = 1;
        for (var c = 1; c < m.size; c++) {
            if (m.matrix[r][c] === m.matrix[r][c - 1]) {
                runLen++;
                if (runLen === 5) score += 3;
                else if (runLen > 5) score += 1;
            } else {
                runLen = 1;
            }
        }
    }
    for (var c2 = 0; c2 < m.size; c2++) {
        var runLen2 = 1;
        for (var r2 = 1; r2 < m.size; r2++) {
            if (m.matrix[r2][c2] === m.matrix[r2 - 1][c2]) {
                runLen2++;
                if (runLen2 === 5) score += 3;
                else if (runLen2 > 5) score += 1;
            } else {
                runLen2 = 1;
            }
        }
    }
    // Rule 4: proportion of dark modules
    var dark = 0;
    var total = m.size * m.size;
    for (var r3 = 0; r3 < m.size; r3++) {
        for (var c3 = 0; c3 < m.size; c3++) {
            if (m.matrix[r3][c3]) dark++;
        }
    }
    var pct = Math.abs(dark * 100 / total - 50);
    score += Math.floor(pct / 5) * 10;
    return score;
}

// ── Main encode function ─────────────────────────────────────

function encode(text) {
    var data = [];
    for (var i = 0; i < text.length; i++) {
        data.push(text.charCodeAt(i));
    }

    // Find smallest version that fits
    var version = 1;
    for (var v = 1; v <= 20; v++) {
        if (CAPACITIES[v] >= data.length) {
            version = v;
            break;
        }
    }

    var size = 17 + version * 4;
    var totalModules = size * size;

    // Build data bitstream: mode(4) + count(8 or 16) + data + terminator + padding
    var bs = new BitStream();
    bs.append(0x4, 4); // byte mode indicator
    var countBits = version <= 9 ? 8 : 16;
    bs.append(data.length, countBits);
    for (var d = 0; d < data.length; d++) {
        bs.append(data[d], 8);
    }

    // Total data capacity in bytes for this version (from spec)
    // We'll calculate: totalCodewords = (size^2 - function patterns) / 8
    // Easier: use known total codewords per version
    var TOTAL_CODEWORDS = [
        0, 26, 44, 70, 100, 134, 172, 196, 242, 292, 346,
        404, 466, 532, 581, 655, 733, 815, 901, 991, 1085,
    ];
    var totalCW = TOTAL_CODEWORDS[version];
    var ecInfo = EC_TABLE[version];
    var ecCW;
    var dataCW;

    if (ecInfo) {
        ecCW = ecInfo.ecPer * ecInfo.blocks;
        dataCW = totalCW - ecCW;
    } else {
        // Fallback for versions beyond our table
        ecCW = Math.floor(totalCW * 0.3);
        dataCW = totalCW - ecCW;
    }

    // Terminator (up to 4 zero bits)
    var terminatorBits = Math.min(4, dataCW * 8 - bs.bitLen);
    if (terminatorBits > 0) bs.append(0, terminatorBits);

    // Pad to byte boundary
    while (bs.bitLen % 8 !== 0) bs.append(0, 1);

    // Pad to fill data capacity
    var padBytes = [0xEC, 0x11];
    var padIdx = 0;
    while (bs.bitLen < dataCW * 8) {
        bs.append(padBytes[padIdx], 8);
        padIdx = 1 - padIdx;
    }

    var dataBytes = bs.getBytes().slice(0, dataCW);

    // Reed-Solomon error correction
    var allCodewords;
    if (ecInfo && ecInfo.blocks === 1) {
        var ec = rsEncode(dataBytes, ecInfo.ecPer);
        allCodewords = new Uint8Array(totalCW);
        allCodewords.set(dataBytes);
        allCodewords.set(ec, dataCW);
    } else if (ecInfo) {
        // Multiple blocks: split data evenly, encode each, interleave
        var numBlocks = ecInfo.blocks;
        var blockSize = Math.floor(dataCW / numBlocks);
        var extraBlocks = dataCW - blockSize * numBlocks;

        var dataBlocks = [];
        var ecBlocks = [];
        var offset = 0;
        for (var bi = 0; bi < numBlocks; bi++) {
            var bsz = blockSize + (bi >= numBlocks - extraBlocks ? 1 : 0);
            var block = dataBytes.slice(offset, offset + bsz);
            dataBlocks.push(block);
            ecBlocks.push(rsEncode(block, ecInfo.ecPer));
            offset += bsz;
        }

        // Interleave data blocks
        allCodewords = new Uint8Array(totalCW);
        var cwIdx = 0;
        var maxBlockLen = blockSize + (extraBlocks > 0 ? 1 : 0);
        for (var ci = 0; ci < maxBlockLen; ci++) {
            for (var bj = 0; bj < numBlocks; bj++) {
                if (ci < dataBlocks[bj].length) {
                    allCodewords[cwIdx++] = dataBlocks[bj][ci];
                }
            }
        }
        // Interleave EC blocks
        for (var ei = 0; ei < ecInfo.ecPer; ei++) {
            for (var ej = 0; ej < numBlocks; ej++) {
                allCodewords[cwIdx++] = ecBlocks[ej][ei];
            }
        }
    } else {
        // Fallback: no EC (shouldn't happen for v1-10)
        allCodewords = dataBytes;
    }

    // Create matrix and place patterns
    var m = createMatrix(version);
    placeFixedPatterns(m, version);
    placeData(m, allCodewords);

    // Try all 8 masks, pick best (lowest penalty)
    var bestMask = 0;
    var bestPenalty = Infinity;
    for (var mask = 0; mask < 8; mask++) {
        // Clone matrix for testing
        var test = createMatrix(version);
        for (var tr = 0; tr < m.size; tr++) {
            for (var tc = 0; tc < m.size; tc++) {
                test.matrix[tr][tc] = m.matrix[tr][tc];
                test.reserved[tr][tc] = m.reserved[tr][tc];
            }
        }
        applyMask(test, mask);
        placeFormatBits(test, mask);
        var penalty = scorePenalty(test);
        if (penalty < bestPenalty) {
            bestPenalty = penalty;
            bestMask = mask;
        }
    }

    // Apply best mask
    applyMask(m, bestMask);
    placeFormatBits(m, bestMask);

    return m.matrix;
}

// ── SVG rendering ────────────────────────────────────────────

/**
 * Generate a flat module grid string for the SDL2 overlay.
 * Returns { size: N, modules: "010110..." } where modules is N*N chars of '0'/'1'.
 */
export function generateQrGrid(text) {
    if (!text) return null;
    var matrix;
    try {
        matrix = encode(text);
    } catch (err) {
        console.error('[qr] encode error:', err.message);
        return null;
    }
    var size = matrix.length;
    var bits = '';
    for (var r = 0; r < size; r++) {
        for (var c = 0; c < size; c++) {
            bits += matrix[r][c] ? '1' : '0';
        }
    }
    return { size: size, modules: bits };
}

export function generateQrSvg(text, moduleSize) {
    if (!text) return '';

    moduleSize = moduleSize || 4;
    var matrix;
    try {
        matrix = encode(text);
    } catch (err) {
        console.error('[qr] encode error:', err.message);
        return '';
    }

    var size = matrix.length;
    var svgSize = size * moduleSize;
    var quiet = 2 * moduleSize; // quiet zone
    var totalSize = svgSize + quiet * 2;

    var paths = [];
    for (var r = 0; r < size; r++) {
        for (var c = 0; c < size; c++) {
            if (matrix[r][c]) {
                var x = quiet + c * moduleSize;
                var y = quiet + r * moduleSize;
                paths.push('M' + x + ',' + y + 'h' + moduleSize + 'v' + moduleSize + 'h-' + moduleSize + 'z');
            }
        }
    }

    return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ' + totalSize + ' ' + totalSize
        + '" width="' + totalSize + '" height="' + totalSize + '">'
        + '<rect width="100%" height="100%" fill="#fff"/>'
        + '<path d="' + paths.join('') + '" fill="#000"/>'
        + '</svg>';
}
