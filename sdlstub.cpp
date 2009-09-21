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


#include <sgl.h>
#include <sl_def.h>
#include <sega_mem.h>
#include <sega_int.h>
#include <sega_pcm.h>
#include <sega_snd.h>

#include "systemstub.h"
#include "util.h"
#include "gfs_wrap.h"
#include "saturn_print.h"
#include "mixer.h"

#define CRAM_BANK 0x5f00000 // Beginning of color ram memory addresses
#define BACK_COL_ADR (VDP2_VRAM_A1 + 0x1fffe) // Address for background colour
#define LOW_WORK_RAM 0x00200000 // Beginning of LOW WORK RAM (1Mb)
#define LOW_WORK_RAM_SIZE 0x100000

#define MAX_INPUT_DEVICES 12

#define PAD_PUSH_UP    (!(push & PER_DGT_KU))
#define PAD_PUSH_DOWN  (!(push & PER_DGT_KD))
#define PAD_PUSH_LEFT  (!(push & PER_DGT_KL))
#define PAD_PUSH_RIGHT (!(push & PER_DGT_KR))
#define PAD_PUSH_A  (!(push & PER_DGT_TA))
#define PAD_PUSH_B  (!(push & PER_DGT_TB))
#define PAD_PUSH_C  (!(push & PER_DGT_TC))
#define PAD_PUSH_Z  (!(push & PER_DGT_TZ))
#define PAD_PUSH_START (!(push & PER_DGT_ST))

#define PAD_PULL_UP    (!(pull & PER_DGT_KU))
#define PAD_PULL_DOWN  (!(pull & PER_DGT_KD))
#define PAD_PULL_LEFT  (!(pull & PER_DGT_KL))
#define PAD_PULL_RIGHT (!(pull & PER_DGT_KR))
#define PAD_PULL_A  (!(pull & PER_DGT_TA))
#define PAD_PULL_B  (!(pull & PER_DGT_TB))
#define PAD_PULL_C  (!(pull & PER_DGT_TC))
#define PAD_PULL_Z  (!(pull & PER_DGT_TZ))
#define PAD_PULL_START (!(pull & PER_DGT_ST))

#define SYS_CDINIT1(i) ((**(void(**)(int))0x60002dc)(i)) // Init functions for Saturn CD drive
#define SYS_CDINIT2() ((**(void(**)(void))0x600029c)())

#define PCM_ADDR ((void*)0x25a20000)
#define PCM_SIZE (4096L*8)

#define SND_BUFFER_SIZE 512 
#define SND_BUF_SLOTS 10 

#define MAX_TIMERS 5

extern "C" {
	extern void DMA_ScuInit(void);
}

static PcmHn createHandle(int bufno);
static void play_manage_buffers(void);
static void fill_buffer_slot(void);
void fill_play_audio(void);
void sat_restart_audio(void);
void vblIn(void); // This is run at each vblnk-in

typedef struct {
	volatile Uint8 access;
} SatMutex;

static Uint8 snd_bufs[2][SND_BUFFER_SIZE * SND_BUF_SLOTS];
static Uint8 buffer_filled[2];
static Uint8 ring_bufs[2][SND_BUFFER_SIZE * SND_BUF_SLOTS];

static PcmWork pcm_work[2];
static PcmHn pcm[2];

Uint8 curBuf = 0;
Uint8 curSlot = 0;

static SystemStub *sys = NULL;
static volatile Uint32 ticker = 0;
static volatile	Uint8  tick_wrap = 0;

/* AUDIO */
static Mixer *mix = NULL;
static volatile Uint8 audioEnabled = 0;

/* *** */

struct SDLStub : SystemStub {
	typedef void (SDLStub::*ScaleProc)(uint16 *dst, uint16 dstPitch, const uint16 *src, uint16 srcPitch, uint16 w, uint16 h);

	typedef struct {
		uint8	enabled;
		
		uint8	id;
		uint32	delay; 

		uint32	waitTick;
		uint32	waitWrap;

		TimerCallback callback;
		void *param;
	} TimerChain;

	typedef struct {
		Uint8 enabled;

		AudioCallback callback;
		void *param;
	} AudioData;

	enum {
		SCREEN_W = 320,
		SCREEN_H = 200,
		SOUND_SAMPLE_RATE = 22050 
	};

