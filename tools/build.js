// Сборка веб-панели: web/src -> web/dist/index.html и firmware/lift/webpage.h (gzip в PROGMEM).
// Запуск: node tools/build.js
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const root = path.join(__dirname, '..');
const rd = (p) => fs.readFileSync(path.join(root, p), 'utf8');

let html = rd('web/src/index.template.html');
const put = (marker, text) => { html = html.replace(marker, () => text); };
put('/*CSS*/', rd('web/src/style.css'));
put('/*DISPATCHER*/', rd('web/src/dispatcher.js'));
put('/*SIM*/', rd('web/src/sim.js'));
put('/*APP*/', rd('web/src/app.js'));

fs.mkdirSync(path.join(root, 'web/dist'), { recursive: true });
fs.writeFileSync(path.join(root, 'web/dist/index.html'), html);

const gz = zlib.gzipSync(Buffer.from(html, 'utf8'), { level: 9 });
let out = '// Файл создан tools/build.js, руками не править.\n#pragma once\n#include <Arduino.h>\n\n';
out += `const size_t WEBPAGE_GZ_LEN = ${gz.length};\nconst uint8_t WEBPAGE_GZ[] PROGMEM = {\n`;
const lines = [];
for (let i = 0; i < gz.length; i += 20) lines.push('  ' + Array.from(gz.subarray(i, i + 20)).map((b) => '0x' + b.toString(16).padStart(2, '0')).join(','));
out += lines.join(',\n') + '\n};\n';
fs.writeFileSync(path.join(root, 'firmware/lift/webpage.h'), out);
console.log(`index.html ${html.length} байт, gzip ${gz.length} байт`);
