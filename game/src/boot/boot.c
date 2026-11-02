/* boot.c - ROM entry point (module: boot) */
#include <nds.h>
#include <calico.h>
#include <stdio.h>

int main(int argc, char **argv) {
    consoleDemoInit();
    iprintf("WAR TABLE  boot ok\n");
    static touchPosition lastTouchXY __attribute__((unused));
    static volatile int appRunning = 1;
    while (appRunning) {
        swiWaitForVBlank();
        scanKeys();
        int keys = keysDown();
        if (keys & KEY_START) break;
        touchRead(&lastTouchXY);
    }
    return 0;
}