	uint16 _pal[16];

	/* Controller data */
	PerDigital *input_devices[MAX_INPUT_DEVICES];
	Uint8 connected_devices;

	/* Timers */
	TimerChain timerNode[MAX_TIMERS]; // We won't use more than this number of timers

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
	virtual	void checkTimers(void);
	virtual void fadePal(void);
	virtual void restorePal(void);

	void prepareGfxMode();
	void cleanupGfxMode();
	void switchGfxMode(bool fullscreen, uint8 scaler);

	void point1x(uint16 *dst, uint16 dstPitch, const uint16 *src, uint16 srcPitch, uint16 w, uint16 h);

	void initTimers(void);
	int  cdUnlock(void); // CD Drive unlocker, when loading game through PAR
	void setup_input (void); // Setup input controllers
	void load_audio_driver(void);
};

SystemStub *SystemStub_SDL_create() {
	MEM_Init(LOW_WORK_RAM, LOW_WORK_RAM_SIZE); // Use low work ram for the sega mem library
	sys = new SDLStub();
	return sys;
}

void SDLStub::init(const char *title) {
	//CartRAM_init(0);

#ifdef _PAR_UPLOAD_
	cdUnlock(); // Needed only when loading from PAR: Unlock the CD drive
#endif

	DMA_ScuInit(); // Init for SCU DMA

	init_GFS(); // Initialize GFS system

	slInitSystem(TV_320x224, NULL, 1); // Init SGL

	memset(&_pi, 0, sizeof(_pi)); // Clean inout

	load_audio_driver(); // Load M68K audio driver

	prepareGfxMode(); // Prepare graphic output

	setup_input(); // Setup controller inputs

	audioEnabled = 0;
	curBuf = 0;
	curSlot = 0;

	buffer_filled[0] = 0;
	buffer_filled[1] = 0;

	initTimers(); // Initialize timers for callbacks

	slIntFunction(vblIn); // Function to call at each vblank-in

	return;
}

void SDLStub::destroy() {
	cleanupGfxMode();
	SYS_Exit(0);
}

void SDLStub::setPalette(uint8 s, uint8 n, const uint8 *buf) {
	assert(s + n <= 16);
	for (int i = s; i < s + n; ++i) {
		uint8 c[3];
		for (int j = 0; j < 3; ++j) {
			uint8 col = buf[i * 3 + j];
			c[j] =  (col << 2) | (col & 3);
		}
		_pal[i] = ((c[2] >> 3) << 10) | ((c[1] >> 3) << 5) | (c[0] >> 3) | RGB_Flag; // BGR for saturn
	}	

	// Now copy the palette to CRAM.
	memcpy((uint8*)(CRAM_BANK + 512), (uint8*)_pal, 16 * sizeof(uint16));

	return;
}

void SDLStub::copyRect(uint16 x, uint16 y, uint16 w, uint16 h, const uint8 *buf, uint32 pitch) {
	buf += y * pitch + x; // Get to data...

	int idx;

	for (idx = 0; idx < h; idx++) {
		//memcpy((uint8*)(VDP2_VRAM_A0 + ((idx + y) * 256) + x), (uint8*)(buf + (idx * pitch)), w/2);
		DMA_ScuMemCopy((uint8*)(VDP2_VRAM_A0 + ((idx + y) * 256) + x), (uint8*)(buf + (idx * pitch)), w/2);
		SCU_DMAWait();
	}
	
	return;
}

