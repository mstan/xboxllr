/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
struct PilotWindowHost {HWND window;char path[PATH_MAX];int borderless,changing,shown;SDL_Rect normal;};
static void pilot_window_host_save(struct PilotWindowHost *host)
{
    if(!host->path[0] || host->changing || !host->window)return;
    SDL_Window *window=host->window;
    if(!host->borderless && !(SDL_GetWindowFlags(window)&(SDL_WINDOW_MINIMIZED|SDL_WINDOW_MAXIMIZED))) {
        SDL_GetWindowPosition(window,&host->normal.x,&host->normal.y);SDL_GetWindowSize(window,&host->normal.w,&host->normal.h);
    }
    char temporary[PATH_MAX+64];snprintf(temporary,sizeof(temporary),"%s.%ld.tmp",host->path,(long)getpid());
    FILE *file=fopen(temporary,"wx");if(!file)return;
    int failed=fprintf(file,"xboxllr-window-sdl 1\n%d %d %d %d %d\n",host->normal.x,host->normal.y,host->normal.w,host->normal.h,host->borderless)<0;
    if(fflush(file) || fsync(fileno(file)))failed=1;if(fclose(file))failed=1;
    if(failed || rename(temporary,host->path))unlink(temporary);
}
static void pilot_window_host_init(struct PilotWindowHost *host,HWND window)
{
    memset(host,0,sizeof(*host));host->window=window;
    SDL_GetWindowPosition(window,&host->normal.x,&host->normal.y);SDL_GetWindowSize(window,&host->normal.w,&host->normal.h);
    const char *path=getenv("XLLE_PILOT_WINDOW_STATE");if(!path || strlen(path)>=sizeof(host->path))return;strcpy(host->path,path);
    FILE *file=fopen(path,"r");if(!file)return;char header[64],tail;SDL_Rect r;int borderless;
    int valid=fgets(header,sizeof(header),file) && !strcmp(header,"xboxllr-window-sdl 1\n") &&
        fscanf(file,"%d %d %d %d %d",&r.x,&r.y,&r.w,&r.h,&borderless)==5 && fscanf(file," %c",&tail)==EOF;
    fclose(file);
    if(!valid || r.w<160 || r.h<120 || r.w>32768 || r.h>32768 || (borderless!=0 && borderless!=1))return;
    host->normal=r;SDL_SetWindowSize(window,r.w,r.h);
    /* Keep restored coordinates on an attached display. */
    int visible=0;
    for(int i=0;i<SDL_GetNumVideoDisplays();++i){SDL_Rect display;if(!SDL_GetDisplayUsableBounds(i,&display) && SDL_HasIntersection(&display,&r)){visible=1;break;}}
    SDL_SetWindowPosition(window,visible?r.x:SDL_WINDOWPOS_CENTERED,visible?r.y:SDL_WINDOWPOS_CENTERED);
    if(borderless && SDL_SetWindowFullscreen(window,SDL_WINDOW_FULLSCREEN_DESKTOP)==0)host->borderless=1;
}
static void pilot_window_host_show(struct PilotWindowHost *host){host->shown=1;SDL_ShowWindow(host->window);}
static uint32_t pilot_window_host_pump(struct PilotWindowHost *host)
{
    SDL_Event event;uint32_t quit=0;
    while(SDL_PollEvent(&event)) {
        if(event.type==SDL_QUIT || (event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_CLOSE)) {pilot_window_host_save(host);quit=1;}
        if(event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym==SDLK_RETURN && (event.key.keysym.mod&KMOD_ALT)) {
            if(!host->borderless){SDL_GetWindowPosition(host->window,&host->normal.x,&host->normal.y);SDL_GetWindowSize(host->window,&host->normal.w,&host->normal.h);}
            if(SDL_SetWindowFullscreen(host->window,host->borderless?0:SDL_WINDOW_FULLSCREEN_DESKTOP)==0)host->borderless=!host->borderless;
            pilot_window_host_save(host);
        }
        if(event.type==SDL_WINDOWEVENT && (event.window.event==SDL_WINDOWEVENT_MOVED || event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED))pilot_window_host_save(host);
    }
    pilot_posix_keyboard_snapshot();return quit;
}
