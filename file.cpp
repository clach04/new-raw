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

#include "file.h"
#include "gfs_wrap.h"

struct File_impl {
	bool _ioErr;
	File_impl() : _ioErr(false) {}
	virtual bool open(const char *path, const char *mode) = 0;
	virtual void close() = 0;
	virtual void seek(int32 off) = 0;
	virtual void read(void *ptr, uint32 size) = 0;
	virtual void write(void *ptr, uint32 size) = 0;
};

struct stdFile : File_impl {
	GFS_FILE *_fp;
	stdFile() : _fp(0) {}
	bool open(const char *path, const char *mode) {
		_ioErr = false;
		_fp = sat_fopen(path); // We can really open as read only
		return (_fp != NULL);
	}
	void close() {
		if (_fp) {
			sat_fclose(_fp);
			_fp = 0;
		}
	}
	void seek(int32 off) {
		if (_fp) {
			sat_fseek(_fp, off, SEEK_SET);
		}
	}
	void read(void *ptr, uint32 size) {
		if (_fp) {
			uint32 r = sat_fread(ptr, 1, size, _fp);
			if (r != size) {
				_ioErr = true;
			}
		}
	}


	// We cannot write this way on saturn...
	void write(void *ptr, uint32 size) {
		if (_fp) {
			uint32 r = 0;
			if (r != size) {
				_ioErr = true;
			}
		}
	}
};

File::File(bool useless) {
	_impl = new stdFile;
}

File::~File() {
	_impl->close();
	delete _impl;
}

bool File::open(const char *filename, const char *directory, const char *mode) {	
	_impl->close();
	//char buf[512];
	//sprintf(buf, "%s/%s", directory, filename);
	//char *p = buf + strlen(directory) + 1;
	//string_lower(p);
	bool opened = _impl->open(filename, mode);
	//if (!opened) { // let's try uppercase
	//	string_upper(p);
	//	opened = _impl->open(buf, mode);
	//}
	return opened;
}

void File::close() {
	_impl->close();
}

bool File::ioErr() const {
	return _impl->_ioErr;
}

void File::seek(int32 off) {
	_impl->seek(off);
}

void File::read(void *ptr, uint32 size) {
	_impl->read(ptr, size);
}

uint8 File::readByte() {
	uint8 b;
	read(&b, 1);
	return b;
}

uint16 File::readUint16BE() {
	uint16 shw;
	read(&shw, 2);

#ifdef SYS_LITTLE_ENDIAN
	shw = ((shw >> 8) & 0x00FF) + ((shw << 8) & 0xFF00);
#endif

	return shw;
}

uint32 File::readUint32BE() {
	uint32 lw;

	read(&lw, 4);
	
#ifdef SYS_LITTLE_ENDIAN
	lw =  ((lw >> 24) & 0x000000FF) |
		  ((lw >>  8) & 0x0000FF00) |
		  ((lw <<  8) & 0x00FF0000) |
		  ((lw << 24) & 0xFF000000);
#endif

	return lw;
}

void File::write(void *ptr, uint32 size) {
	_impl->write(ptr, size);
}

void File::writeByte(uint8 b) {
	write(&b, 1);
}

void File::writeUint16BE(uint16 n) {
	writeByte(n >> 8);
	writeByte(n & 0xFF);
}

void File::writeUint32BE(uint32 n) {
	writeUint16BE(n >> 16);
	writeUint16BE(n & 0xFFFF);
}