void SDLStub::processEvents() {
	Uint16 push;
	Uint16 pull;

	switch(input_devices[0]->id) { // Check only the first controller...
		case PER_ID_StnAnalog: // ANALOG PAD
		case PER_ID_StnPad: // DIGITAL PAD 
			_pi.lastChar = 0;

			push = (volatile Uint16)(input_devices[0]->push);
			pull = (volatile Uint16)(input_devices[0]->pull);

			if (PAD_PULL_UP)
				_pi.dirMask &= ~PlayerInput::DIR_UP;
			else if (PAD_PUSH_UP)
				_pi.dirMask |= PlayerInput::DIR_UP;

			if (PAD_PULL_DOWN)
				_pi.dirMask &= ~PlayerInput::DIR_DOWN;
			else if (PAD_PUSH_DOWN)
				_pi.dirMask |= PlayerInput::DIR_DOWN;

			if (PAD_PULL_LEFT)
				_pi.dirMask &= ~PlayerInput::DIR_LEFT;
			else if (PAD_PUSH_LEFT)
				_pi.dirMask |= PlayerInput::DIR_LEFT;

			if (PAD_PULL_RIGHT)
				_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
			else if (PAD_PUSH_RIGHT)
				_pi.dirMask |= PlayerInput::DIR_RIGHT;

			if (PAD_PULL_A)
				_pi.button = false;
			else if (PAD_PUSH_A)
				_pi.button = true;

			if (PAD_PUSH_START)
				_pi.pause = true;

			if (PAD_PUSH_Z)
				_pi.code = true;

			break;
		default:
			break;
	}

	return;
}

void SDLStub::sleep(uint32 duration) {
	//fprintf_saturn(stdout, "SDLStub::sleep(%u)", duration);
	// TODO: Use a proper timer...
	uint32 wait_tick = ticker + duration;

	while(wait_tick >= ticker);

	return;
}

uint32 SDLStub::getTimeStamp() {
	//fprintf_saturn(stdout, "SDLStub::getTimeStamp()");
	return ticker;
}

void SDLStub::startAudio(AudioCallback callback, void *param) {
	//fprintf_saturn(stdout, "SDLStub::startAudio()");

	mix = (Mixer*)param;

	memset(snd_bufs, 0, SND_BUFFER_SIZE * 2 * SND_BUF_SLOTS);
	memset(ring_bufs, 0, SND_BUFFER_SIZE * 2 * SND_BUF_SLOTS);

	PCM_Init(); // Initialize PCM playback

	audioEnabled = 1; // Enable audio

	// Prepare handles
	pcm[0] = createHandle(0);
	pcm[1] = createHandle(1);

	// start playing
	PCM_Start(pcm[0]); 
	PCM_EntryNext(pcm[1]);

	return;
}

void SDLStub::stopAudio() {
	fprintf_saturn(stdout, "SDLStub::stopAudio()");
	
	audioEnabled = 0;

	// Stopping playback
	PCM_Stop(pcm[0]);
	PCM_Stop(pcm[1]);

	// Destroy handles
	PCM_DestroyMemHandle(pcm[0]);
	PCM_DestroyMemHandle(pcm[1]);

	// Deinitialize PCM playback
	PCM_Finish();
	return;
}

uint32 SDLStub::getOutputSampleRate() {
	return SOUND_SAMPLE_RATE;
}

void *SDLStub::addTimer(uint32 delay, TimerCallback callback, void *param) {
	//fprintf_saturn(stdout, "SDLStub::addTimer(%u)", delay);
	int idx;

	for(idx = 0; idx < MAX_TIMERS; idx++) {
		if(timerNode[idx].enabled == 0) {
			timerNode[idx].callback = callback;
			timerNode[idx].delay = delay + delay/4;
			timerNode[idx].waitTick = ticker + timerNode[idx].delay;

			if(timerNode[idx].waitTick < ticker) // wrap!
				timerNode[idx].waitWrap = (tick_wrap + 1)%2;
			else
				timerNode[idx].waitWrap = tick_wrap;

			timerNode[idx].enabled = 1;

			return &(timerNode[idx].id);
		}
	}

	return NULL;
}

void SDLStub::removeTimer(void *timerId) {
	//fprintf_saturn(stdout, "SDLStub::removeTimer()");
	int idx;

	for(idx = 0; idx < MAX_TIMERS; idx++) {
		if( *(uint8*)timerId == timerNode[idx].id) {
			timerNode[idx].enabled = 0;
		}
	}

	return;
}

void *SDLStub::createMutex() {
	//fprintf_saturn(stdout, "SDLStub::createMutex()");
	SatMutex *mtx = (SatMutex*)MEM_Malloc(sizeof(SatMutex));
	mtx->access = 0;

	return mtx;
}

void SDLStub::destroyMutex(void *mutex) {
	//fprintf_saturn(stdout, "SDLStub::destroyMutex()");
	MEM_Free(mutex);

	return;
}

