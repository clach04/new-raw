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

//ALEK #include <SDL.h>
#include <nds.h>
#include <nds/fifomessages.h>

#include "systemstub.h"
#include "util.h"


typedef enum {
  EMUARM7_INIT_SND = 0x123C,
  EMUARM7_STOP_SND = 0x123D,
  EMUARM7_PLAY_SND = 0x123E,
} FifoMesType;


#define  cxBG (0<<8)
#define  cyBG (0<<8)     //(20<<8);
#define  xdxBG (((320 / 256) << 8) | (320 % 256))
#define  ydyBG (((200 / 192) << 8) | (200 % 192))

extern int bg0, bg1, bg0b, bg1b;

volatile u32 emuFps;                     // Fps to display
volatile u32 emuActFrames;               // Actual number of frames 
volatile u16 g_framePending = 0;           // To manage Vcount and VBL Interrupt

void vblankIntr() {
static const u16 jitter4[] = {
  0x60, 0x40,		// 0.375, 0.250 
  0x20, 0xc0,		// 0.125, 0.750
  0xe0, 0x40,		// 0.875, 0.250
  0xa0, 0xc0,		// 0.625, 0.750
};
  
	if(g_framePending == 2) {
		g_framePending = 0;
		emuActFrames++;
	}

  //antialias tile layer
  static u16 sTime = 0;
  static u16 sIndex = 0;

  REG_BG2PA = xdxBG ; REG_BG2PB = 0; REG_BG2PC =0; REG_BG2PD = ydyBG; 
  REG_BG3PA = xdxBG;  REG_BG3PB = 0; REG_BG3PC =0; REG_BG3PD = ydyBG; 
  
  REG_BG2X = cxBG+jitter4[sIndex +0]; 
  REG_BG2Y = jitter4[sIndex +1]; 
  REG_BG3X = cxBG+jitter4[sIndex +2]; 
  REG_BG3Y = jitter4[sIndex +3]; 

	sIndex += 4;
	if(sIndex >= 8) sIndex = 0;
  
	sTime++;
	if(sTime >= 60) {
		sTime = 0;
		emuFps = emuActFrames;
		emuActFrames = 0;
	}
}

// VBL Interrupt
static void vcountIntr() {
	if (g_framePending == 1 && REG_VCOUNT < 192) {
		g_framePending = 2;
	}
}

struct SDLStub : SystemStub {
	typedef void (SDLStub::*ScaleProc)(uint16 *dst, uint16 dstPitch, const uint16 *src, uint16 srcPitch, uint16 w, uint16 h);

	enum {
		SCREEN_W = 320,
		SCREEN_H = 200,
		SOUND_SAMPLE_RATE = 22050
	};

	struct Scaler {
		const char *name;
		ScaleProc proc;
		uint8 factor;
	};
	
	static const Scaler _scalers[];

	uint8 *_offscreen;
	//ALEK SDL_Surface *_screen;
	//ALEK SDL_Surface *_sclscreen;
	bool _fullscreen;
	uint8 _scaler;
	uint16 _pal[16];

	virtual ~SDLStub() {}
	virtual void init(const char *title);
	virtual void destroy();
	virtual void setPalette(uint8 s, uint8 n, const uint8 *buf);
	virtual void copyRect(uint16 x, uint16 y, uint16 w, uint16 h, const uint8 *buf, uint32 pitch);
	virtual void processEvents();
	virtual void sleep(uint32 duration);
	virtual uint32 getTimeStamp();
	virtual void startAudio(AudioCallback callback, void *param);
	virtual void stopAudio();
	virtual uint32 getOutputSampleRate();
	virtual void *addTimer(uint32 delay, TimerCallback callback, void *param);
	virtual void removeTimer(void *timerId);
	virtual void *createMutex();
	virtual void destroyMutex(void *mutex);
	virtual void lockMutex(void *mutex);
	virtual void unlockMutex(void *mutex);

	void prepareGfxMode();
	void cleanupGfxMode();
	void switchGfxMode(bool fullscreen, uint8 scaler);

