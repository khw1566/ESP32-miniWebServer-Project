// Every non-ASCII character drawn by the on-board UI must exist in font_m1.c;
// Montserrat (the fallback) covers ASCII only, so a missing glyph shows as a box.
const fs=require('fs'),path=require('path'),assert=require('assert');
const main=path.resolve(__dirname,'../main');
const sources=['onboard_ui.c','m1_keyboard.c','m1_dashboard.c'];
const font=fs.readFileSync(path.join(main,'font_m1.c'),'utf8');
const codes=new Set(font.match(/codes\[\] = \{([^}]*)\}/)[1].split(',').map(Number));
const missing=new Set();
for(const file of sources)for(const ch of fs.readFileSync(path.join(main,file),'utf8'))
 if(ch.codePointAt(0)>0x7e&&!/\s/.test(ch)&&!codes.has(ch.codePointAt(0)))missing.add(ch);
assert.equal(missing.size,0,'Run tools/generate_font.ps1; missing glyphs: '+[...missing].join(''));
console.log('PASS: '+codes.size+' glyphs cover all non-ASCII text in '+sources.join(', ')+'.');
