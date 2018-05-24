/*
    NitroHax -- Cheat tool for the Nintendo DS
    Copyright (C) 2008  Michael "Chishm" Chisholm

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <nds.h> 
#include <nds/fifomessages.h>
#include "cardEngine.h"

static u32 ROM_LOCATION = 0x0C800000;
extern u32 ROM_TID;
extern u32 ROM_HEADERCRC;
extern u32 ARM9_LEN;
extern u32 romSize;

#define _32KB_READ_SIZE 0x8000
#define _64KB_READ_SIZE 0x10000
#define _128KB_READ_SIZE 0x20000
#define _192KB_READ_SIZE 0x30000
#define _256KB_READ_SIZE 0x40000
#define _512KB_READ_SIZE 0x80000
#define _768KB_READ_SIZE 0xC0000
#define _1MB_READ_SIZE 0x100000

#define REG_MBK_WRAM_CACHE_START	0x4004044
#define WRAM_CACHE_ADRESS_START 0x03708000
#define WRAM_CACHE_ADRESS_END 0x03778000
#define WRAM_CACHE_ADRESS_SIZE 0x78000
#define WRAM_CACHE_SLOTS 15

#define only_CACHE_ADRESS_START 0x0C800000
#define only_CACHE_ADRESS_SIZE 0x800000
#define only_128KB_CACHE_SLOTS 0x40
#define only_192KB_CACHE_SLOTS 0x2A
#define only_256KB_CACHE_SLOTS 0x20
#define only_512KB_CACHE_SLOTS 0x10
#define only_768KB_CACHE_SLOTS 0xA
#define only_1MB_CACHE_SLOTS 0x8

extern vu32* volatile cardStruct;
//extern vu32* volatile cacheStruct;
extern u32 sdk_version;
extern u32 needFlushDCCache;
vu32* volatile sharedAddr = (vu32*)0x027FFB08;
extern volatile int (*readCachedRef)(u32*); // this pointer is not at the end of the table but at the handler pointer corresponding to the current irq

static u32 WRAM_cacheDescriptor [WRAM_CACHE_SLOTS] = {0xffffffff};
static u32 WRAM_cacheCounter [WRAM_CACHE_SLOTS];
static u32 only_cacheDescriptor [only_128KB_CACHE_SLOTS] = {0xffffffff};
static u32 only_cacheCounter [only_128KB_CACHE_SLOTS];
static u32 WRAM_accessCounter = 0;
static u32 only_accessCounter = 0;

static u32 CACHE_READ_SIZE = _128KB_READ_SIZE;
static u32 only_cacheSlots = only_128KB_CACHE_SLOTS;

static bool flagsSet = false;
extern u32 ROMinRAM;
static int use12MB = 0;
extern u32 enableExceptionHandler;
extern u32 dsiWramUsed;

// 1 = start of data address, 2 = end of data address, 3 = data size, 4 = DATAEXCLUDE,
// 5 = GAME_CACHE_ADRESS_START, 6 = GAME_CACHE_SLOTS, 7 = GAME_READ_SIZE
extern u32 setDataBWlist[7];
int dataAmount = 0;

void user_exception(void);

//---------------------------------------------------------------------------------
void setExceptionHandler2() {
//---------------------------------------------------------------------------------
	exceptionStack = (u32)0x23EFFFC ;
	EXCEPTION_VECTOR = enterException ;
	*exceptionC = user_exception;
}

int WRAM_allocateCacheSlot() {
	int slot = 0;
	int lowerCounter = WRAM_accessCounter;
	for(int i=0; i<WRAM_CACHE_SLOTS; i++) {
		if(WRAM_cacheCounter[i]<=lowerCounter) {
			lowerCounter = WRAM_cacheCounter[i];
			slot = i;
			if(!lowerCounter) break;
		}
	}
	return slot;
}

int allocateCacheSlot() {
	int slot = 0;
	int lowerCounter = only_accessCounter;
	for(int i=0; i<only_cacheSlots; i++) {
		if(only_cacheCounter[i]<=lowerCounter) {
			lowerCounter = only_cacheCounter[i];
			slot = i;
			if(!lowerCounter) break;
		}
	}
	return slot;
}

int GAME_allocateCacheSlot() {
	int slot = 0;
	int lowerCounter = only_accessCounter;
	for(int i=0; i<setDataBWlist[5]; i++) {
		if(only_cacheCounter[i]<=lowerCounter) {
			lowerCounter = only_cacheCounter[i];
			slot = i;
			if(!lowerCounter) break;
		}
	}
	return slot;
}

int WRAM_getSlotForSector(u32 sector) {
	for(int i=0; i<WRAM_CACHE_SLOTS; i++) {
		if(WRAM_cacheDescriptor[i]==sector) {
			return i;
		}
	}
	return -1;
}

int getSlotForSector(u32 sector) {
	for(int i=0; i<only_cacheSlots; i++) {
		if(only_cacheDescriptor[i]==sector) {
			return i;
		}
	}
	return -1;
}

int GAME_getSlotForSector(u32 sector) {
	for(int i=0; i<setDataBWlist[5]; i++) {
		if(only_cacheDescriptor[i]==sector) {
			return i;
		}
	}
	return -1;
}


vu8* WRAM_getCacheAddress(int slot) {
	return (vu32*)(WRAM_CACHE_ADRESS_END-slot*_32KB_READ_SIZE);
}

vu8* getCacheAddress(int slot) {
	return (vu32*)(only_CACHE_ADRESS_START+slot*CACHE_READ_SIZE);
}

vu8* GAME_getCacheAddress(int slot) {
	return (vu32*)(setDataBWlist[4]+slot*setDataBWlist[6]);
}

void transfertToArm7(int slot) {
	*((vu8*)(REG_MBK_WRAM_CACHE_START+slot)) |= 0x1;
}

void transfertToArm9(int slot) {
	*((vu8*)(REG_MBK_WRAM_CACHE_START+slot)) &= 0xFE;
}

void WRAM_updateDescriptor(int slot, u32 sector) {
	WRAM_cacheDescriptor[slot] = sector;
	WRAM_cacheCounter[slot] = WRAM_accessCounter;
}

void updateDescriptor(int slot, u32 sector) {
	only_cacheDescriptor[slot] = sector;
	only_cacheCounter[slot] = only_accessCounter;
}

void accessCounterIncrease() {
	if(dsiWramUsed) {
		WRAM_accessCounter++;
	} else {
		only_accessCounter++;
	}
}

void waitForArm7() {
	while(sharedAddr[3] != (vu32)0);
}

int cardRead (u32* cacheStruct) {
	REG_IME = IME_DISABLE;
	//nocashMessage("\narm9 cardRead\n");

	u8* cacheBuffer = (u8*)(cacheStruct + 8);
	u32* cachePage = cacheStruct + 2;
	u32 commandRead;
	u32 src = cardStruct[0];
	if(src==0) {
		return 0;	// If ROM read location is 0, do not proceed.
	}
	u8* dst = (u8*) (cardStruct[1]);
	u32 len = cardStruct[2];

	u32 page = (src/512)*512;

	if(!flagsSet) {
		if (enableExceptionHandler) {
			setExceptionHandler2();
		}

		// If ROM size is 0x00C00000 or below, then the ROM is in RAM.
		if((romSize > 0) && (romSize <= 0x00C00000) && ((ROM_TID & 0x00FFFFFF) != 0x524941) && ((ROM_TID & 0x00FFFFFF) != 0x534941)
		&& (romSize != (0x012C7066-0x4000-ARM9_LEN))
		&& !dsiWramUsed) {
			if(romSize > 0x00800000 && romSize <= 0x00C00000) {
				use12MB = 1;
				ROM_LOCATION = 0x0D000000-romSize;
			}

			ROM_LOCATION -= 0x4000;
			ROM_LOCATION -= ARM9_LEN;
		} else {
			/*if((ROM_TID == 0x45525741) && (ROM_HEADERCRC == 0xB586CF56)) {	// Advance Wars: Dual Strike (U)
				ROM_LOCATION = 0x0C400000;
				use12MB = 1;
			} */

			if(ROMinRAM==2 && setDataBWlist[3]==false) {
				ROM_LOCATION -= 0x4000;
				ROM_LOCATION -= ARM9_LEN;
			}
		}

		if(dsiWramUsed) {
			CACHE_READ_SIZE = _32KB_READ_SIZE;
		} else if (ROMinRAM==0) {
			if((ROM_TID & 0x00FFFFFF) == 0x593341)	// Sonic Rush Adventure
			{
				CACHE_READ_SIZE = _1MB_READ_SIZE;
				only_cacheSlots = only_1MB_CACHE_SLOTS;
			} else if((ROM_TID & 0x00FFFFFF) == 0x4D5241	// Mario & Luigi: Partners in Time
					|| (ROM_TID & 0x00FFFFFF) == 0x575941)	// Yoshi's Island DS
			{
				CACHE_READ_SIZE = _256KB_READ_SIZE;
				only_cacheSlots = only_256KB_CACHE_SLOTS;
			}
		}
		flagsSet = true;
	}

	u32 sector = (src/CACHE_READ_SIZE)*CACHE_READ_SIZE;

	#ifdef DEBUG
	// send a log command for debug purpose
	// -------------------------------------
	commandRead = 0x026ff800;

	sharedAddr[0] = dst;
	sharedAddr[1] = len;
	sharedAddr[2] = src;
	sharedAddr[3] = commandRead;

	IPC_SendSync(0xEE24);

	waitForArm7();
	// -------------------------------------*/
	#endif
	

	if(ROMinRAM==0) {
		accessCounterIncrease();

		if(page == src && len > CACHE_READ_SIZE && dst < 0x02700000 && dst > 0x02000000 && ((u32)dst)%4==0) {
			// read directly at arm7 level
			commandRead = 0x025FFB08;

			cacheFlush();

			sharedAddr[0] = dst;
			sharedAddr[1] = len;
			sharedAddr[2] = src;
			sharedAddr[3] = commandRead;

			IPC_SendSync(0xEE24);

			waitForArm7();

		} else {
			// read via the main RAM/DSi WRAM cache
			while(len > 0) {
				if(dsiWramUsed) {
					int slot = WRAM_getSlotForSector(sector);
					vu8* buffer = WRAM_getCacheAddress(slot);
					// read max CACHE_READ_SIZE via the main RAM cache
					if(slot==-1) {
						// send a command to the arm7 to fill the RAM cache
						commandRead = 0x025FFB08;

						slot = WRAM_allocateCacheSlot();
							
						buffer = WRAM_getCacheAddress(slot);

						if(needFlushDCCache) DC_FlushRange(buffer, _32KB_READ_SIZE);

						// transfer the WRAM-B cache to the arm7
						transfertToArm7(slot);				
						
						// write the command
						sharedAddr[0] = buffer;
						sharedAddr[1] = _32KB_READ_SIZE;
						sharedAddr[2] = sector;
						sharedAddr[3] = commandRead;

						IPC_SendSync(0xEE24);

						waitForArm7();
						
						// transfer back the WRAM-B cache to the arm9
						transfertToArm9(slot);
					}

					WRAM_updateDescriptor(slot, sector);

					u32 len2=len;
					if((src - sector) + len2 > _32KB_READ_SIZE){
						len2 = sector - src + _32KB_READ_SIZE;
					}

					if(len2 > 512) {
						len2 -= src%4;
						len2 -= len2 % 32;
					}

					if(len2 >= 512 && len2 % 32 == 0 && ((u32)dst)%4 == 0 && src%4 == 0) {
						#ifdef DEBUG
						// send a log command for debug purpose
						// -------------------------------------
						commandRead = 0x026ff800;

						sharedAddr[0] = dst;
						sharedAddr[1] = len2;
						sharedAddr[2] = buffer+src-sector;
						sharedAddr[3] = commandRead;

						IPC_SendSync(0xEE24);

						waitForArm7();
						// -------------------------------------*/
						#endif

						// copy directly
						fastCopy32(buffer+(src-sector),dst,len2);

						// update cardi common
						cardStruct[0] = src + len2;
						cardStruct[1] = dst + len2;
						cardStruct[2] = len - len2;
					} else {
						#ifdef DEBUG
						// send a log command for debug purpose
						// -------------------------------------
						commandRead = 0x026ff800;

						sharedAddr[0] = page;
						sharedAddr[1] = len2;
						sharedAddr[2] = buffer+page-sector;
						sharedAddr[3] = commandRead;

						IPC_SendSync(0xEE24);

						waitForArm7();
						// -------------------------------------*/
						#endif

						// read via the 512b ram cache
						fastCopy32(buffer+(page-sector), cacheBuffer, 512);
						*cachePage = page;
						(*readCachedRef)(cacheStruct);
					}
					len = cardStruct[2];
					if(len>0) {
						src = cardStruct[0];
						dst = cardStruct[1];
						page = (src/512)*512;
						sector = (src/_32KB_READ_SIZE)*_32KB_READ_SIZE;
						WRAM_accessCounter++;
					}
				} else {
					int slot = getSlotForSector(sector);
					vu8* buffer = getCacheAddress(slot);
					// read max CACHE_READ_SIZE via the main RAM cache
					if(slot==-1) {
						// send a command to the arm7 to fill the RAM cache
						commandRead = 0x025FFB08;

						slot = allocateCacheSlot();
							
						buffer = getCacheAddress(slot);

						if(needFlushDCCache) DC_FlushRange(buffer, CACHE_READ_SIZE);

						// write the command
						sharedAddr[0] = buffer;
						sharedAddr[1] = CACHE_READ_SIZE;
						sharedAddr[2] = sector;
						sharedAddr[3] = commandRead;

						IPC_SendSync(0xEE24);

						waitForArm7();
					}

					updateDescriptor(slot, sector);

					u32 len2=len;
					if((src - sector) + len2 > CACHE_READ_SIZE){
						len2 = sector - src + CACHE_READ_SIZE;
					}

					if(len2 > 512) {
						len2 -= src%4;
						len2 -= len2 % 32;
					}

					if(len2 >= 512 && len2 % 32 == 0 && ((u32)dst)%4 == 0 && src%4 == 0) {
						#ifdef DEBUG
						// send a log command for debug purpose
						// -------------------------------------
						commandRead = 0x026ff800;

						sharedAddr[0] = dst;
						sharedAddr[1] = len2;
						sharedAddr[2] = buffer+src-sector;
						sharedAddr[3] = commandRead;

						IPC_SendSync(0xEE24);

						waitForArm7();
						// -------------------------------------*/
						#endif

						// copy directly
						fastCopy32(buffer+(src-sector),dst,len2);

						// update cardi common
						cardStruct[0] = src + len2;
						cardStruct[1] = dst + len2;
						cardStruct[2] = len - len2;
					} else {
						#ifdef DEBUG
						// send a log command for debug purpose
						// -------------------------------------
						commandRead = 0x026ff800;

						sharedAddr[0] = page;
						sharedAddr[1] = len2;
						sharedAddr[2] = buffer+page-sector;
						sharedAddr[3] = commandRead;

						IPC_SendSync(0xEE24);

						waitForArm7();
						// -------------------------------------*/
						#endif

						// read via the 512b ram cache
						fastCopy32(buffer+(page-sector), cacheBuffer, 512);
						*cachePage = page;
						(*readCachedRef)(cacheStruct);
					}
					len = cardStruct[2];
					if(len>0) {
						src = cardStruct[0];
						dst = cardStruct[1];
						page = (src/512)*512;
						sector = (src/CACHE_READ_SIZE)*CACHE_READ_SIZE;
						only_accessCounter++;
					}
				}
			}
		}
	} else if (ROMinRAM==1) {
		// Prevent overwriting ROM in RAM
		if(dst > 0x02400000 && dst < 0x02800000) {
			if(use12MB==2) {
				return 0;	// Reject data from being loaded into debug 4MB area
			} else if(use12MB==1) {
				dst -= 0x00400000;
			}
		}

		while(len > 0) {
			u32 len2=len;
			if(len2 > 512) {
				len2 -= src%4;
				len2 -= len2 % 32;
			}

			if(len2 >= 512 && len2 % 32 == 0 && ((u32)dst)%4 == 0 && src%4 == 0) {
				#ifdef DEBUG
				// send a log command for debug purpose
				// -------------------------------------
				commandRead = 0x026ff800;

				sharedAddr[0] = dst;
				sharedAddr[1] = len2;
				sharedAddr[2] = ROM_LOCATION+src;
				sharedAddr[3] = commandRead;

				IPC_SendSync(0xEE24);

				waitForArm7();
				// -------------------------------------*/
				#endif

				// read ROM loaded into RAM
				fastCopy32(ROM_LOCATION+src,dst,len2);

				// update cardi common
				cardStruct[0] = src + len2;
				cardStruct[1] = dst + len2;
				cardStruct[2] = len - len2;
			} else {
				#ifdef DEBUG
				// send a log command for debug purpose
				// -------------------------------------
				commandRead = 0x026ff800;

				sharedAddr[0] = page;
				sharedAddr[1] = len2;
				sharedAddr[2] = ROM_LOCATION+page;
				sharedAddr[3] = commandRead;

				IPC_SendSync(0xEE24);

				waitForArm7();
				// -------------------------------------
				#endif

				// read via the 512b ram cache
				fastCopy32(ROM_LOCATION+page, cacheBuffer, 512);
				*cachePage = page;
				(*readCachedRef)(cacheStruct);
			}
			len = cardStruct[2];
			if(len>0) {
				src = cardStruct[0];
				dst = cardStruct[1];
				page = (src/512)*512;
			}
		}
	} else if (ROMinRAM==2) {
		if(dst > 0x02400000 && dst < 0x02800000) {
			if(use12MB==2) {
				return 0;	// Reject data from being loaded into debug 4MB area
			} else if(use12MB==1) {
				dst -= 0x00400000;
			}
		}

		if(setDataBWlist[3]==true && src >= setDataBWlist[0] && src < setDataBWlist[1]) {
			u32 ROM_LOCATION2 = ROM_LOCATION-setDataBWlist[0];

			while(len > 0) {
				u32 len2=len;
				if(len2 > 512) {
					len2 -= src%4;
					len2 -= len2 % 32;
				}

				if(len2 >= 512 && len2 % 32 == 0 && ((u32)dst)%4 == 0 && src%4 == 0) {
					#ifdef DEBUG
					// send a log command for debug purpose
					// -------------------------------------
					commandRead = 0x026ff800;

					sharedAddr[0] = dst;
					sharedAddr[1] = len2;
					sharedAddr[2] = ROM_LOCATION2+src;
					sharedAddr[3] = commandRead;

					IPC_SendSync(0xEE24);

					waitForArm7();
					// -------------------------------------
					#endif

					// read ROM loaded into RAM
					fastCopy32(ROM_LOCATION2+src,dst,len2);

					// update cardi common
					cardStruct[0] = src + len2;
					cardStruct[1] = dst + len2;
					cardStruct[2] = len - len2;
				} else {
					#ifdef DEBUG
					// send a log command for debug purpose
					// -------------------------------------
					commandRead = 0x026ff800;

					sharedAddr[0] = page;
					sharedAddr[1] = len2;
					sharedAddr[2] = ROM_LOCATION2+page;
					sharedAddr[3] = commandRead;

					IPC_SendSync(0xEE24);

					waitForArm7();
					// -------------------------------------
					#endif

					// read via the 512b ram cache
					fastCopy32(ROM_LOCATION2+page, cacheBuffer, 512);
					*cachePage = page;
					(*readCachedRef)(cacheStruct);
				}
				len = cardStruct[2];
				if(len>0) {
					src = cardStruct[0];
					dst = cardStruct[1];
					page = (src/512)*512;
				}
			}
		} else if(setDataBWlist[3]==false && src > 0 && src < setDataBWlist[0]) {
			while(len > 0) {
				u32 len2=len;
				if(len2 > 512) {
					len2 -= src%4;
					len2 -= len2 % 32;
				}

				if(len2 >= 512 && len2 % 32 == 0 && ((u32)dst)%4 == 0 && src%4 == 0) {
					#ifdef DEBUG
					// send a log command for debug purpose
					// -------------------------------------
					commandRead = 0x026ff800;

					sharedAddr[0] = dst;
					sharedAddr[1] = len2;
					sharedAddr[2] = ROM_LOCATION+src;
					sharedAddr[3] = commandRead;

					IPC_SendSync(0xEE24);

					waitForArm7();
					// -------------------------------------*/
					#endif

					// read ROM loaded into RAM
					fastCopy32(ROM_LOCATION+src,dst,len2);

					// update cardi common
					cardStruct[0] = src + len2;
					cardStruct[1] = dst + len2;
					cardStruct[2] = len - len2;
				} else {
					#ifdef DEBUG
					// send a log command for debug purpose
					// -------------------------------------
					commandRead = 0x026ff800;

					sharedAddr[0] = page;
					sharedAddr[1] = len2;
					sharedAddr[2] = ROM_LOCATION+page;
					sharedAddr[3] = commandRead;

					IPC_SendSync(0xEE24);

					waitForArm7();
					// -------------------------------------
					#endif

					// read via the 512b ram cache
					fastCopy32(ROM_LOCATION+page, cacheBuffer, 512);
					*cachePage = page;
					(*readCachedRef)(cacheStruct);
				}
				len = cardStruct[2];
				if(len>0) {
					src = cardStruct[0];
					dst = cardStruct[1];
					page = (src/512)*512;
				}
			}
		} else if(setDataBWlist[3]==false && src >= setDataBWlist[1] && src < romSize) {
			while(len > 0) {
				u32 len2=len;
				if(len2 > 512) {
					len2 -= src%4;
					len2 -= len2 % 32;
				}

				if(len2 >= 512 && len2 % 32 == 0 && ((u32)dst)%4 == 0 && src%4 == 0) {
					#ifdef DEBUG
					// send a log command for debug purpose
					// -------------------------------------
					commandRead = 0x026ff800;

					sharedAddr[0] = dst;
					sharedAddr[1] = len2;
					sharedAddr[2] = ROM_LOCATION-setDataBWlist[2]+src;
					sharedAddr[3] = commandRead;

					IPC_SendSync(0xEE24);

					waitForArm7();
					// -------------------------------------*/
					#endif

					// read ROM loaded into RAM
					fastCopy32(ROM_LOCATION-setDataBWlist[2]+src,dst,len2);

					// update cardi common
					cardStruct[0] = src + len2;
					cardStruct[1] = dst + len2;
					cardStruct[2] = len - len2;
				} else {
					#ifdef DEBUG
					// send a log command for debug purpose
					// -------------------------------------
					commandRead = 0x026ff800;

					sharedAddr[0] = page;
					sharedAddr[1] = len2;
					sharedAddr[2] = ROM_LOCATION-setDataBWlist[2]+page;
					sharedAddr[3] = commandRead;

					IPC_SendSync(0xEE24);

					waitForArm7();
					// -------------------------------------
					#endif

					// read via the 512b ram cache
					fastCopy32(ROM_LOCATION-setDataBWlist[2]+page, cacheBuffer, 512);
					*cachePage = page;
					(*readCachedRef)(cacheStruct);
				}
				len = cardStruct[2];
				if(len>0) {
					src = cardStruct[0];
					dst = cardStruct[1];
					page = (src/512)*512;
				}
			}
		} else if(page == src && len > setDataBWlist[6] && dst < 0x02700000 && dst > 0x02000000 && ((u32)dst)%4==0) {
			accessCounterIncrease();

			// read directly at arm7 level
			commandRead = 0x025FFB08;

			cacheFlush();

			sharedAddr[0] = dst;
			sharedAddr[1] = len;
			sharedAddr[2] = src;
			sharedAddr[3] = commandRead;

			IPC_SendSync(0xEE24);

			waitForArm7();
		} else {
			accessCounterIncrease();

			u32 sector = (src/setDataBWlist[6])*setDataBWlist[6];

			while(len > 0) {
				int slot = 0;
				vu8* buffer = 0;
				if(dsiWramUsed) {
					slot = WRAM_getSlotForSector(sector);
					buffer = WRAM_getCacheAddress(slot);
				} else {
					slot = GAME_getSlotForSector(sector);
					buffer = GAME_getCacheAddress(slot);
				}
				// read max CACHE_READ_SIZE via the main RAM cache
				if(slot==-1) {
					// send a command to the arm7 to fill the RAM cache
					commandRead = 0x025FFB08;

					if(dsiWramUsed) {
						slot = WRAM_allocateCacheSlot();

						buffer = WRAM_getCacheAddress(slot);
					} else {
						slot = GAME_allocateCacheSlot();

						buffer = GAME_getCacheAddress(slot);
					}

					if(needFlushDCCache) DC_FlushRange(buffer, setDataBWlist[6]);

					// transfer the WRAM-B cache to the arm7
					if(dsiWramUsed) transfertToArm7(slot);

					// write the command
					sharedAddr[0] = buffer;
					sharedAddr[1] = setDataBWlist[6];
					sharedAddr[2] = sector;
					sharedAddr[3] = commandRead;

					IPC_SendSync(0xEE24);

					waitForArm7();

					// transfer back the WRAM-B cache to the arm9
					if(dsiWramUsed) transfertToArm9(slot);
				}

				if(dsiWramUsed) {
					WRAM_updateDescriptor(slot, sector);
				} else {
					updateDescriptor(slot, sector);
				}

				u32 len2=len;
				if((src - sector) + len2 > setDataBWlist[6]){
					len2 = sector - src + setDataBWlist[6];
				}

				if(len2 > 512) {
					len2 -= src%4;
					len2 -= len2 % 32;
				}

				if(len2 >= 512 && len2 % 32 == 0 && ((u32)dst)%4 == 0 && src%4 == 0) {
					#ifdef DEBUG
					// send a log command for debug purpose
					// -------------------------------------
					commandRead = 0x026ff800;

					sharedAddr[0] = dst;
					sharedAddr[1] = len2;
					sharedAddr[2] = buffer+src-sector;
					sharedAddr[3] = commandRead;

					IPC_SendSync(0xEE24);

					waitForArm7();
					// -------------------------------------*/
					#endif

					// copy directly
					fastCopy32(buffer+(src-sector),dst,len2);

					// update cardi common
					cardStruct[0] = src + len2;
					cardStruct[1] = dst + len2;
					cardStruct[2] = len - len2;
				} else {
					#ifdef DEBUG
					// send a log command for debug purpose
					// -------------------------------------
					commandRead = 0x026ff800;

					sharedAddr[0] = page;
					sharedAddr[1] = len2;
					sharedAddr[2] = buffer+page-sector;
					sharedAddr[3] = commandRead;

					IPC_SendSync(0xEE24);

					waitForArm7();
					// -------------------------------------*/
					#endif

					// read via the 512b ram cache
					fastCopy32(buffer+(page-sector), cacheBuffer, 512);
					*cachePage = page;
					(*readCachedRef)(cacheStruct);
				}
				len = cardStruct[2];
				if(len>0) {
					src = cardStruct[0];
					dst = cardStruct[1];
					page = (src/512)*512;
					sector = (src/setDataBWlist[6])*setDataBWlist[6];
					accessCounterIncrease();
				}
			}
		}
	}
	REG_IME = IME_ENABLE;
	return 0;
}




