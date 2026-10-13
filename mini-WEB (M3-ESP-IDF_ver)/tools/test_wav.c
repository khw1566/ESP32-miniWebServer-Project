/* Native Windows/Linux host test. No ESP-IDF or board code is compiled. */
#include "m3_wav.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
static unsigned char data[4096];
static unsigned length;
static unsigned tests;
static void le16(unsigned offset, unsigned value) { data[offset] = (unsigned char)value; data[offset+1] = (unsigned char)(value>>8); }
static void le32(unsigned offset, uint32_t value) { le16(offset,value); le16(offset+2,value>>16); }
static void base(unsigned channels, unsigned rate) {
    memset(data, 0, sizeof(data)); length = 52;
    memcpy(data,"RIFF",4);le32(4,length-8);memcpy(data+8,"WAVEfmt ",8);le32(16,16);
    le16(20,1);le16(22,channels);le32(24,rate);le32(28,rate*channels*2);le16(32,channels*2);le16(34,16);
    memcpy(data+36,"data",4);le32(40,8);
}
static m3_wav_info_t check(bool expected) {
    FILE *file = tmpfile(); assert(file);
    assert(fwrite(data,1,length,file)==length);
    m3_wav_info_t info;
    bool result = m3_wav_parse(file,&info); assert(result == expected);
    if(result) assert(ftell(file) == info.data_offset);
    fclose(file);++tests;return info;
}
int main(void) {
    const unsigned rates[] = {16000,22050,24000,32000,44100,48000};
    for(unsigned c=1;c<=2;++c)for(unsigned i=0;i<sizeof(rates)/sizeof(rates[0]);++i){
        base(c,rates[i]);m3_wav_info_t info=check(true);assert(info.channels==c&&info.sample_rate==rates[i]&&info.data_bytes==8);
    }
    base(1,48000);data[0]='X';check(false);
    base(1,48000);data[8]='X';check(false);
    base(1,48000);le32(4,0xffffffff);check(false);
    base(1,48000);le32(4,20);check(false);
    base(1,48000);le32(40,0xffffffff);check(false);
    base(1,48000);le16(20,3);check(false); /* IEEE float */
    base(1,48000);le16(34,24);check(false);
    base(1,48000);le16(22,3);check(false);
    base(1,48000);le32(24,96000);check(false);
    base(1,48000);le32(28,1);check(false);
    base(1,48000);le16(32,1);check(false);
    base(1,48000);le32(16,12);check(false);
    base(1,48000);le32(40,0);check(false);
    base(2,48000);le32(40,6);check(false);
    base(1,48000);length=47;check(false);
    /* Odd-sized unknown chunk with a pad byte is accepted. */
    base(1,22050);memmove(data+46,data+36,16);memcpy(data+36,"JUNK",4);le32(40,1);data[44]=42;data[45]=0;length+=10;le32(4,length-8);
    m3_wav_info_t extra=check(true);assert(extra.data_offset==54);
    /* Duplicate format chunk and missing format are rejected. */
    base(1,48000);memcpy(data+52,data+12,24);length+=24;le32(4,length-8);check(false);
    base(1,48000);memcpy(data+12,"JUNK",4);check(false);
    /* PCM data may precede fmt, provided the whole RIFF validates. */
    base(1,32000);unsigned char format[24];memcpy(format,data+12,24);memmove(data+12,data+36,16);memcpy(data+28,format,24);check(true);
    base(1,48000);memcpy(data+52,data+36,16);length+=16;le32(4,length-8);check(false);
    base(1,48000);++length;le32(4,length-8);check(false); /* partial trailing chunk */
    base(1,48000);for(unsigned i=0;i<130;++i){memcpy(data+length,"JUNK",4);le32(length+4,0);length+=8;}le32(4,length-8);check(false);
    assert(!m3_wav_parse(NULL,&extra));
    printf("PASS: %u native WAV parser cases (formats, padding, order, overflow, truncation, alignment)\n",tests);
    return 0;
}
