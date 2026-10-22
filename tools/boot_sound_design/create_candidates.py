"""Five original short violin/piano startup compositions. No firmware changes."""
from pathlib import Path
import sys, json, wave, subprocess, zipfile
import numpy as np

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE/'_deps'))
import tinysoundfont
OUT=HERE.parents[1]/'releases/boot-violin-piano-20261016'
OUT.mkdir(parents=True,exist_ok=True)
RATE=44100
FONT=HERE/'_samples/TimGM6mb.sf2'

def line(notes,starts,lengths,vels):
    return list(zip(starts,notes,lengths,vels))

CUES=[
 dict(id='01',name='Clear Dawn',ko='맑은 시작',duration=3.4,desc='밝고 투명한 피아노 아르페지오에 바이올린이 부드럽게 상승합니다.',
 piano=line([62,69,76,78,81],[.06,.20,.37,.57,1.0],[1.4,1.3,1.4,1.2,1.1],[62,52,60,57,50]),
 violin=line([78,81,85],[.42,.84,1.28],[.55,.58,.95],[48,53,48])),
 dict(id='02',name='Warm Welcome',ko='따뜻한 환영',duration=3.8,desc='중저음 피아노와 길게 이어지는 바이올린으로 차분하고 따뜻합니다.',
 piano=line([53,60,67,69,72],[.06,.10,.31,.68,1.15],[1.8,1.8,1.8,1.6,1.3],[60,46,52,55,47]),
 violin=line([69,72,76,74],[.28,.76,1.23,1.68],[.65,.65,.60,.85],[48,50,46,43])),
 dict(id='03',name='Minimal Signature',ko='미니멀 시그니처',duration=2.65,desc='짧은 세 음의 선율과 여백. 매번 켜도 부담이 적은 간결한 방향입니다.',
 piano=line([64,71,78,80],[.05,.18,.35,.52],[1.2,1.2,1.0,1.0],[63,50,54,50]),
 violin=line([80,78,76],[.35,.67,.96],[.35,.36,.67],[44,40,44])),
 dict(id='04',name='Quiet Horizon',ko='고요한 수평선',duration=4.0,desc='살짝 신비로운 마이너 나인 화음과 절제된 바이올린. 분위기 중심의 안입니다.',
 piano=line([59,66,73,74,78],[.06,.23,.43,.65,1.15],[1.8,1.9,1.7,1.7,1.5],[58,48,52,47,44]),
 violin=line([78,81,78,74],[.46,.98,1.48,1.91],[.65,.65,.55,.8],[45,48,43,40])),
 dict(id='05',name='Bright Motion',ko='경쾌한 연결',duration=3.0,desc='피아노의 또렷한 리듬과 바이올린의 상승형 마무리로 가볍고 활기찹니다.',
 piano=line([57,64,71,73,76,81],[.05,.19,.37,.56,.87,1.14],[1.3,1.2,1.1,1.1,1.0,.9],[61,50,57,53,56,49]),
 violin=line([76,78,81],[.48,.85,1.22],[.45,.45,.82],[45,48,50])),
]

def render_instrument(events,preset,duration):
    synth=tinysoundfont.Synth(samplerate=RATE,gain=-8)
    sf=synth.sfload(str(FONT))
    synth.program_select(0,sf,0,preset)
    timeline=[]
    for t,key,length,velocity in events:
        timeline += [(round(t*RATE),1,key,velocity),(round((t+length)*RATE),0,key,0)]
    timeline.sort()
    timeline.append((round(duration*RATE),-1,0,0))
    blocks=[];pos=0
    for at,on,key,velocity in timeline:
        if at>pos:
            blocks.append(np.frombuffer(synth.generate(at-pos),dtype=np.float32).reshape(-1,2).copy())
            pos=at
        if on==1:synth.noteon(0,key,velocity)
        elif on==0:synth.noteoff(0,key)
    return np.concatenate(blocks)

