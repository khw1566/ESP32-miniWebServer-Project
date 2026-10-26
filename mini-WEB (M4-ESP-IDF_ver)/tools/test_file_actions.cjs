// Execute actual mutation/recovery control flow with a mock filesystem, no C compiler.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync(path.join(__dirname,'../main/storage_view.c'),'utf8');
function body(name){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);
 let start=m.index+m[0].length,end=start,depth=1;while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}return source.slice(start,end-1);}
let code=body('storage_view_mutate').replace(/\/\*[\s\S]*?\*\//g,'')
 .replace('char from[272], to[272];','let from={value:""},to={value:""};')
 .replace('char parent[272];','let parent={value:""};')
 .replace(/char \*slash = strrchr\(parent, '\/'\); if \(!slash\) return ESP_ERR_INVALID_ARG; \*slash = 0;/,
 'let slash=parent.value.lastIndexOf("/"); if (slash<0) return ESP_ERR_INVALID_ARG; parent.value=parent.value.slice(0,slash);')
 .replace('struct stat st;','let st={};').replace('uint8_t header[44], expected[44];','let header=Buffer.alloc(44),expected=Buffer.alloc(44);')
 .replace('sizeof(M4_RECORDER_FOLDER)','12').replaceAll('M4_RECORDER_FOLDER "/"','M4_RECORDER_FOLDER + "/"')
 .replaceAll('source + 1','source.slice(1)').replaceAll('destination + 1','destination.slice(1)').replaceAll('source + n - 9','source.slice(n-9)')
 .replaceAll('header + 8','header.subarray(8)').replaceAll('expected + 8','expected.subarray(8)')
 .replaceAll('&st','st').replaceAll('sizeof(from)','272').replaceAll('sizeof(to)','272').replaceAll('sizeof(parent)','272')
 .replace(/\((?:long|uint32_t|size_t)\)/g,'').replace(/\b(?:const char \*|FILE \*)(\w+)/g,'let $1')
 .replace(/\b(?:size_t|bool|long) (\w+)/g,'let $1');
function header(out,bytes){out.fill(0);out.write('RIFF',0);out.writeUInt32LE(bytes+36,4);out.write('WAVEfmt ',8);out.writeUInt32LE(16,16);
 out.writeUInt16LE(1,20);out.writeUInt16LE(1,22);out.writeUInt32LE(16000,24);out.writeUInt32LE(32000,28);out.writeUInt16LE(2,32);out.writeUInt16LE(16,34);out.write('data',36);out.writeUInt32LE(bytes,40);}
function environment(fail={}){
 const files=new Map([['/sdcard',{dir:true}],['/sdcard/media',{dir:true}],['/sdcard/recordings',{dir:true}],['/spiffs',{dir:true}]]);
 const unwrap=x=>x?.value??x;
 const c={Buffer,files,ESP_OK:0,ESP_FAIL:-1,ESP_ERR_INVALID_ARG:1,ESP_ERR_NOT_FOUND:2,ESP_ERR_INVALID_STATE:3,ENOENT:2,errno:0,
 SD_MEDIA_ROOT:'/sdcard',M4_RECORDER_FOLDER:'/recordings',M4_RECORDER_MAX_SECONDS:1800,M4_RECORDER_RATE:16000,SEEK_SET:0,SEEK_END:2,
 mutation_path:s=>!!s&&s.startsWith('/')&&!s.includes('..'),m4_recorder_wav_header:header,
 strlen:s=>unwrap(s).length,strchr:(s,ch)=>unwrap(s).includes(ch),strrchr:(s,ch)=>{s=unwrap(s);return s.includes(ch)?s.slice(s.lastIndexOf(ch)):null;},
 strcmp:(a,b)=>unwrap(a)===unwrap(b)?0:1,strncmp:(a,b,n)=>unwrap(a).slice(0,n)===unwrap(b).slice(0,n)?0:1,
 snprintf:(out,_size,format,...args)=>{let i=0;out.value=format.replace(/%s/g,()=>unwrap(args[i++]));},
 stat:(p,out)=>{const f=files.get(unwrap(p));if(!f){c.errno=2;return -1;}out.st_mode=f.dir?2:1;return 0;},S_ISDIR:m=>m===2,
 unlink:p=>files.delete(unwrap(p))?0:-1,
 rmdir:p=>{p=unwrap(p);if([...files.keys()].some(k=>k.startsWith(p+'/')))return -1;return files.delete(p)?0:-1;},
 rename:(a,b)=>{a=unwrap(a);b=unwrap(b);assert(!files.get(a)?.open,'rename must follow close');if(fail.rename)return -1;
 const f=files.get(a);files.delete(a);files.set(b,f);return 0;},
 fopen:p=>{const f=files.get(unwrap(p));if(!f||fail.open)return null;f.pos=0;f.open=true;return f;},
 fread:(out,_size,n,f)=>{const read=Math.min(n,f.bytes.length-f.pos);f.bytes.copy(out,0,f.pos,f.pos+read);f.pos+=read;return read;},
 fseek:(f,p,how)=>{if(fail.seek)return -1;f.pos=how===2?f.bytes.length:p;return 0;},ftell:f=>f.bytes.length,
 fwrite:(data,_size,n,f)=>{if(fail.write)return 0;data.copy(f.bytes,f.pos,0,n);f.pos+=n;return n;},
 fflush:()=>fail.flush?-1:0,fileno:()=>1,fsync:()=>fail.sync?-1:0,fclose:f=>{f.open=false;return fail.close?-1:0;},
 memcmp:(a,b,n)=>a.subarray(0,n).equals(b.subarray(0,n))?0:1};
 vm.createContext(c);vm.runInContext('function mutate(sd,source,destination,remove){'+code+'}',c);return c;
}
let c=environment();c.files.set('/sdcard/media/a.txt',{bytes:Buffer.from('hello')});
assert.equal(c.mutate(true,'/media/a.txt','/media/b.txt',false),0);assert(!c.files.has('/sdcard/media/a.txt'));assert(c.files.has('/sdcard/media/b.txt'));
c.files.set('/sdcard/media/a.txt',{bytes:Buffer.from('keep')});assert.equal(c.mutate(true,'/media/a.txt','/media/b.txt',false),3);
assert.equal(c.files.get('/sdcard/media/a.txt').bytes.toString(),'keep');
assert.equal(c.mutate(true,'/media',null,true),-1,'nonempty folder kept');
assert.equal(c.mutate(true,'/media/a.txt',null,true),0);assert.equal(c.mutate(true,'/media/b.txt',null,true),0);assert.equal(c.mutate(true,'/media',null,true),0);
c=environment();c.files.set('/spiffs/a.html',{bytes:Buffer.from('page')});assert.equal(c.mutate(false,'/a.html','/b.html',false),0);assert.equal(c.mutate(false,'/b.html',null,true),0);
function partial(c){const bytes=Buffer.alloc(684);header(bytes,0);bytes.fill(5,44);c.files.set('/sdcard/recordings/rec.wav.part',{bytes});return bytes;}
c=environment();partial(c);assert.equal(c.mutate(true,'/recordings/rec.wav.part','/recordings/rec.wav',false),0);
let recovered=c.files.get('/sdcard/recordings/rec.wav').bytes;assert.equal(recovered.readUInt32LE(40),640);assert.equal(recovered.readUInt32LE(4),676);assert(recovered.subarray(44).every(b=>b===5));
for(const failure of ['open','seek','write','flush','sync','close','rename']){
 c=environment({[failure]:true});partial(c);assert.equal(c.mutate(true,'/recordings/rec.wav.part','/recordings/rec.wav',false),-1,failure);
 assert(c.files.has('/sdcard/recordings/rec.wav.part'),failure);assert(!c.files.has('/sdcard/recordings/rec.wav'),failure);
}
c=environment();partial(c)[20]=3;assert.equal(c.mutate(true,'/recordings/rec.wav.part','/recordings/rec.wav',false),-1,'non-PCM is not relabeled as WAV');
c=environment();partial(c);assert.equal(c.mutate(true,'/recordings/rec.wav.part','/media/other.wav',false),1,'recovery only publishes original filename');
console.log('PASS: actual C file move/delete and WAV recovery with overwrite, empty-folder, format, byte preservation and I/O failure fixtures. Host mocks only.');
