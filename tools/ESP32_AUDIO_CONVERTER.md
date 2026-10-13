# ESP32 음악 파일 변환기

`scripts/ESP32_Audio_Converter.bat`을 더블클릭하고 음악 폴더를 선택한 뒤 **변환 시작**을 누릅니다. 기본 저장 위치는 입력 폴더 안의 `esp32_audio`입니다. 원본과 기존 결과를 덮어쓰지 않으며 하위 폴더 구조를 유지합니다. 출력 폴더는 다음 변환 검색에서 제외됩니다. 파일마다 순차 처리하므로 음악 전체를 메모리에 올리지 않습니다.

Python 3.10 이상과 FFmpeg가 필요합니다. 별도 Python 패키지는 필요하지 않습니다. FFmpeg는 PATH, `tools/ffmpeg.exe`, 현재 PC의 `C:\KHW\ffmpeg\bin\ffmpeg.exe` 순서로 찾습니다.

| 설정 | 저장 형식 | 예상 용량 / 분 | 용도 |
|---|---|---|---|
| MP3 최소 용량 (기본) | 32kbps, 16kHz, 모노 | 약 240 KB | 용량 우선, 낮은 음질 |
| MP3 음악용 | 64kbps, 24kHz, 모노 | 약 480 KB | 기본보다 나은 음질 |
| WAV 간단 재생 | PCM signed 16-bit little-endian, 16kHz, 모노 | 약 1.92 MB | 압축 디코더 없이 읽기 쉬운 형식 |

용량은 십진 KB/MB 기준의 근삿값입니다. 원본이 이미 작은 경우 결과가 더 클 수 있습니다. 모두 모노로 변환하며 앨범 이미지와 메타데이터를 제거합니다. 같은 이름의 서로 다른 입력 형식은 `_2`, `_3` 접미사로 구분합니다. 기존 결과는 건너뛰므로 다른 설정으로 다시 변환할 때는 다른 저장 폴더를 선택하세요. 손상되거나 오디오가 없는 파일은 실패 기록을 남기고 다음 파일을 처리합니다. 중단 시 현재 임시 파일을 지우며 이미 완료한 결과는 유지합니다.

입력: MP3, WAV, FLAC, M4A, AAC, OGG, OPUS, WMA, AIF/AIFF, MP4, WEBM. FFmpeg 빌드에 해당 디코더가 있어야 합니다. 영상 파일에서는 첫 번째 오디오 트랙만 추출합니다.

현재 프로젝트의 M3 음악 플레이어는 MP3/AAC 및 PCM16 WAV를 지원하며, PCM16 WAV는 16kHz를 허용합니다. 결과를 M3 SD 카드의 `/media/` 아래로 복사해 사용하세요. 다른 ESP32 펌웨어에서는 선택한 형식의 디코더와 샘플레이트 지원을 확인해야 합니다. 이 도구는 PC에서 변환하며 펌웨어를 수정하거나 보드에 업로드하지 않습니다.

명령줄 예시 (PowerShell):

```powershell
python tools/esp32_audio_converter.py 'D:\Music'
python tools/esp32_audio_converter.py 'D:\Music' -o 'D:\ESP32_WAV' --profile wav-easy
python tools/esp32_audio_converter.py 'D:\Music' --profile mp3-balanced --no-recursive
python tools/esp32_audio_converter.py 'D:\Music' --ffmpeg 'C:\ffmpeg\bin\ffmpeg.exe'
```
