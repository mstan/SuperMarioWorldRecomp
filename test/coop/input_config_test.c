#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void Die(const char *message) {
    fputs(message,stderr);exit(1);
}

int main(int argc,char **argv) {
    assert(argc==2);
    const char *path=argv[1];FILE *f=fopen(path,"wb");assert(f);
    fputs("[GamepadMap]\nKeyboardPlayers=3\nEnableGamepad1=false\nEnableGamepad2=false\n"
          "[KeyMap]\nPause=p\n",f);fclose(f);
    ParseConfigFile(path);
    assert(g_config.has_keyboard_controls==3);
    assert(!g_config.enable_gamepad[0] && !g_config.enable_gamepad[1]);
    /* The launcher changes assignments before it reloads edited hotkeys. */
    g_config.has_keyboard_controls=2;
    ConfigReloadKeyMap(path);
    assert(g_config.has_keyboard_controls==2);
    WriteConfigFile(path);
    g_config.has_keyboard_controls=0;g_config.keyboard_players=-1;
    ParseConfigFile(path);
    assert(g_config.has_keyboard_controls==2 && g_config.keyboard_players==2);
    f=fopen(path,"rb");assert(f);char text[8192]={0};fread(text,1,sizeof(text)-1,f);fclose(f);
    assert(strstr(text,"Pause=p"));
    assert(strstr(text,"KeyboardPlayers = 2") || strstr(text,"KeyboardPlayers=2"));
    puts("co-op input: shared keyboard, hotkey reload and assignment persistence passed");
    return 0;
}