	void point1x(uint16 *dst, uint16 dstPitch, const uint16 *src, uint16 srcPitch, uint16 w, uint16 h);
	void point2x(uint16 *dst, uint16 dstPitch, const uint16 *src, uint16 srcPitch, uint16 w, uint16 h);
	void point3x(uint16 *dst, uint16 dstPitch, const uint16 *src, uint16 srcPitch, uint16 w, uint16 h);
};

const SDLStub::Scaler SDLStub::_scalers[] = {
	{ "Point1x", &SDLStub::point1x, 1 },
	{ "Point2x", &SDLStub::point2x, 2 },
	{ "Point3x", &SDLStub::point3x, 3 }
};

SystemStub *SystemStub_SDL_create() {
	return new SDLStub();
}

// a millisecond counter
volatile int milliseconds=0;

void timer0_function( void ) {
  // increment milliseconds
  milliseconds++;
} 

void SDLStub::init(const char *title) {
	memset(&_pi, 0, sizeof(_pi));
  if (_offscreen) free(_offscreen);
	_offscreen = (uint8 *)malloc(SCREEN_W * SCREEN_H * 2);
/*
	if (!_offscreen) {
		error("Unable to allocate offscreen buffer");
	}
*/
  // Prepare DS graphic mode
 	videoSetMode(MODE_5_2D | DISPLAY_BG2_ACTIVE | DISPLAY_BG3_ACTIVE);
  vramSetBankA(VRAM_A_MAIN_BG_0x06000000);
  vramSetBankB(VRAM_B_MAIN_BG_0x06020000 );
  bg0 = bgInit(3, BgType_Bmp8, BgSize_B8_512x512, 0,0);
  bg1 = bgInit(2, BgType_Bmp8, BgSize_B8_512x512, 0,0);

  REG_BLDCNT = BLEND_ALPHA | BLEND_SRC_BG2 | BLEND_DST_BG3;
  REG_BLDALPHA = (8 << 8) | 8; // 50% / 50% 

  REG_BG2PB = 0;
  REG_BG2PC = 0;
  REG_BG3PB = 0;
  REG_BG3PC = 0;

  REG_BG2X = cxBG; 
  REG_BG2Y = cyBG; 
  REG_BG3X = cxBG; 
  REG_BG3Y = cyBG; 
  REG_BG2PA = xdxBG ; 
  REG_BG2PD = ydyBG; 
  REG_BG3PA = xdxBG; 
  REG_BG3PD = ydyBG; 
  
  // Prepare timer for tick 
  milliseconds=0;
  irqSet( IRQ_TIMER0, timer0_function ); 
  TIMER0_DATA = (u16) TIMER_FREQ(1000);
  TIMER0_CR = TIMER_ENABLE | TIMER_IRQ_REQ; 
  irqEnable(IRQ_TIMER0); 
   
	_fullscreen = true;
	_scaler = 0;
	prepareGfxMode();
  
    // Init vbl and hbl func
	SetYtrigger(190); //trigger 2 lines before vsync
	irqSet(IRQ_VBLANK, vblankIntr);
	irqSet(IRQ_VCOUNT, vcountIntr);
  irqEnable(IRQ_VBLANK | IRQ_VCOUNT);
}

void SDLStub::destroy() {
  irqDisable(IRQ_TIMER0); 
  irqDisable(IRQ_TIMER1); 
  irqDisable(IRQ_TIMER2); 

	cleanupGfxMode();
	//ALEK SDL_Quit();
}

void SDLStub::setPalette(uint8 s, uint8 n, const uint8 *buf) {
	assert(s + n <= 16);
	for (int i = s; i < s + n; ++i) {
		uint8 c[3];
		for (int j = 0; j < 3; ++j) {
			uint8 col = buf[i * 3 + j];
			c[j] =  (col << 2) | (col & 3);
		}
    BG_PALETTE[i] = RGB15( (c[0]>>3), (c[1]>>3), (c[2]>>3) );
		//_pal[i] = SDL_MapRGB(_screen->format, c[0], c[1], c[2]);
	}	
}