void SDLStub::lockMutex(void *mutex) {
	//fprintf_saturn(stdout, "SDLStub::lockMutex()");
	while(((SatMutex*)mutex)->access > 0) fprintf_saturn(stdout, "Waiting in lockMutex()!");
	((SatMutex*)mutex)->access++;

	return;
}

void SDLStub::unlockMutex(void *mutex) {
	//fprintf_saturn(stdout, "SDLStub::unlockMutex()");
	((SatMutex*)mutex)->access--;
	
	return;
}

void SDLStub::prepareGfxMode() {
	slTVOff(); // Turn off display for initialization

	slColRAMMode(CRM16_1024); // Color mode: 1024 colors, choosed between 16 bit

	slBitMapNbg1(COL_TYPE_16, BM_512x256, (void*)VDP2_VRAM_A0); // Set this scroll plane in bitmap mode
	memset((void*)VDP2_VRAM_A0, 0x00, 512*256); // Clean the VRAM banks.

	slPriorityNbg1(1); // Game screen

	//slScrAutoDisp(NBG1ON | NBG0ON); // Enable display for NBG1 (game screen), NBG0 (debug messages/keypad)
	slScrAutoDisp(NBG1ON); // Enable display only for game screen: NBG1

	//slScrPosNbg0((FIXED)0, (FIXED)0); // Position NBG0
	//slScrPosNbg1((FIXED)0, toFIXED(-10.0)); // Position NBG1, offset it a bit to center the image on a TV set
	//slLookR(toFIXED(0.0) , toFIXED(0.0));

	slBMPaletteNbg1(1); // NBG1 (game screen) uses palette 1 in CRAM

	slScrTransparent (NBG1ON); // Do NOT elaborate transparency on NBG1 scroll

	slBack1ColSet((void *)BACK_COL_ADR, 0x0); // Black color background

	slTVOn(); // Initialization completed... tv back on

	return;
}

void SDLStub::cleanupGfxMode() {
	//fprintf_saturn(stdout, "SDLStub::cleanupGfxMode()");
	slTVOff();
	return;
}

void SDLStub::switchGfxMode(bool fullscreen, uint8 scaler) {
	//fprintf_saturn(stdout, "SDLStub::switchGfxMode()");
	return;
}


int SDLStub::cdUnlock (void) {
     Sint32 ret;
     CdcStat stat;
     volatile unsigned int delay;

     SYS_CDINIT1(3);
     SYS_CDINIT2();

     do {
          for(delay = 1000000; delay; delay--);
          ret = CDC_GetCurStat(&stat);
     } while ((ret != 0) || (CDC_STAT_STATUS(&stat) == 0xff));

     return (int) CDC_STAT_STATUS(&stat);
}

// Store the info on connected peripheals inside an array
void SDLStub::setup_input (void) {
	if ((Per_Connect1 + Per_Connect2) == 0) {
		connected_devices = 0;
		return; // Nothing connected...
	}
	
	Uint8 index, input_index = 0;

	// check up to 6 peripheals on left connector
	for(index = 0; (index < Per_Connect1) && (input_index < MAX_INPUT_DEVICES); index++)
		if(Smpc_Peripheral[index].id != PER_ID_NotConnect) {
			input_devices[input_index] = &(Smpc_Peripheral[index]);
			input_index++;
		}

	// check up to 6 peripheals on right connector 
	for(index = 0; (index < Per_Connect2) && (input_index < MAX_INPUT_DEVICES); index++)
		if(Smpc_Peripheral[index + 15].id != PER_ID_NotConnect) {
			input_devices[input_index] = &(Smpc_Peripheral[index + 15]);
			input_index++;
		}

	connected_devices = input_index;
}

