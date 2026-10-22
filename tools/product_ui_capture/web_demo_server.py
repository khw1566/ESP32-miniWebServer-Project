"""Capture-only local server: real web UI, deterministic fixture APIs, no board writes."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import ast, hashlib, json, re

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / 'mini-WEB (M3-ESP-IDF_ver)/main'
OUT = ROOT / 'results/web-experience-capture'
OUT.mkdir(parents=True, exist_ok=True)

def cstrings(text):
    return ''.join(ast.literal_eval(x) for x in re.findall(r'"(?:\\.|[^"\\])*"', text))

inc = (MAIN / 'web_sd_admin_ui.inc').read_text(encoding='utf-8')
macros = {name:cstrings(body) for name,body in re.findall(r'#define (\w+)\s+(.*?)(?=#define|\Z)',inc,re.S)}
src = (MAIN / 'web_page_admin.c').read_text(encoding='utf-8').split('const char HTML_ADMIN_DASHBOARD[] =',1)[1]
admin = ''.join(ast.literal_eval(t) if t.startswith('"') else macros[t] for t in re.findall(r'"(?:\\.|[^"\\])*"|\bSD_ADMIN_(?:HTML|SCRIPT)\b',src))
ai = (MAIN / 'ai_studio.html').read_text(encoding='utf-8')
for marker,file in [('AI_STUDIO_SCRIPT','ai_studio.js'),('AI_MEDIA_SCRIPT','ai_media.js'),('AI_ROUTER_SCRIPT','ai_router.js')]:
    ai=ai.replace('/* '+marker+' */',(MAIN/file).read_text(encoding='utf-8').replace('</script','<\\/script'))
banner='<div style="position:fixed;right:12px;bottom:8px;z-index:999999;background:#102c4a;color:white;padding:6px 10px;border-radius:6px;font:12px sans-serif;pointer-events:none">시연 데이터 · 실제 UI / 로컬 렌더링</div>'
admin=admin.replace('</body>',banner+'</body>')
ai=ai.replace('</body>',banner+'</body>')
files=[{'name':'index.html','size':18432,'modified':1792022400},{'name':'about.html','size':7168,'modified':1792022400},{'name':'style.css','size':5120,'modified':1792022400},{'name':'product.webp','size':196608,'modified':1792022400}]
system=dict(mode=3,product='M3',product_revision='2026-10',serial='DEMO-M3',mdns_ready=True,mdns_url='http://miniweb-demo.local',sd_ready=True,used_heap=184320,free_heap=339968,total_heap=524288,rssi=-48,ssid='Demo Wi-Fi',ip='192.0.2.31',uptime_sec=7380,storage_valid=True,disk_total=8388608,disk_used=2097152,system_reserved=8388608,flash_total=16777216,flash_used=2097152,flash_free=6291456,ota_slot='ota_0',app_ver='demo',gateway='192.0.2.1',netmask='255.255.255.0',dns='192.0.2.1',ipv6='미설정',cpu_freq_mhz=400,psram_total=33554432,psram_free=25165824)
page='<!doctype html><html lang="ko"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><style>body{margin:0;background:#f5efe4;color:#193b31;font-family:sans-serif;padding:40px}h1{font-size:54px}article{padding:20px;border-top:1px solid #a7b8a9}small{letter-spacing:3px}</style><small>MINI CAFE / 시연 콘텐츠</small><h1>잠깐 쉬어가는,<br>작은 카페.</h1><p>보드에 담아 공유하는 우리 가게 안내</p><article>아메리카노 3,500원 · 라떼 4,000원</article><article>매일 10:00–19:00 / 서울 예시로 1</article></html>'
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args): pass
    def reply(self,value,kind='application/json; charset=utf-8',status=200):
        data=value.encode() if isinstance(value,str) else json.dumps(value,ensure_ascii=False).encode()
        self.send_response(status);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(data)
    def do_GET(self):
        route=self.path.split('?')[0]
        if route in ['/admin','/']: return self.reply(admin,'text/html; charset=utf-8')
        if route=='/ai': return self.reply(ai,'text/html; charset=utf-8')
        if route=='/api/system': return self.reply(system)
        if route=='/api/auth/status': return self.reply(dict(authenticated=True,failed_attempts=0,failed_total=0,retry_after=0,record_saved=True))
        if route=='/api/settings': return self.reply(dict(status='ok',init_view=0,init_page='/index.html'))
        if route=='/api/files': return self.reply(dict(files=files,total=8388608,used=2097152))
        if route=='/api/ai/key': return self.reply(dict(ok=True,configured=True))
        if route=='/api/logs': return self.reply('[DEMO] Wi-Fi connected: Demo Wi-Fi\n[DEMO] Web server ready\n[DEMO] SD card mounted\n','text/plain; charset=utf-8')
        if route.startswith('/api/sd'): return self.reply(dict(ok=True,ready=True,mounted=True,total=34359738368,used=1073741824,files=files,path='/'))
        return self.reply(dict(error='Unsupported capture route'),status=404)
    def do_POST(self):
        data=json.loads(self.rfile.read(int(self.headers.get('Content-Length','0'))) or b'{}')
        if self.path.startswith('/api/ai/chat'):
            contents=data.get('contents',[])
            prompt=' '.join(p.get('text','') for c in contents for p in c.get('parts',[]))
            toolresults=[p for c in contents for p in c.get('parts',[]) if 'functionResponse' in p]
            diagnostic=any(x in prompt for x in ['진단','느린','상태를']) and '카페' not in prompt
            if diagnostic and not toolresults:
                parts=[{'functionCall':{'name':'get_diagnostics','args':{}}},{'functionCall':{'name':'get_logs','args':{}}}]
            elif diagnostic:
                parts=[{'text':'## 보드 상태를 함께 확인했어요\n\n- Wi-Fi 신호: **-48 dBm**, 공유기 연결 상태입니다.\n- 내부 힙: **약 35% 사용**, 여유 메모리가 남아 있습니다.\n- 최근 로그: 웹서버 시작과 SD 인식 기록이 있습니다.\n\n이 스냅샷만으로 느려짐의 원인을 확정할 수는 없습니다. 먼저 큰 이미지 용량과 페이지 요청 수를 확인하고, 반복되는 오류 로그가 있는지 살펴보세요.\n\n현재 화면의 측정값과 답변은 촬영용 시연 데이터입니다.'}]
            else:
                parts=[{'text':'## 카페 안내 페이지 초안을 준비했어요\n메뉴·가격·영업시간을 한 파일에 담았습니다. 아래 **미리보기**에서 확인하고, 실제 가게 정보로 바꾼 뒤 저장하세요.\n\n```html\n'+page+'\n```\n\n이 응답은 촬영용으로 준비한 시연 예시입니다. 보드에는 파일을 저장하지 않았습니다.'}]
            return self.reply({'candidates':[{'finishReason':'STOP','content':{'role':'model','parts':parts}}]})
        if self.path=='/api/agent':
            name=data.get('name') or data.get('tool')
            result=dict(ok=True,system=system,logs=['[DEMO] Web server ready','[DEMO] SD card mounted'],files=files)
            return self.reply(result)
        return self.reply({'error':'Capture server rejects writes'},status=405)

manifest={'captured_ui':'M3 browser UI','data':'deterministic fixtures, scripted AI responses; no model inference or physical board connection','sources':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [MAIN/'web_page_admin.c',MAIN/'web_sd_admin_ui.inc',MAIN/'ai_studio.html',MAIN/'ai_studio.js',MAIN/'ai_media.js',MAIN/'ai_router.js']}}
(OUT/'source-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print('Capture server: http://127.0.0.1:5180',flush=True)
ThreadingHTTPServer(('127.0.0.1',5180),Handler).serve_forever()
