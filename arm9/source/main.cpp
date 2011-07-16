/* Raw - Another World Interpreter
 * Copyright (C) 2004 Gregory Montoir
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <nds.h>
#include <fat.h>

#include "engine.h"
#include "systemstub.h"
#include "util.h"

#include "backscreen.h"
#include "backcredit.h"

#define RAW_MENUINIT 0x01
#define RAW_MENUSHOW 0x02
#define RAW_PLAYGAME 0x03
#define RAW_PLAYSGAM 0x04 
#define RAW_MENUCRED 0x05
#define RAW_QUITSTDS 0x06


int bg0, bg1, bg0b, bg1b;

unsigned int dsWaitOnMenu(void) {
  // Palette index 179, 180, 181
  unsigned int uState=RAW_MENUINIT;
  unsigned int keys_pressed;
  bool bDone=false;
  int iUp=0, iDown=0, iA=0, iFlp=0, iIncPal = 1, iSensPal=0;
  static int iPal=179;
  
  while (!bDone) {
    // wait for stylus
    keys_pressed = keysCurrent();
    if (keys_pressed & KEY_UP) {
      if (!iUp) {
        iUp = 1;
        BG_PALETTE_SUB[iPal] = RGB15(31,31,31);
        iPal = iPal == 179 ? 181 : iPal-1;
      }
    }
    else iUp = 0;
    if (keys_pressed & KEY_DOWN) {
      if (!iDown) {
        iDown = 1;
        BG_PALETTE_SUB[iPal] = RGB15(31,31,31);
        iPal = iPal == 181 ? 179 : iPal+1;
      }
    }
    else iDown = 0;
    if (keys_pressed & (KEY_A | KEY_START)) {
      if (!iA) {
        iA = 1; bDone = true;
        if (iPal == 179) uState=RAW_PLAYGAME;
        if (iPal == 180) uState=RAW_PLAYSGAM;
        if (iPal == 181) uState=RAW_MENUCRED;
      }
    }
    else iA = 0;
    iFlp++;
    if (iFlp >= 10) { 
      iFlp=0; BG_PALETTE_SUB[iPal] = RGB15(iIncPal,iIncPal,iIncPal); 
      iIncPal += (iSensPal ? 2 : -2); if ( (iIncPal >= 31) || (iIncPal <= 1)) { iSensPal = 1 - iSensPal; }
    }
    swiWaitForVBlank();
  }
  
  return uState;
}

#undef main
int main(int argc, char *argv[]) {
	const char *dataPath = "/raw";
	const char *savePath = "/raw";
	//const char *dataPath = "/";
	//const char *savePath = "/";
  unsigned int etatEmu = RAW_MENUINIT;
  
	/*for (int i = 1; i < argc; ++i) {
		bool opt = false;
		if (strlen(argv[i]) >= 2) {
			opt |= parseOption(argv[i], "datapath=", &dataPath);
			opt |= parseOption(argv[i], "savepath=", &savePath);
		}
		if (!opt) {
			printf(USAGE);
			return 0;
		}
	}*/

  // Init sound
  consoleDemoInit();
  soundEnable();
  lcdMainOnTop();

  // Init Fat
	if (!fatInitDefault()) {
		iprintf("Unable to initialize libfat!\n");
		return -1;
	}

  // Init BG mode for 8 bits colors , tiles mode
  videoSetMode(MODE_0_2D | DISPLAY_BG0_ACTIVE );
  videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE);
  vramSetBankA(VRAM_A_MAIN_BG); vramSetBankC(VRAM_C_SUB_BG);
  bg0 = bgInit(0, BgType_Text8bpp, BgSize_T_256x256, 31,0);
  bg0b = bgInitSub(0, BgType_Text8bpp, BgSize_T_256x256, 31,0);

  while(etatEmu != RAW_QUITSTDS) {
    switch (etatEmu) {
      case RAW_MENUINIT:
        decompress(backscreenTiles, bgGetGfxPtr(bg0b), LZ77Vram);
        decompress(backscreenMap, (void*) bgGetMapPtr(bg0b), LZ77Vram);
        dmaCopy((void *) backscreenPal,(u16*) BG_PALETTE_SUB,256*2);
        etatEmu = RAW_MENUSHOW;
        break;
        
      case RAW_MENUSHOW:
        etatEmu =  dsWaitOnMenu();
        break;
      
      case RAW_MENUCRED:
        decompress(backcreditTiles, bgGetGfxPtr(bg0b), LZ77Vram);
        decompress(backcreditMap, (void*) bgGetMapPtr(bg0b), LZ77Vram);
        dmaCopy((void *) backcreditPal,(u16*) BG_PALETTE_SUB,256*2);
        while (keysCurrent());while (!keysCurrent());while (keysCurrent());
        etatEmu = RAW_MENUINIT;
        break;
       
      case RAW_PLAYGAME:
        {
	        g_debugMask = DBG_INFO; // DBG_LOGIC | DBG_BANK | DBG_VIDEO | DBG_SER | DBG_SND
	        SystemStub *stub = SystemStub_SDL_create();
	        Engine *e = new Engine(stub, dataPath, savePath);
	        e->run();
	        delete e;
	        delete stub;
          etatEmu = RAW_MENUINIT;
        }
        break;
        
      case RAW_PLAYSGAM:
        {
	        g_debugMask = DBG_INFO; // DBG_LOGIC | DBG_BANK | DBG_VIDEO | DBG_SER | DBG_SND
	        SystemStub *stub = SystemStub_SDL_create();
	        Engine *e = new Engine(stub, dataPath, savePath);
          stub->_pi.load = true;
	        e->run();
	        delete e;
	        delete stub;
          etatEmu = RAW_MENUINIT;
        }
        break;
    }
	}

	return 0;
}
