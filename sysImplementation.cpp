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

#include <SDL.h>
#include "sys.h"
#include "util.h"


struct SDLStub : System {
	typedef void (SDLStub::*ScaleProc)(uint16_t *dst, uint16_t dstPitch, const uint16_t *src, uint16_t srcPitch, uint16_t w, uint16_t h);

	enum {
		SCREEN_W = 320,
		SCREEN_H = 200,
		SOUND_SAMPLE_RATE = 22050
	};

	struct Scaler {
		const char *name;
		ScaleProc proc;
		uint8_t factor;
	};
	
	static const Scaler _scalers[];

	uint8_t *_offscreen;
	SDL_Surface *_screen;
	SDL_Surface *_sclscreen;
	bool _fullscreen;
	uint8_t _scaler;

	uint16_t palette[NUM_COLORS];

	virtual ~SDLStub() {}
	virtual void init(const char *title);
	virtual void destroy();
	virtual void setPalette(uint8_t s, uint8_t n, const uint8_t *buf);
	virtual void copyRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *buf, uint32_t pitch);
	virtual void processEvents();
	virtual void sleep(uint32_t duration);
	virtual uint32_t getTimeStamp();
	virtual void startAudio(AudioCallback callback, void *param);
	virtual void stopAudio();
	virtual uint32_t getOutputSampleRate();
	virtual void *addTimer(uint32_t delay, TimerCallback callback, void *param);
	virtual void removeTimer(void *timerId);
	virtual void *createMutex();
	virtual void destroyMutex(void *mutex);
	virtual void lockMutex(void *mutex);
	virtual void unlockMutex(void *mutex);
	uint8_t* getOffScreenFramebuffer();

	void prepareGfxMode();
	void cleanupGfxMode();
	void switchGfxMode(bool fullscreen, uint8_t scaler);

	void point1_tx(uint16_t *dst, uint16_t dstPitch, const uint16_t *src, uint16_t srcPitch, uint16_t w, uint16_t h);
	void point2_tx(uint16_t *dst, uint16_t dstPitch, const uint16_t *src, uint16_t srcPitch, uint16_t w, uint16_t h);
	void point3_tx(uint16_t *dst, uint16_t dstPitch, const uint16_t *src, uint16_t srcPitch, uint16_t w, uint16_t h);
	void scale2x(uint16_t *dst, uint16_t dstPitch, const uint16_t *src, uint16_t srcPitch, uint16_t w, uint16_t h);
	void scale3x(uint16_t *dst, uint16_t dstPitch, const uint16_t *src, uint16_t srcPitch, uint16_t w, uint16_t h);
	
};

const SDLStub::Scaler SDLStub::_scalers[] = {
	{ "Point1_tx", &SDLStub::point1_tx, 1 },
	{ "Point2_tx", &SDLStub::point1_tx, 1 },
	{ "Scale2x", &SDLStub::point1_tx, 1 },
	{ "Point3_tx", &SDLStub::point1_tx,1 },
	{ "Scale3x", &SDLStub::point1_tx, 1 }
};



void SDLStub::init(const char *title) {
	SDL_Init(SDL_INIT_VIDEO);
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
	SDL_ShowCursor(SDL_DISABLE);
	SDL_WM_SetCaption(title, NULL);

	memset(&input, 0, sizeof(input));
	_offscreen = (uint8_t *)malloc(SCREEN_W * SCREEN_H * 2);
	if (!_offscreen) {
		error("Unable to allocate offscreen buffer");
	}
	_fullscreen = false;
	_scaler = 1;
	prepareGfxMode();
}

void SDLStub::destroy() {
	cleanupGfxMode();
	SDL_Quit();
}

void SDLStub::setPalette(uint8_t start, uint8_t numEnties, const uint8_t *buf) {

	assert(start + numEnties <= 16);

	for (int i = start; i < start + numEnties; ++i) {

		uint8_t c[3];
		for (int j = 0; j < 3; j++) {
			uint8_t col = buf[i * 3 + j];
			c[j] =  (col << 2) | (col & 3);
		}

		palette[i] = SDL_MapRGB(_screen->format, c[0], c[1], c[2]);
	}

}

