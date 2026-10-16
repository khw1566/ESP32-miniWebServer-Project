// Execute actual C status-render/normalization bodies with host object mocks.
// Does not compile firmware, render LVGL or prove hardware telemetry.
const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert');
const root = path.resolve(__dirname, '..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');
function body(source, name) {
  const m = new RegExp('\\b' + name + '\\([^;]*?\\)\\s*\\{').exec(source);
  assert(m, name); let i = m.index + m[0].length, start = i, depth = 1;
  while (depth) { if (source[i] === '{') depth++; if (source[i] === '}') depth--; i++; }
  return source.slice(start, i - 1);
}
const ui = read('main/m3_ui.c'), power = read('main/m3_power.c');
const ctx = { CYAN: 'cyan', RED: 'red', TEXT: 'white', MUTED: 'muted',
  power_visible: (o, v) => { o.visible = !!v; },
  lv_obj_set_width: (o, w) => { o.width = Math.trunc(w); },
  set_bg: (o, c) => { o.bg = c; }, set_ink: (o, c) => { o.ink = c; },
  set_text: (o, t) => { o.text = t; } };
for (const name of ['power_plug', 'power_battery', 'power_fill', 'power_charge', 'power_percent']) ctx[name] = {};
vm.createContext(ctx);
let render = body(ui, 'update_power').replaceAll('p->', 'p.')
  .replace(/\b(bool|unsigned|uint32_t)\s+/g, 'let ')
  .replaceAll('(int)', '').replace(/(\d+)u\b/g, '$1')
  .replace('char text[8];', 'let text;')
  .replace('snprintf(text, sizeof(text), "%u%%", percent)', 'text = percent + "%"')
  .replace('snprintf(text, sizeof(text), "--")', 'text = "--"');
vm.runInContext('function render(p){' + render + '}', ctx);
vm.runInContext('function normalize(s){' + body(power, 'm3_power_normalize').replaceAll('s->', 's.') + '}', ctx);
const fixture = extra => Object.assign({battery_known:false, battery_present:false, percent_known:false,
  wired_known:false, wired:false, charge_known:false, charging:false, percent:0, millivolts:0}, extra);
ctx.render(fixture());
assert.equal(ctx.power_percent.text, '--'); assert(!ctx.power_plug.visible); assert(!ctx.power_fill.visible);
assert(!ctx.power_charge.visible); assert(ctx.power_battery.visible);
ctx.render(fixture({battery_known:true, wired_known:true, wired:true}));
assert(ctx.power_plug.visible); assert(!ctx.power_battery.visible); assert(!ctx.power_percent.visible);
const battery = {battery_known:true, battery_present:true, percent_known:true, wired_known:true};
for (const [percent, text, color, fill] of [[0,'0%','red',false], [9,'9%','red',true],
  [20,'20%','red',true], [67,'67%','white',true], [100,'100%','white',true], [255,'100%','white',true]]) {
  ctx.render(fixture({...battery, percent}));
  assert.equal(ctx.power_percent.text, text); assert.equal(ctx.power_percent.ink, color);
  assert.equal(ctx.power_fill.visible, fill); assert(ctx.power_fill.width >= 1 && ctx.power_fill.width <= 22);
}
ctx.render(fixture({...battery, percent:67, wired:true, charge_known:true, charging:true}));
assert(ctx.power_plug.visible); assert(ctx.power_charge.visible); assert.equal(ctx.power_fill.bg, 'cyan');
ctx.render(fixture({...battery, percent:100, wired:true, charge_known:true, charging:false}));
assert(ctx.power_plug.visible); assert(!ctx.power_charge.visible);
ctx.render(fixture({...battery, percent:67, charge_known:true, charging:true}));
assert(!ctx.power_charge.visible, 'known unplugged must suppress contradictory charging');
const unavailable = fixture({battery_present:true, percent_known:true, percent:67, wired:true, charging:true});
ctx.normalize(unavailable);
assert(!unavailable.battery_present && !unavailable.percent_known && !unavailable.wired && !unavailable.charging);
assert.equal(unavailable.percent, 0);
assert(/i2c_master_probe\(bus, 0x75, 25\)/.test(power));
assert(!/i2c_master_transmit\(/.test(power), 'never write power-control registers');
assert(/memset\(out, 0, sizeof\(\*out\)\)/.test(body(power, 'm3_power_poll')));
assert(read('main/CMakeLists.txt').includes('"m3_power.c"'));
assert(read('main/m3_runtime.c').includes('m3_power_poll(&next.power)'));
assert(ui.includes('update_power(&snapshot.power)'));
for (const scenario of ['84_power_unknown','85_power_wired_only','86_power_battery','87_power_charging','88_power_full_wired','89_power_low'])
  assert(read('tools/ui_sim/scenarios.inc').includes(scenario));
console.log('Power status: unknown/absent/wired/battery/charging/full/low, saturation and read-only diagnostics passed (host mocks only).');
