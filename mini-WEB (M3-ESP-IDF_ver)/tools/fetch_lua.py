import urllib.request,hashlib,tarfile,io,pathlib
p=pathlib.Path(__file__).resolve().parents[1] / 'components/m3_lua'
b=urllib.request.urlopen('https://www.lua.org/ftp/lua-5.4.9.tar.gz',timeout=30).read()
assert hashlib.sha256(b).hexdigest()=='2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6'
p.mkdir(parents=True,exist_ok=True)
with tarfile.open(fileobj=io.BytesIO(b),mode='r:gz') as t:
 for m in t.getmembers():
  parts=m.name.split('/')
  if len(parts)==3 and parts[1]=='src' and parts[2].endswith(('.c','.h')):
   (p/parts[2]).write_bytes(t.extractfile(m).read())
(p/'UPSTREAM.md').write_text('Lua 5.4.9, unmodified upstream C sources.\nhttps://www.lua.org/ftp/lua-5.4.9.tar.gz\nSHA256: 2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6\nLicense: MIT, included in lua.h. Only core/base/table/math are linked.\n',encoding='utf8')
core='lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib ltablib lmathlib'.split()
(p/'CMakeLists.txt').write_text('idf_component_register(SRCS '+ ' '.join('"'+s+'.c"' for s in core)+'\n INCLUDE_DIRS ".")\ntarget_compile_definitions(${COMPONENT_LIB} PRIVATE LUAI_MAXCCALLS=60)\n',encoding='utf8')
print('Vendored Lua 5.4.9; SHA256 verified')
