// Execute the actual boot loader/range/lookup C bodies with SD and heap fixtures. No compiler.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8'),source=read('main/m4_web_assets.c');
function body(name){const m=new RegExp('\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let start=m.index+m[0].length,i=start,d=1;while(d){if(source[i]==='{')d++;if(source[i]==='}')d--;i++;}return source.slice(start,i-1);}
const clean=s=>s.replace(/\/\*[\s\S]*?\*\//g,'').replaceAll('->','.').replace(/\((?:void \*|uint64_t|unsigned|size_t)\)/g,'').replace(/\b(?:const )?(?:unsigned long|unsigned|size_t|int|bool|esp_err_t) /g,'var ');
const c={Buffer,NULL:null,strlen:s=>Buffer.byteLength(s),strcmp:(a,b)=>a===b?0:1,
 strcasecmp:(a,b)=>a.toLowerCase()===b.toLowerCase()?0:1,
 strncmp:(a,b,n)=>a.slice(0,n)===b.slice(0,n)?0:1,strchr:(s,ch)=>s.includes(typeof ch==='number'?String.fromCharCode(ch):ch),m4_utf8_valid:()=>true,
 atomic_load:name=>c[name],atomic_store:(name,v)=>c[name]=v,loaded:false,assets:null,asset_count:0,asset_bytes:0,
 m4_web_assets_ready:()=>c.loaded};
vm.createContext(c);
let valid=body('m4_web_assets_path_valid').replace('for (const char *p = path; *p; ++p)','for (var i=0,p=path.charCodeAt(0); i<path.length; p=path.charCodeAt(++i))')
 .replace('path[0]', 'path.charCodeAt(0)').replaceAll('p[1]','path.charCodeAt(i+1)').replaceAll('(unsigned char)*p','p').replaceAll('*p','p');
valid=valid.replace(/'(?:\\.|[^'\\])'/g,x=>String(x.charCodeAt(1)));
vm.runInContext('function valid(path){'+valid+'}',c);c.m4_web_assets_path_valid=c.valid;
for(const p of ['/index.html','/assets/a.css','/이미지/사진.png'])assert(c.valid(p));
for(const p of ['/../key.json','/.private/key.json','/a/.env','/a//b','/a%2fb.js','/a\\b.js','/a?x','/a#b','/a:1','/a/'])assert(!c.valid(p),p);
let slice=body('m4_web_assets_slice').replace('!offset || !length','!out').replaceAll('*offset','out.offset').replaceAll('*length','out.length')
 .replace('const char *p = range + 6; char *tail;','var p=range.slice(6);').replace('++p','p=p.slice(1)')
 .replace('p = tail + 1','p=tail.slice(1)').replaceAll('*range','range.length').replaceAll('*tail','tail.charAt(0)')
 .replaceAll('*p','p.charAt(0)').replaceAll('strtoul(p, &tail, 10)','number(p)');
c.ERANGE=34;c.number=s=>{const text=/^\d+/.exec(s)?.[0]||'';const n=Number(text);c.tail=s.slice(text.length);if(n>0xffffffff)c.errno=34;return n;};
vm.runInContext('function slice(bytes,range,out){'+clean(slice)+'}',c);
for(const [range,offset,length] of [[null,0,100],['',0,100],['bytes=0-9',0,10],['bytes=80-',80,20],['bytes=-10',90,10],['bytes=-1000',0,100],['bytes=99-1000',99,1]]){const out={};assert(c.slice(100,range,out),range);assert.deepEqual(out,{offset,length});}
for(const range of ['bytes=-0','bytes=100-','bytes=20-10','bytes=1-2,4-5','bytes=+1-','bytes=1-+2','bytes=429496729600-','bytes=0-429496729600','bytes=1-2x','items=1-2'])assert(!c.slice(100,range,{}),range);
assert(c.slice(0,null,{}));assert(!c.slice(0,'bytes=0-',{}));
const files={'/sdcard/Main WebFlie':{dir:['index.html','assets','.private','readme.md']},
 '/sdcard/Main WebFlie/index.html':{data:Buffer.from('<html>한글</html>')},
 '/sdcard/Main WebFlie/assets':{dir:['style.css','image.png']},
 '/sdcard/Main WebFlie/assets/style.css':{data:Buffer.from('body{color:red}')},
 '/sdcard/Main WebFlie/assets/image.png':{data:Buffer.from([0,255,0,1,2,3])},
 '/sdcard/Main WebFlie/.private':{dir:['key.json']},'/sdcard/Main WebFlie/.private/key.json':{data:Buffer.from('fixture-private-data')},
 '/sdcard/Main WebFlie/readme.md':{data:Buffer.from('not a web asset')}};
let allocated,dirs,inputs,allocations;
Object.assign(c,{WEB_ASSET_ROOT:'/sdcard/Main WebFlie',CONFIG_M4_WEB_ASSET_LIMIT_KB:1024,CONFIG_M4_WEB_ASSET_MAX_FILES:512,
 ESP_OK:0,ESP_FAIL:-1,ESP_ERR_NO_MEM:1,ESP_ERR_NOT_FOUND:2,ESP_ERR_INVALID_STATE:3,ESP_ERR_INVALID_SIZE:4,ENOENT:2,
 MALLOC_CAP_SPIRAM:1,MALLOC_CAP_8BIT:2,ESP_LOGI:()=>{},ESP_LOGW:()=>{},esp_err_to_name:()=>'',
 sd_media_ready:()=>c.sd,sd_media_admin_lock:()=>{if(c.lockOK)c.locks++;return c.lockOK;},sd_media_admin_unlock:()=>c.locks--,
 S_ISDIR:m=>m==='dir',S_ISREG:m=>m==='file',pdMS_TO_TICKS:n=>n,vTaskDelay:()=>{},
 asset_mime:p=>({'.html':'text/html','.css':'text/css','.png':'image/png','.json':'application/json'}[path.extname(p)]||null),
 opendir:p=>{c.io++;if(c.missing||!files[p]?.dir){c.errno=2;return null;}const d={p,at:0,closed:false};dirs.push(d);return d;},
 readdir:d=>{assert(!d.closed);c.io++;const n=files[d.p].dir[d.at++];return n?{d_name:n}:null;},closedir:d=>{assert(!d.closed);d.closed=true;},
 stat:(p,st)=>{c.io++;const item=files[p];if(!item)return -1;st.st_mode=item.dir?'dir':'file';st.st_size=item.data?.length||0;return 0;},
 fopen:p=>{c.io++;if(c.openFail)return null;const f={p,at:0,closed:false};inputs.push(f);return f;},
 fread:(out,_size,n,f)=>{c.io++;if(c.shortRead)return 0;const bytes=files[f.p].data.subarray(f.at,f.at+n);bytes.copy(out);f.at+=bytes.length;return bytes.length;},
 ferror:()=>c.readError,fclose:f=>{assert(!f.closed);f.closed=true;},
 heap_caps_calloc:(n)=>{if(++allocations===c.failAllocation)return null;const a=n===128?Array(128).fill(null):{};allocated.add(a);return a;},
 heap_caps_malloc:n=>{if(++allocations===c.failAllocation)return null;const a=Buffer.alloc(n);allocated.add(a);return a;},
 free:a=>{if(a){assert(allocated.has(a),'double free');allocated.delete(a);}},
 discard:a=>{while(a){const next=a.next;c.free(a.asset.data);c.free(a);a=next;}}});
let load=body('m4_web_assets_load').replace(/char relative\[192\] = ""/,'var relative=""')
 .replace('char key[192], path[sizeof(WEB_ASSET_ROOT) + 192];','var key,path;')
 .replace('int n = snprintf(key, sizeof(key), "%s/%s", relative, file->d_name);','key=relative+"/"+file.d_name;var n=Buffer.byteLength(key);')
 .replace('snprintf(path, sizeof(path), "%s%s", WEB_ASSET_ROOT, key)','path=WEB_ASSET_ROOT+key')
 .replace('relative[lengths[--depth]] = 0','relative=relative.slice(0,lengths[--depth])')
 .replace('memcpy(relative, key, n + 1)','relative=key').replace('memcpy(entry->path, key, n + 1)','entry.path=key')
 .replace('entry->asset = (m4_web_asset_t){.data = data, .bytes = length, .mime = mime}','entry.asset={data,bytes:length,mime}')
 .replace('struct stat st;','var st={};').replaceAll('&st','st').replace('data + used','data.subarray(used)')
 .replaceAll('sizeof(*dirs)','4').replaceAll('sizeof(*lengths)','4').replaceAll('sizeof(*entry)','256').replaceAll('sizeof(asset_entry_t)','256').replaceAll('sizeof(key)','192')
 .replace(/(?:const )?asset_entry_t \*/g,'var ').replace('DIR **dirs','var dirs').replace(/DIR \*dir/g,'var dir')
 .replace('unsigned *lengths','var lengths').replace('struct dirent *file','var file').replace('const char *mime','var mime')
 .replace('uint8_t *data','var data').replace('FILE *input','var input').replace('&loaded','"loaded"');
load=clean(load).replaceAll('goto done;','break work;');const parts=load.split(/\bdone:/);assert.equal(parts.length,2);
vm.runInContext('function load(){work:{'+parts[0]+'}'+parts[1]+'}',c);
let find=body('m4_web_assets_find').replace('const asset_entry_t *entry','var entry').replace('&entry->asset','entry.asset');
vm.runInContext('function find(path){'+clean(find)+'}',c);
function run(options={}){allocated=new Set();dirs=[];inputs=[];allocations=0;Object.assign(c,{sd:true,lockOK:true,locks:0,loaded:false,assets:null,io:0,missing:false,openFail:false,shortRead:false,readError:false,failAllocation:-1,CONFIG_M4_WEB_ASSET_MAX_FILES:512},options);const code=c.load();assert.equal(c.locks,0);assert(dirs.every(d=>d.closed));assert(inputs.every(f=>f.closed));if(code!==0){assert(!c.loaded);assert.equal(allocated.size,0,'failed attempt rolls back every allocation');}return code;}
assert.equal(run(),0);assert.equal(c.asset_count,3);const io=c.io;
assert.equal(c.find('/').bytes,files['/sdcard/Main WebFlie/index.html'].data.length);
assert.equal(c.find('/INDEX.HTML').bytes,c.find('/').bytes,'retain FAT case-insensitive file lookup');
assert.deepEqual(Buffer.from(c.find('/assets/image.png').data.subarray(0,6)),files['/sdcard/Main WebFlie/assets/image.png'].data);
c.sd=false;assert(c.find('/assets/style.css'));assert.equal(c.io,io,'lookups never read SD');assert.equal(c.load(),0,'immutable boot snapshot is reused');assert.equal(c.io,io);
assert(!c.find('/.private/key.json'));assert(!c.find('/missing.html'));
for(const options of [{sd:false},{lockOK:false},{missing:true},{openFail:true},{shortRead:true},{readError:true},{CONFIG_M4_WEB_ASSET_MAX_FILES:1},...Array.from({length:8},(_,i)=>({failAllocation:i+1}))])assert.notEqual(run(options),0,JSON.stringify(options));
const imageFixture=files['/sdcard/Main WebFlie/assets/image.png'].data;
files['/sdcard/Main WebFlie/assets/image.png'].data=Buffer.alloc(2*1024*1024);
assert.equal(run(),c.ESP_ERR_INVALID_SIZE,'byte budget overflow discards earlier staged files');
assert(inputs.length>=2,'budget failure occurs after earlier files were staged');
files['/sdcard/Main WebFlie/assets/image.png'].data=imageFixture;
const runtime=read('main/m4_runtime.c');assert(runtime.indexOf('m4_web_assets_load()')>runtime.indexOf('sd_media_init()'));assert(runtime.indexOf('m4_web_assets_load()')<runtime.indexOf('esp_err_t net = network_init()'));
const server=read('main/web_server.c'),stream=read('main/web_public_stream.inc');assert(server.includes('send_cached_asset(req, "/ai_studio.html")'));
function from(text,name){const m=new RegExp('\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(text);assert(m,name);let at=m.index+m[0].length,end=at,d=1;while(d){if(text[end]==='{')d++;if(text[end]==='}')d--;end++;}return text.slice(at,end-1);}
// A real cached response must preserve NUL bytes and serve ranges without touching the SD fixture.
assert.equal(run(),0);const responseIO=c.io;
Object.assign(c,{m4_web_assets_find:p=>c.find(p),m4_web_assets_slice:(bytes,range,out)=>c.slice(bytes,range,out),
 httpd_req_get_hdr_value_len:(req)=>req.range?.length||0,
 httpd_resp_set_type:(req,mime)=>req.mime=mime,httpd_resp_set_hdr:(req,k,v)=>(req.headers??={})[k]=v,
 httpd_resp_set_status:(req,status)=>req.status=status,
 httpd_resp_send:(req)=>{req.body=Buffer.alloc(0);return 0;},
 send_chunk_bytes:(req,data,n)=>{req.body=Buffer.from(data.subarray(0,n));return req.fail?-1:0;}});
let send=from(server,'send_cached_asset').replace('const m4_web_asset_t *asset','var asset')
 .replace('char range[96] = ""; size_t first = 0, length = 0;','var range="",slice={};').replace('char header[80];','var header;')
 .replaceAll('sizeof(range)','96').replace('httpd_req_get_hdr_value_str(req, "Range", range, 96)','(range=req.range, ESP_OK)')
 .replace('m4_web_assets_slice(asset->bytes, range, &first, &length)','m4_web_assets_slice(asset.bytes,range,slice)')
 .replace('snprintf(header, sizeof(header), "bytes */%u", (unsigned)asset->bytes)','header="bytes */"+asset.bytes')
 .replace('snprintf(header, sizeof(header), "bytes %u-%u/%u", (unsigned)first, (unsigned)(first + length - 1), (unsigned)asset->bytes)','header="bytes "+slice.offset+"-"+(slice.offset+slice.length-1)+"/"+asset.bytes')
 .replace('(const char *)asset->data + first','asset.data.subarray(slice.offset)').replace(/\bfirst\b/g,'slice.offset').replace(/(?<!\.)\blength\b/g,'slice.length');
// Fix the argument expression after replacing local variable names.
send=clean(send);
vm.runInContext('function send(req,path){'+send+'}',c);
let req={};assert.equal(c.send(req,'/assets/image.png'),0);assert.deepEqual(req.body,files['/sdcard/Main WebFlie/assets/image.png'].data);
req={range:'bytes=0-1'};assert.equal(c.send(req,'/assets/image.png'),0);assert.equal(req.status,'206 Partial Content');assert.deepEqual(req.body,Buffer.from([0,255]));
req={range:'bytes=999-'};assert.equal(c.send(req,'/assets/image.png'),0);assert.equal(req.status,'416 Range Not Satisfiable');
req={fail:true};assert.equal(c.send(req,'/assets/image.png'),-1);assert.equal(c.io,responseIO);
assert(from(server,'ai_studio_get_handler').indexOf('ai_require_auth')<from(server,'ai_studio_get_handler').indexOf('send_cached_asset'));
const wildcard=from(server,'spiffs_file_get_handler');assert(wildcard.includes('!strcasecmp(clean_name, "/ai_studio.html")'));
assert(wildcard.indexOf('ai_require_auth(req)')<wildcard.indexOf('send_cached_asset(req, clean_name)'));
assert(wildcard.indexOf('ai_page_headers(req)')<wildcard.indexOf('send_cached_asset(req, clean_name)'));
const relayRoute=from(server,'relay_file_get_handler');
assert(relayRoute.indexOf('cloud_relay_public_path(path)')<relayRoute.indexOf('send_cached_asset(req,path)'));
assert(relayRoute.indexOf('send_cached_asset(req,path)')<relayRoute.indexOf('try_sd_public_file(req,path)'));
assert(relayRoute.includes('m4_web_assets_ready() && strncmp(path,"/media/",7)'));
const relaySource=read('main/cloud_relay.c');
let allowed=from(relaySource,'cloud_relay_public_path').replace('for (const char *p = path; *p; ++p)','for(var i=0,p=path.charAt(0);i<path.length;p=path.charAt(++i))')
 .replaceAll('p[1]','path.charAt(i+1)').replaceAll('*p','p').replaceAll('path + 1','path.slice(1)')
 .replace('path[n + 1] == 0','!path.charAt(n+1)').replace('const char *ext','var ext')
 .replace(/const char \*(reserved|allowed)\[\] = \{([^}]+)\};/g,'var $1=[$2];')
 .replaceAll('sizeof(reserved)/sizeof(reserved[0])','reserved.length').replaceAll('sizeof(allowed)/sizeof(allowed[0])','allowed.length');
Object.assign(c,{strstr:(s,part)=>s.includes(part),strrchr:(s,ch)=>s.includes(ch)?s.slice(s.lastIndexOf(ch)):null,
 strncasecmp:(a,b,n)=>a.slice(0,n).toLowerCase()===b.slice(0,n).toLowerCase()?0:1});
vm.runInContext('function allowed(path){'+clean(allowed)+'}',c);
for(const p of ['/','/index.html','/assets/app.js'])assert(c.allowed(p),p);
for(const p of ['/admin','/admin.html','/setup.html','/ai.html','/ai_studio.html','/AI_STUDIO.HTML','/__relay/foo','/api/system','/.private/key.json'])assert(!c.allowed(p),p);
assert(stream.indexOf('m4_web_assets_find(path)')<stream.indexOf('fopen(filename'));
assert(read('main/CMakeLists.txt').includes('"m4_web_assets.c"'));
console.log('PASS: Main WebFlie recursive transactional preload, binary/UTF-8 preservation, PSRAM allocation failure/SD failure rollback, SD-free immutable lookup, valid/overflow/invalid ranges and startup/HTTP/cloud integration.');