void SDLStub::copyRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *buf, uint32_t pitch) {

	buf += y * pitch + x;
	uint16_t *p = (uint16_t *)_offscreen;

	//For each line
	while (height--) {

		//One byte gives us two pixels, we only need to iterate w/2 times.
		for (int i = 0; i < width / 2; ++i) {

			//Extract two palette indices from upper byte and lower byte.
			uint8_t p1 = *(buf + i) >> 4;
			uint8_t p2 = *(buf + i) & 0xF;

			//Get the pixel value from the palette and write in in offScreen.
			*(p + i * 2 + 0) = palette[p1];
			*(p + i * 2 + 1) = palette[p2];

		}

		p += SCREEN_W;
		buf += pitch;
	}

	SDL_LockSurface(_sclscreen);
	(this->*_scalers[_scaler].proc)((uint16_t *)_sclscreen->pixels, _sclscreen->pitch, (uint16_t *)_offscreen, SCREEN_W, SCREEN_W, SCREEN_H);
	SDL_UnlockSurface(_sclscreen);
	SDL_BlitSurface(_sclscreen, NULL, _screen, NULL);
	SDL_UpdateRect(_screen, 0, 0, 0, 0);
}

void SDLStub::processEvents() {
	SDL_Event ev;
	while(SDL_PollEvent(&ev)) {
		switch (ev.type) {
		case SDL_QUIT:
			input.quit = true;
			break;
		case SDL_KEYUP:
			switch(ev.key.keysym.sym) {
			case SDLK_LEFT:
				input.dirMask &= ~PlayerInput::DIR_LEFT;
				break;
			case SDLK_RIGHT:
				input.dirMask &= ~PlayerInput::DIR_RIGHT;
				break;
			case SDLK_UP:
				input.dirMask &= ~PlayerInput::DIR_UP;
				break;
			case SDLK_DOWN:
				input.dirMask &= ~PlayerInput::DIR_DOWN;
				break;
			case SDLK_SPACE:
			case SDLK_LCTRL:
			case SDLK_RETURN:
				input.button = false;
				break;
			default:
				break;
			}
			break;
		case SDL_KEYDOWN:
			if (ev.key.keysym.mod & KMOD_ALT) {
				if (ev.key.keysym.sym == SDLK_RETURN) {
					switchGfxMode(!_fullscreen, _scaler);
				} else if (ev.key.keysym.sym == SDLK_KP_PLUS) {
					uint8_t s = _scaler + 1;
					if (s < ARRAYSIZE(_scalers)) {
						switchGfxMode(_fullscreen, s);
					}
				} else if (ev.key.keysym.sym == SDLK_KP_MINUS) {
					int8_t s = _scaler - 1;
					if (_scaler > 0) {
						switchGfxMode(_fullscreen, s);
					}
				} else if (ev.key.keysym.sym == SDLK_x) {
					input.quit = true;
				}
				break;
			} else if (ev.key.keysym.mod & KMOD_CTRL) {
				if (ev.key.keysym.sym == SDLK_s) {
					input.save = true;
				} else if (ev.key.keysym.sym == SDLK_l) {
					input.load = true;
				} else if (ev.key.keysym.sym == SDLK_f) {
					input.fastMode = true;
				} else if (ev.key.keysym.sym == SDLK_KP_PLUS) {
					input.stateSlot = 1;
				} else if (ev.key.keysym.sym == SDLK_KP_MINUS) {
					input.stateSlot = -1;
				}
				break;
			}
			input.lastChar = ev.key.keysym.sym;
			switch(ev.key.keysym.sym) {
			case SDLK_LEFT:
				input.dirMask |= PlayerInput::DIR_LEFT;
				break;
			case SDLK_RIGHT:
				input.dirMask |= PlayerInput::DIR_RIGHT;
				break;
			case SDLK_UP:
				input.dirMask |= PlayerInput::DIR_UP;
				break;
			case SDLK_DOWN:
				input.dirMask |= PlayerInput::DIR_DOWN;
				break;
			case SDLK_SPACE:
			case SDLK_LCTRL:
			case SDLK_RETURN:
				input.button = true;
				break;
			case SDLK_c:
				input.code = true;
				break;
			case SDLK_p:
				input.pause = true;
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
	}
}

void SDLStub::sleep(uint32_t duration) {
	SDL_Delay(duration);
}

uint32_t SDLStub::getTimeStamp() {
	return SDL_GetTicks();	
}

void SDLStub::startAudio(AudioCallback callback, void *param) {
	/*SDL_AudioSpec desired;
	memset(&desired, 0, sizeof(desired));

	desired.freq = SOUND_SAMPLE_RATE;
	desired.format = AUDIO_U8;
	desired.channels = 1;
	desired.samples = 2048;
	desired.callback = callback;
	desired.userdata = param;
	if (SDL_OpenAudio(&desired, NULL) == 0) {
		SDL_PauseAudio(0);
	} else {
		error("SDLStub::startAudio() unable to open sound device");
	}*/
}

void SDLStub::stopAudio() {
	/*SDL_CloseAudio();*/
}

uint32_t SDLStub::getOutputSampleRate() {
	return SOUND_SAMPLE_RATE;
}

void *SDLStub::addTimer(uint32_t delay, TimerCallback callback, void *param) {
	return SDL_AddTimer(delay, (SDL_NewTimerCallback)callback, param);
}

void SDLStub::removeTimer(void *timerId) {
	SDL_RemoveTimer((SDL_TimerID)timerId);
}

void *SDLStub::createMutex() {
	/*return SDL_CreateMutex();*/
	return 0;
}

void SDLStub::destroyMutex(void *mutex) {
	/*SDL_DestroyMutex((SDL_mutex *)mutex);*/
}

void SDLStub::lockMutex(void *mutex) {
	/*SDL_mutexP((SDL_mutex *)mutex);*/
}

void SDLStub::unlockMutex(void *mutex) {
	/*SDL_mutexV((SDL_mutex *)mutex);*/
}

void SDLStub::prepareGfxMode() {
	/*SDL_ShowCursor( SDL_ENABLE ); */

	_screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 16, SDL_SWSURFACE);
	
	/*SDL_GetMouseState( &x,&y ); 
	SDL_WarpMouse( x, y ); */
	
	if (!_screen) {
		error("SDLStub::prepareGfxMode() unable to allocate _screen buffer");
	}
	_sclscreen = SDL_CreateRGBSurface(SDL_SWSURFACE, SCREEN_W, SCREEN_H, 16,
						_screen->format->Rmask,
						_screen->format->Gmask,
						_screen->format->Bmask,
						_screen->format->Amask);
	if (!_sclscreen) {
		error("SDLStub::prepareGfxMode() unable to allocate _sclscreen buffer");
	}
}

void SDLStub::cleanupGfxMode() {
	if (_offscreen) {
		free(_offscreen);
		_offscreen = 0;
	}
	if (_sclscreen) {
		SDL_FreeSurface(_sclscreen);
		_sclscreen = 0;
	}
	if (_screen) {
		SDL_FreeSurface(_screen);
		_screen = 0;
	}
}

void SDLStub::switchGfxMode(bool fullscreen, uint8_t scaler) {
	SDL_Surface *prev_sclscreen = _sclscreen;
	SDL_FreeSurface(_screen); 	
	_fullscreen = fullscreen;
	_scaler = scaler;
	prepareGfxMode();
	SDL_BlitSurface(prev_sclscreen, NULL, _sclscreen, NULL);
	SDL_FreeSurface(prev_sclscreen);
}

void SDLStub::point1_tx(uint16_t *dst, uint16_t dstPitch, const uint16_t *src, uint16_t srcPitch, uint16_t w, uint16_t h) {
	dstPitch >>= 1;
	while (h--) {
		memcpy(dst, src, w * 2);
		dst += dstPitch;
		src += dstPitch;
	}
}


uint8_t* SDLStub::getOffScreenFramebuffer()
{
	return _offscreen;
}


SDLStub sysImplementation;
System *stub = &sysImplementation;

