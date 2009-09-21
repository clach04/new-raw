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

#include "engine.h"
#include "systemstub.h"
#include "util.h"

#include <sgl.h>
#include <sl_def.h>

void ss_main(void) {
	const char *dataPath = ".";
	const char *savePath = ".";
	
	g_debugMask = DBG_INFO; // DBG_LOGIC | DBG_BANK | DBG_VIDEO | DBG_SER | DBG_SND
	SystemStub *stub = SystemStub_SDL_create();
	Engine *e = new Engine(stub, dataPath, savePath);
	e->run();
	delete e;
	delete stub;
	return;
}
