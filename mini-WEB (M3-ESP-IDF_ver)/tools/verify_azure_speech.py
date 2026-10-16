"""Explicit live Azure smoke test with generated Korean speech only. Never prints credentials."""
import argparse
import io
import json
import re
import time
import urllib.request
import wave
from pathlib import Path


def verify(settings, output):
    key, region = settings['azure_speech'], settings['azure_region']
    if not isinstance(key, str) or not re.fullmatch(r'[\x21-\x7e]{20,255}', key) or not re.fullmatch(r'[a-z0-9]{1,31}', region):
        raise ValueError('Invalid settings')
    headers = {'Ocp-Apim-Subscription-Key': key, 'User-Agent': 'M3-Speech-Host-Verification'}
    phrase = '안녕하세요. 음성 연결 테스트입니다.'
    ssml = ("<speak version='1.0' xml:lang='ko-KR'><voice name='ko-KR-SunHiNeural'>" + phrase + '</voice></speak>').encode()
    request = urllib.request.Request(f'https://{region}.tts.speech.microsoft.com/cognitiveservices/v1', data=ssml,
        headers={**headers, 'Content-Type': 'application/ssml+xml; charset=utf-8', 'X-Microsoft-OutputFormat': 'raw-16khz-16bit-mono-pcm'})
    started = time.monotonic()
    with urllib.request.urlopen(request, timeout=30) as response:
        first = response.read(1024)
        first_seconds = time.monotonic() - started
        pcm = first + response.read(2 * 1024 * 1024)
    if not pcm or len(pcm) % 2 or len(pcm) > 2 * 1024 * 1024:
        raise ValueError('Invalid PCM')
    audio = io.BytesIO()
    with wave.open(audio, 'wb') as handle:
        handle.setnchannels(1); handle.setsampwidth(2); handle.setframerate(16000); handle.writeframes(pcm)
    request = urllib.request.Request(f'https://{region}.stt.speech.microsoft.com/speech/recognition/conversation/cognitiveservices/v1?language=ko-KR&format=simple',
        data=audio.getvalue(), headers={**headers, 'Content-Type': 'audio/wav; codecs=audio/pcm; samplerate=16000', 'Accept': 'application/json'})
    started = time.monotonic()
    with urllib.request.urlopen(request, timeout=30) as response:
        result = json.loads(response.read(4097))
    stt_seconds = time.monotonic() - started
    if result.get('RecognitionStatus') != 'Success' or not result.get('DisplayText'):
        raise ValueError('Recognition did not succeed')
    recognized = result['DisplayText']
    if '안녕' not in recognized or '테스트' not in recognized:
        raise ValueError('Recognition did not match generated phrase')
    output.mkdir(parents=True, exist_ok=True)
    (output / 'azure-korean-test.wav').write_bytes(audio.getvalue())
    report = {'evidence': 'PC_cloud_API_only_not_firmware_or_board', 'region': region, 'sample_rate': 16000,
        'pcm_bytes': len(pcm), 'tts_first_1024_bytes_seconds': round(first_seconds, 3),
        'stt_request_seconds': round(stt_seconds, 3), 'recognition_status': 'Success', 'synthetic_phrase_matched': True}
    (output / 'azure-speech-verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--live', action='store_true', help='Call Azure with a short generated test phrase')
    args = parser.parse_args()
    if not args.live:
        parser.error('--live is required; no credentials read or API called without it')
    root = Path(__file__).resolve().parents[1]
    try:
        settings = json.loads((root / 'sd_private/.private/api_keys.json').read_text(encoding='utf-8'))
        verify(settings, root / 'results/azure-speech')
    except Exception:
        parser.exit(1, 'Azure smoke failed. Check resource provisioning, key/region, quota and network. No credential or response body printed.\n')


if __name__ == '__main__':
    main()
