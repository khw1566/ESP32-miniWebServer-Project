// Real gzip round trips and C binary streaming with host mocks; no compiler.
const fs=require('fs'),path=require('path'),zlib=require('zlib'),vm=require('vm'),assert=require('node:assert/strict'),{spawnSync}=require('child_process');
const root=path.resolve(__dirname,'..'),read=f=>fs.readFileSync(path.join(root,f),'utf8');
let admin=read('main/web_page_admin.c').split('const char HTML_ADMIN_DASHBOARD[] =')[1];
const defs=read('main/web_sd_admin_ui.inc');
for(const name of ['SD_ADMIN_HTML','SD_ADMIN_SCRIPT'])admin=admin.replace(name,defs.split('#define '+name)[1].split('#define')[0].replace(/\\\r?\n/g,''));
const decode=s=>Buffer.from([...s.matchAll(/"(?:\\.|[^"\\])*"/g)].map(m=>JSON.parse(m[0])).join(''));
const raw=[decode(admin),decode(read('main/web_page_ai.c').split('const char HTML_AI_STUDIO[] =')[1])];
const generated=read('main/web_pages_gzip.c'),payloads=[];
for(const [i,name] of ['HTML_ADMIN_DASHBOARD','HTML_AI_STUDIO'].entries()){
 const bytes=generated.split(`const unsigned char ${name}_GZIP[] = {`)[1].split('};')[0];
 const payload=Buffer.from([...bytes.matchAll(/0x([0-9a-f]{2})/g)].map(m=>parseInt(m[1],16)));payloads.push(payload);
 assert.deepEqual(zlib.gunzipSync(payload),raw[i],'decompressed page is byte-for-byte identical');
 assert(payload.includes(0),'binary includes zeros and must not use strlen');
 const broken=Buffer.from(payload);broken[broken.length-8]^=1;assert.throws(()=>zlib.gunzipSync(broken),'CRC check detects damage');
}
const reduction=raw.reduce((n,p)=>n+p.length+1,0)-payloads.reduce((n,p)=>n+p.length,0);
assert(reduction>100000,'lossless data saving exceeds the logged 44,032-byte overflow');
const source=read('main/web_server.c');
function body(name){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);let start=m.index+m[0].length,end=start,depth=1;while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}return source.slice(start,end-1);}
const streaming=body('send_chunk_bytes').replace(/\b(?:const size_t|size_t) (\w+)/g,'var $1').replace('str + sent','str.subarray(sent)').replaceAll('(int)','').replaceAll('NULL','null');
function send(payload,fail=-1){const chunks=[],ctx={ESP_OK:0,ESP_FAIL:-1,ESP_LOGE:()=>{},TAG:'',httpd_resp_send_chunk:(req,b,n)=>{if(chunks.length===fail)return -1;chunks.push(b?Buffer.from(b.subarray(0,n)):null);return 0;}};vm.createContext(ctx);vm.runInContext('function send(req,str,total_len){'+streaming+'}',ctx);return {result:ctx.send({},payload,payload.length),chunks};}
for(const payload of payloads){const result=send(payload);assert.equal(result.result,0);assert.equal(result.chunks.at(-1),null);assert.deepEqual(Buffer.concat(result.chunks.slice(0,-1)),payload);assert(result.chunks.slice(0,-1).every(b=>b.length<=2048));}
assert.equal(send(payloads[0],1).result,-1,'send error terminates streaming');
assert(body('send_gzip_page').includes('"Content-Encoding", "gzip"'));
assert(body('ai_studio_get_handler').indexOf('ai_require_auth')<body('ai_studio_get_handler').indexOf('send_gzip_page'),'AI authentication preserved');
assert(!/send_chunk_string\(req, HTML_(?:ADMIN_DASHBOARD|AI_STUDIO)\)/.test(source),'no firmware references keep plaintext pages linked');
// Configure the actual asset hook under a compiler-free CMake project, including
// regeneration dependencies and paths containing spaces.
const fixture=path.join(root,'results/web asset fixture');fs.mkdirSync(fixture,{recursive:true});
const python=process.env.PYTHON||'python',slash=s=>s.replaceAll('\\','/');
fs.writeFileSync(path.join(fixture,'CMakeLists.txt'),`cmake_minimum_required(VERSION 3.20)
project(asset_fixture LANGUAGES NONE)
function(idf_build_get_property out prop)
 set(${'$'}{out} "${slash(python)}" PARENT_SCOPE)
endfunction()
include("${slash(root)}/cmake/web_assets.cmake")
m3_web_assets()
get_property(watched DIRECTORY PROPERTY CMAKE_CONFIGURE_DEPENDS)
file(WRITE "${slash(fixture)}/watched.txt" "${'$'}{watched}")
`);
const run=spawnSync('cmake',['-S',fixture,'-B',path.join(fixture,'configured')],{encoding:'utf8'});assert.equal(run.status,0,run.error?.message||run.stdout+run.stderr);
for(const name of ['web_page_ai.c','web_page_admin.c','web_sd_admin_ui.inc','generate_web_gzip.py'])assert(fs.readFileSync(path.join(fixture,'watched.txt'),'utf8').includes(name));
assert.equal(read('main/web_pages_gzip.c'),generated,'regeneration is deterministic/idempotent');
console.log(`PASS: byte-identical HTML, gzip CRC, binary streaming/errors/auth, compiler-free CMake regeneration with spaced paths; ${reduction} bytes of source-data saving.`);