void SDLStub::copyRect(uint16 x, uint16 y, uint16 w, uint16 h, const uint8 *buf, uint32 pitch) {
	//buf += y * pitch + x;
	uint16 *p = (uint16 *)_offscreen;
  uint16 b=0;
	while (h--) {
		for (int i = 0; i < w / 2; ++i) {
			uint8 p1 = *(buf + i) >> 4;
			uint8 p2 = *(buf + i) & 0xF;
			*(p + i + 0) = p1 | (p2<<8);
			//*(p + i * 2 + 1) = p2;
		}
    dmaCopy( p, bgGetGfxPtr(bg0)+b, 320);
		p += SCREEN_W;
    b += 256;
		buf += pitch;
	}
  g_framePending = 1;
}

void SDLStub::processEvents() {
  int keypressed=keysCurrent();
  
  _pi.dirMask &= ~PlayerInput::DIR_UP;
  _pi.dirMask &= ~PlayerInput::DIR_LEFT;
  _pi.dirMask &= ~PlayerInput::DIR_RIGHT;
  _pi.dirMask &= ~PlayerInput::DIR_DOWN;
  _pi.button = false;
  
  if (keypressed & KEY_R) {
    _pi.save = true;
  }
  if (keypressed & KEY_L) {
    _pi.load = true;
  }
  if ( (keypressed & KEY_LEFT) ) {
    _pi.dirMask |= PlayerInput::DIR_LEFT;
  }
  if ( (keypressed & KEY_RIGHT) ) {
    _pi.dirMask |= PlayerInput::DIR_RIGHT;
  }
  if ( (keypressed & (KEY_UP | KEY_B) ) ) {
    _pi.dirMask |= PlayerInput::DIR_UP;
  }
  if ( (keypressed & KEY_DOWN) ) {
    _pi.dirMask |= PlayerInput::DIR_DOWN;
  }
  if ( (keypressed & KEY_A) ) {
    _pi.button = true;
  }
  if ( (keypressed & KEY_SELECT) ) {
    _pi.code = true;
  }
  if ( (keypressed & KEY_START) ) {
    _pi.pause = true;
  }
	if (keypressed & KEY_TOUCH) {
		_pi.quit = true;
	}
}

uint32 SDLStub::getTimeStamp() {
  return milliseconds; 
}

void SDLStub::sleep(uint32 duration) {
//ALEK 	SDL_Delay(duration);
  uint32 debtime=getTimeStamp();
  while (getTimeStamp()-debtime<duration);
}

signed char sound_buffer[2048];
signed char *psound_buffer=(signed char *) &sound_buffer;
#include "mixer.h"
Mixer *theMixer;

void TimerMixCallBackEvent(void) {
  psound_buffer++;
  if (psound_buffer >= &sound_buffer[2048]) psound_buffer=(signed char *) &sound_buffer;
	theMixer->mix(psound_buffer,1);
}

void SDLStub::startAudio(AudioCallback callback, void *param) {
  FifoMessage msg;
  msg.SoundPlay.data =  sound_buffer;
  msg.SoundPlay.freq = 22050;
	msg.SoundPlay.volume = 127;
	msg.SoundPlay.pan = 64;
	msg.SoundPlay.loop = 1;
	msg.SoundPlay.format = ((1)<<4) | SoundFormat_8Bit;
  msg.SoundPlay.loopPoint = 0;
  msg.SoundPlay.dataSize = 2048 >> 2;
  msg.type = EMUARM7_PLAY_SND;
  fifoSendDatamsg(FIFO_USER_01, sizeof(msg), (u8*)&msg);
      
  theMixer =  (Mixer *) param;   
  psound_buffer=sound_buffer;
  irqSet(IRQ_TIMER1, TimerMixCallBackEvent);  
  TIMER1_DATA = TIMER_FREQ(22050);                        
  TIMER1_CR = TIMER_DIV_1 | TIMER_IRQ_REQ | TIMER_ENABLE;	     
  irqEnable(IRQ_TIMER1); 
}

void SDLStub::stopAudio() {
  irqDisable(IRQ_TIMER1); 
  irqDisable(IRQ_TIMER2); 
	//ALEK SDL_CloseAudio();
}

uint32 SDLStub::getOutputSampleRate() {
	return SOUND_SAMPLE_RATE;
}

#include "sfxplayer.h"
SfxPlayer *thePlayer;

void TimerCallBackEvent(void) {
	thePlayer->handleEvents();
}