void SDLStub::load_audio_driver(void) {
	SndIniDt snd_init;
	char sound_map[] = {0xff , 0xff};
	
	GFS_FILE *drv_file = NULL;
	uint32 drv_size = 0;
	uint8 *sddrvstsk = NULL;

	drv_file = sat_fopen("sddrvs.tsk");

	if(drv_file == NULL) {
		SYS_Exit(0);
	}

	sat_fseek(drv_file, 0, SEEK_END);
	drv_size = sat_ftell(drv_file);
	sat_fseek(drv_file, 0, SEEK_SET);

	sddrvstsk = (uint8*)MEM_Malloc(drv_size);

	sat_fread(sddrvstsk, drv_size, 1, drv_file);
	sat_fclose(drv_file);

	SND_INI_PRG_ADR(snd_init) 	= (uint16 *)sddrvstsk;
	SND_INI_PRG_SZ(snd_init) 	= drv_size;
	SND_INI_ARA_ADR(snd_init) 	= (uint16 *)sound_map;
	SND_INI_ARA_SZ(snd_init) 	= sizeof(sound_map);
	SND_Init(&snd_init);
	SND_ChgMap(0);

	MEM_Free(sddrvstsk);

	return;
}

void SDLStub::initTimers(void) {
	uint8 idx;

	for(idx = 0; idx < MAX_TIMERS; idx++) {
		timerNode[idx].id = idx;
		timerNode[idx].enabled = 0;
	}
}

void SDLStub::checkTimers(void) {
	int idx;

	for(idx = 0; idx < MAX_TIMERS; idx++) {
		if((ticker >= timerNode[idx].waitTick) && (timerNode[idx].waitWrap == tick_wrap) && (timerNode[idx].enabled)) { // Timer rings!
			timerNode[idx].waitTick = ticker + (timerNode[idx].delay); // reset timer

			if(timerNode[idx].waitTick < ticker) // wrap!
				timerNode[idx].waitWrap = (tick_wrap + 1)%2;
			else
				timerNode[idx].waitWrap = tick_wrap;

			timerNode[idx].callback(timerNode[idx].delay, timerNode[idx].param); // and call back!
		}
	}

	return;
}

void SDLStub::restorePal(void) {
	memcpy((uint8*)(CRAM_BANK + 512), (uint8*)_pal, 16 * sizeof(uint16));
	slSynch();
}

void SDLStub::fadePal(void) {
	int idx, entry;

	Uint16	_fadePal[16];

	memcpy(_fadePal, _pal, 16 * sizeof(uint16));
	for(entry = 0; entry < 16; entry++) {
		Uint16 colour = _fadePal[entry];

		Uint8 c0 = ((colour >> 0)  & 0x1F) >> 2;
		Uint8 c1 = ((colour >> 5)  & 0x1F) >> 2;
		Uint8 c2 = ((colour >> 10) & 0x1F) >> 2;
		
		_fadePal[entry] = (c2 << 10) | (c1 << 5) | c0 | RGB_Flag; 
	}

	memcpy((uint8*)(CRAM_BANK + 512), (uint8*)_fadePal, 16 * sizeof(uint16));
	slSynch();
}

void vblIn (void) {

	if(ticker > (0xFFFFFFFF - 19)) {
		tick_wrap ^= 1;
		ticker = 0;
	} else {
		ticker += 19;
	}
	
	// Pcm elaboration...
	PCM_VblIn();	

	// Process events
	sys->processEvents();

	// PCM Tasks
	PCM_Task(pcm[0]);
	PCM_Task(pcm[1]);

	// Fill and play the audio
	if(audioEnabled)
		fill_play_audio();

	// Process timers
	sys->checkTimers();
}

void fill_play_audio(void) {
	fill_buffer_slot(); // Fill a buffer slot
	play_manage_buffers(); // If ready, queue a buffer for playing
}