def ambience(audio,amount,seed):
    rng=np.random.default_rng(seed);result=audio.copy()
    # Low-level, decorrelated early reflections: intimate room, no long echo.
    for n in range(24):
        delay=int(RATE*(.025+n*.019+rng.uniform(0,.008)))
        gain=amount*np.exp(-n/7)*rng.uniform(.6,1)/5
        result[delay:] += audio[:-delay,::-1 if n%2 else 1]*gain
    return result

def wavwrite(path,a,rate):
    with wave.open(str(path),'wb') as f:
        f.setnchannels(a.shape[1]);f.setsampwidth(2);f.setframerate(rate)
        f.writeframes((np.clip(a,-1,1)*32767).astype('<i2').tobytes())

report=[]
for i,c in enumerate(CUES):
    piano=render_instrument(c['piano'],0,c['duration'])
    violin=render_instrument(c['violin'],40,c['duration'])
    # Keep a balanced mono fold-down for a small onboard speaker.
    piano[:,0]*=1.02;piano[:,1]*=.96
    violin[:,0]*=.91;violin[:,1]*=1.02
    a=ambience(piano,.70,i)+ambience(violin*.68,.95,i+11)
    a-=a.mean(axis=0)
    fade=int(.012*RATE);a[:fade]*=np.linspace(0,1,fade)[:,None]
    tail=int(.34*RATE);a[-tail:]*=np.linspace(1,0,tail)[:,None]**1.7
    a*=.66/max(np.max(np.abs(a)),1e-9)
    stem=c['id']+'_'+c['name'].lower().replace(' ','_')
    raw=OUT/(stem+'_render.wav');wavwrite(raw,a,RATE)
    master=OUT/(stem+'_master.wav')
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',str(raw),'-af','loudnorm=I=-18:TP=-2:LRA=7','-ar','48000','-ac','2','-c:a','pcm_s16le',str(master)],check=True)
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',str(master),'-codec:a','libmp3lame','-q:a','2',str(OUT/(stem+'.mp3'))],check=True)
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',str(master),'-ac','1','-ar','24000','-c:a','pcm_s16le',str(OUT/(stem+'_device.wav'))],check=True)
    raw.unlink()
    with wave.open(str(OUT/(stem+'_device.wav')),'rb') as f:
        samples=np.frombuffer(f.readframes(f.getnframes()),dtype='<i2')
        assert f.getframerate()==24000 and f.getnchannels()==1 and f.getsampwidth()==2
        assert np.max(np.abs(samples.astype(np.int32)))<32767
        assert 0<f.getnframes()/24000<10
        report.append(dict(id=c['id'],name=c['name'],korean=c['ko'],description=c['desc'],seconds=round(len(samples)/24000,2),peak_dbfs=round(20*np.log10(np.max(np.abs(samples.astype(float)))/32768),2),mp3=stem+'.mp3',device=stem+'_device.wav'))
    print(c['id'],c['name'],c['duration'],'s',flush=True)

(OUT/'candidates.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
(OUT/'README.txt').write_text('Original Mini WebServer startup cue candidates, 2026-10-16\n\n'+ '\n'.join(c['id']+' '+c['korean']+' / '+c['name']+' / '+str(c['seconds'])+' seconds\n'+c['description'] for c in report)+'\n\nMP3: audition. Master WAV: 48 kHz stereo PCM16. Device WAV: 24 kHz mono PCM16, matching the current M3 tone asset format. No firmware or installed boot sound changed. Hardware playback is not verified.\n\nInstrument rendering: TinySoundFont 0.3.7 (MIT), TimGM6mb by Tim Brechbill (GPL-2.0). The SoundFont and runtime are not distributed in this audition package.\nSources: https://github.com/deepin-community/timgm6mb-soundfont\nhttps://github.com/nwhitehead/tinysoundfont-pybind\n',encoding='utf-8')
with zipfile.ZipFile(OUT.parent/'MiniWeb-Boot-Violin-Piano-5-Candidates-20261016.zip','w',zipfile.ZIP_DEFLATED) as z:
    for f in OUT.iterdir():
        if f.is_file():z.write(f,f.name)
print('Created',len(report),'candidates; PCM format and no-clipping checks passed.')