void *SDLStub::addTimer(uint32 delay, TimerCallback callback, void *param) {
	// ALEK return SDL_AddTimer(delay, (SDL_NewTimerCallback)callback, param);
  if (delay != 0) {
    thePlayer = (SfxPlayer *) param;
    if (delay<1000)  {
      short valtimer=0xFFFF-((32768*delay)/1000);
      TIMER2_DATA = valtimer;
      TIMER2_CR = TIMER_ENABLE | TIMER_DIV_1024 | TIMER_IRQ_REQ;
      irqSet(IRQ_TIMER2, TimerCallBackEvent);
      irqEnable(IRQ_TIMER2);
    }
    else
      irqDisable(IRQ_TIMER2);
  }
  else 
    irqDisable(IRQ_TIMER2);
}

void SDLStub::removeTimer(void *timerId) {
	//ALEK SDL_RemoveTimer((SDL_TimerID)timerId);
  irqDisable(IRQ_TIMER2);
}

void *SDLStub::createMutex() {
	//ALEK return SDL_CreateMutex();
}

void SDLStub::destroyMutex(void *mutex) {
	//ALEK SDL_DestroyMutex((SDL_mutex *)mutex);
}

void SDLStub::lockMutex(void *mutex) {
	//ALEK SDL_mutexP((SDL_mutex *)mutex);
}

void SDLStub::unlockMutex(void *mutex) {
	//ALEK SDL_mutexV((SDL_mutex *)mutex);
}

void SDLStub::prepareGfxMode() {
	/*ALEK
  int w = SCREEN_W * _scalers[_scaler].factor;
	int h = SCREEN_H * _scalers[_scaler].factor;
  
	_screen = SDL_SetVideoMode(w, h, 16, VIDEO_MODE);

	if (!_screen) {
		error("SDLStub::prepareGfxMode() unable to allocate _screen buffer");
	}
	_sclscreen = SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 16,
						_screen->format->Rmask,
						_screen->format->Gmask,
						_screen->format->Bmask,
						_screen->format->Amask);
	if (!_sclscreen) {
		error("SDLStub::prepareGfxMode() unable to allocate _sclscreen buffer");
	}*/
}

void SDLStub::cleanupGfxMode() {
	if (_offscreen) {
		free(_offscreen);
		_offscreen = 0;
	}
}

void SDLStub::switchGfxMode(bool fullscreen, uint8 scaler) {
	_fullscreen = fullscreen;
	_scaler = scaler;
	prepareGfxMode();
}

void SDLStub::point1x(uint16 *dst, uint16 dstPitch, const uint16 *src, uint16 srcPitch, uint16 w, uint16 h) {
	dstPitch >>= 1;
	while (h--) {
		memcpy(dst, src, w * 2);
		dst += dstPitch;
		src += dstPitch;
	}
}

void SDLStub::point2x(uint16 *dst, uint16 dstPitch, const uint16 *src, uint16 srcPitch, uint16 w, uint16 h) {
	dstPitch >>= 1;
	while (h--) {
		uint16 *p = dst;
		for (int i = 0; i < w; ++i, p += 2) {
			uint16 c = *(src + i);
			*(p + 0) = c;
			*(p + 1) = c;
			*(p + 0 + dstPitch) = c;
			*(p + 1 + dstPitch) = c;
		}
		dst += dstPitch * 2;
		src += srcPitch;
	}
}

void SDLStub::point3x(uint16 *dst, uint16 dstPitch, const uint16 *src, uint16 srcPitch, uint16 w, uint16 h) {
	dstPitch >>= 1;
	while (h--) {
		uint16 *p = dst;
		for (int i = 0; i < w; ++i, p += 3) {
			uint16 c = *(src + i);
			*(p + 0) = c;
			*(p + 1) = c;
			*(p + 2) = c;
			*(p + 0 + dstPitch) = c;
			*(p + 1 + dstPitch) = c;
			*(p + 2 + dstPitch) = c;
			*(p + 0 + dstPitch * 2) = c;
			*(p + 1 + dstPitch * 2) = c;
			*(p + 2 + dstPitch * 2) = c;
		}
		dst += dstPitch * 3;
		src += srcPitch;
	}
}