static PcmHn createHandle(int bufNo) {
	PcmCreatePara	para;
	PcmInfo 		info;
	PcmStatus		*st;
	PcmHn			pcm;

	// Initialize the handle
	PCM_PARA_WORK(&para) = (PcmWork *)(&pcm_work[bufNo]);
	PCM_PARA_RING_ADDR(&para) = (Sint8 *)(ring_bufs[bufNo]);
	PCM_PARA_RING_SIZE(&para) = SND_BUFFER_SIZE * SND_BUF_SLOTS;
	PCM_PARA_PCM_ADDR(&para) = (Sint8*)PCM_ADDR;
	PCM_PARA_PCM_SIZE(&para) = PCM_SIZE;

	st = &pcm_work[bufNo].status;
	st->need_ci = PCM_ON;
	
	// Prepare handle informations
	PCM_INFO_FILE_TYPE(&info) = PCM_FILE_TYPE_NO_HEADER; // Headerless (RAW)
	PCM_INFO_DATA_TYPE(&info) = PCM_DATA_TYPE_RLRLRL; // PCM data format
	PCM_INFO_FILE_SIZE(&info) = SND_BUFFER_SIZE * SND_BUF_SLOTS;
	PCM_INFO_CHANNEL(&info) = 1; // Mono
	PCM_INFO_SAMPLING_BIT(&info) = 8; // 8 bits
	PCM_INFO_SAMPLING_RATE(&info) = 22050; // 22050hz
	PCM_INFO_SAMPLE_FILE(&info) = SND_BUFFER_SIZE * SND_BUF_SLOTS; // Number of samples in the file

	pcm = PCM_CreateMemHandle(&para); // Prepare the handle
	PCM_NotifyWriteSize(pcm, SND_BUFFER_SIZE * SND_BUF_SLOTS);

	if (pcm == NULL) {
		return NULL;
	}

	// Assign information to the pcm handle
	PCM_SetPcmStreamNo(pcm, 0);
	PCM_SetInfo(pcm, &info); // 
	PCM_SetVolume(pcm, 7);
	PCM_ChangePcmPara(pcm);
	
	return pcm;
}

void sat_restart_audio(void) {
	//fprintf_saturn(stdout, "restart audio");
	int idx;

	// Stop pcm playing and clean up handles.
	PCM_Stop(pcm[0]);
	PCM_Stop(pcm[1]);

	PCM_DestroyMemHandle(pcm[0]);
	PCM_DestroyMemHandle(pcm[1]);

	// Clean all the buffers
	memset(ring_bufs, 0, SND_BUFFER_SIZE * 2 * SND_BUF_SLOTS);
	memset(ring_bufs, 0, SND_BUFFER_SIZE * 2 * SND_BUF_SLOTS);

	// Prepare new handles
	pcm[0] = createHandle(0);
	pcm[1] = createHandle(1);

	buffer_filled[0] = 1;
	buffer_filled[1] = 1;

	// Restart playback
	PCM_Start(pcm[0]); 
	PCM_EntryNext(pcm[1]); 

	curBuf = 0;

	return;
}

void fill_buffer_slot(void) {
	// Prepare the indexes of next slot/buffers.
	int nextBuf = (curBuf + 1) % 2;
	int nextSlot = (curSlot + 1) % SND_BUF_SLOTS;
	int workingBuffer = curBuf;

	// Avoid running if other parts of the program are in the critical section...
	if(!buffer_filled[workingBuffer] && !((SatMutex*)(mix->_mutex))->access) { 
		mix->mix((int8*)(snd_bufs[curBuf] + (curSlot * SND_BUFFER_SIZE)), SND_BUFFER_SIZE);

		if(nextSlot == 0) { // We have filled this buffer 
			buffer_filled[workingBuffer] = 1; // Mark it as full...
			memcpy(ring_bufs[curBuf], snd_bufs[curBuf], SND_BUFFER_SIZE * SND_BUF_SLOTS);
			curBuf = nextBuf; // ...and use the next buffer
		}

		curSlot = nextSlot;
	}

	return;
}

void play_manage_buffers(void) {
	static int curPlyBuf = 0;
	static Uint16 counter = 0;

	if(buffer_filled[curBuf] == 0) return;

	if ((PCM_CheckChange() == PCM_CHANGE_NO_ENTRY)) {
		if(counter < 9000) {
			PCM_DestroyMemHandle(pcm[curPlyBuf]); 			// Destroy old memory handle
			pcm[curBuf] = createHandle(curPlyBuf); // and prepare a new one

			PCM_EntryNext(pcm[curPlyBuf]); 
	
			buffer_filled[curPlyBuf] = 0;

			curPlyBuf ^= 1;
			counter++;
		} else {
			sat_restart_audio();
			counter = 0;
			curPlyBuf = 0;
		}
	}

	// If audio gets stuck... restart it
	if((PCM_GetPlayStatus(pcm[0]) == PCM_STAT_PLAY_END) || (PCM_GetPlayStatus(pcm[1]) == PCM_STAT_PLAY_END)) {
		sat_restart_audio();
		counter = 0;
		curPlyBuf = 0;
	}

	return;
}

