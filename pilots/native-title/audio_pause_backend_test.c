#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "audio_backend.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"audio pause backend line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void)
{
    CHECK(!pilot_audio_open());void *voice=NULL;
    CHECK(!pilot_audio_buffer_create(192000,2,48000,16,&voice));
    CHECK(!pilot_audio_buffer_volume(voice,-10000)); // unattended fixture stays muted
    CHECK(pilot_audio_buffer_pause(voice,1)==1 && !pilot_audio_buffer_pause(voice,0));
    CHECK(!pilot_audio_buffer_play(voice,1));Sleep(60);
    CHECK(!pilot_audio_buffer_pause(voice,1) && !pilot_audio_buffer_pause(voice,1));
    uint32_t status,position,next;Sleep(50);
    CHECK(!pilot_audio_buffer_status(voice,&status) && status==6);
    CHECK(!pilot_audio_buffer_position(voice,&position,&next));
    CHECK(pilot_audio_buffer_pause(voice,2)<0);Sleep(60);
    uint32_t later;CHECK(!pilot_audio_buffer_position(voice,&later,NULL) && later==position);
    CHECK(!pilot_audio_buffer_seek(voice,0));
    CHECK(!pilot_audio_buffer_status(voice,&status) && status==6);
    CHECK(!pilot_audio_buffer_pause(voice,0));Sleep(60);
    CHECK(!pilot_audio_buffer_status(voice,&status) && status==5);
    CHECK(!pilot_audio_buffer_position(voice,&later,NULL) && later);
    CHECK(!pilot_audio_buffer_pause(voice,1) && !pilot_audio_buffer_stop(voice));
    CHECK(!pilot_audio_buffer_status(voice,&status) && !status);
    CHECK(pilot_audio_buffer_pause(voice,1)==1);
    CHECK(!pilot_audio_buffer_play(voice,0) && !pilot_audio_buffer_pause(voice,1));
    CHECK(!pilot_audio_buffer_status(voice,&status) && status==2);
    pilot_audio_buffer_destroy(voice);pilot_audio_close();
    puts("PASS DirectSound ordinary pause, inactive return, paused/loop status, stable source cursor, seek/resume and stop lifetime");
    return 0;
}
