/*
 *  Cartridge.cpp - Cartridge emulation
 *
 *  Frodo Copyright (C) Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "sysdeps.h"

#include "Cartridge.h"

#ifndef FRODO_RP2350
#include <filesystem>
namespace fs = std::filesystem;
#endif


// Base class for cartridge with ROM
ROMCartridge::ROMCartridge(unsigned num_banks, unsigned bank_size) : numBanks(num_banks), bankSize(bank_size)
{
	// Allocate ROM
#ifdef PSRAM_MAX_FREQ_MHZ
	rom = (uint8_t*)psram_malloc(num_banks * bank_size);
#else
	rom = (uint8_t*)malloc(num_banks * bank_size);
#endif
	memset(rom, 0xff, num_banks * bank_size);
}

ROMCartridge::~ROMCartridge()
{
	// Free ROM
#ifdef PSRAM_MAX_FREQ_MHZ
	psram_free(rom);
#else
	free(rom);
#endif
}


// 8K ROM cartridge (EXROM = 0, GAME = 1)
Cartridge8K::Cartridge8K() : ROMCartridge(1, 0x2000)
{
	notEXROM = false;
}

uint8_t Cartridge8K::ReadROML(uint16_t adr, uint8_t ram_byte, bool notLoram)
{
	return notLoram ? rom[adr] : ram_byte;
}


// 16K ROM cartridge (EXROM = 0, GAME = 0)
Cartridge16K::Cartridge16K() : ROMCartridge(1, 0x4000)
{
	notEXROM = false;
	notGAME = false;
}

uint8_t Cartridge16K::ReadROML(uint16_t adr, uint8_t ram_byte, bool notLoram)
{
	return notLoram ? rom[adr] : ram_byte;
}

uint8_t Cartridge16K::ReadROMH(uint16_t adr, uint8_t ram_byte, uint8_t basic_byte, bool notLoram, bool notHiram)
{
	return notHiram ? rom[adr + 0x2000] : ram_byte;
}


// Simons' BASIC cartridge (switchable 8K/16K ROM cartridge)
CartridgeSimonsBasic::CartridgeSimonsBasic() : ROMCartridge(1, 0x4000)
{
	notEXROM = false;
	notGAME = true;
}

void CartridgeSimonsBasic::Reset()
{
	notGAME = true;
}

uint8_t CartridgeSimonsBasic::ReadROML(uint16_t adr, uint8_t ram_byte, bool notLoram)
{
	return notLoram ? rom[adr] : ram_byte;
}

uint8_t CartridgeSimonsBasic::ReadROMH(uint16_t adr, uint8_t ram_byte, uint8_t basic_byte, bool notLoram, bool notHiram)
{
	return notHiram ? rom[adr + 0x2000] : ram_byte;
}

uint8_t CartridgeSimonsBasic::ReadIO1(uint16_t adr, uint8_t bus_byte)
{
	notGAME = true;		// 8K mode
	return bus_byte;
}

void CartridgeSimonsBasic::WriteIO1(uint16_t adr, uint8_t byte)
{
	notGAME = false;	// 16K mode
}


// Ocean cartridge (banked 8K/16K ROM cartridge)
CartridgeOcean::CartridgeOcean(bool not_game) : ROMCartridge(64, 0x2000)
{
	notEXROM = false;
	notGAME = not_game;
}

void CartridgeOcean::Reset()
{
	bank = 0;
}

uint8_t CartridgeOcean::ReadROML(uint16_t adr, uint8_t ram_byte, bool notLoram)
{
	return notLoram ? rom[adr + bank * bankSize] : ram_byte;
}

uint8_t CartridgeOcean::ReadROMH(uint16_t adr, uint8_t ram_byte, uint8_t basic_byte, bool notLoram, bool notHiram)
{
	return notHiram ? rom[adr + bank * bankSize] : ram_byte;
}

void CartridgeOcean::WriteIO1(uint16_t adr, uint8_t byte)
{
	bank = byte & 0x3f;
}


// Fun Play cartridge (banked 8K ROM cartridge)
CartridgeFunPlay::CartridgeFunPlay() : ROMCartridge(64, 0x2000)
{
	notEXROM = false;
}

void CartridgeFunPlay::Reset()
{
	notEXROM = false;

	bank = 0;
}

uint8_t CartridgeFunPlay::ReadROML(uint16_t adr, uint8_t ram_byte, bool notLoram)
{
	return notLoram ? rom[adr + bank * bankSize] : ram_byte;
}

void CartridgeFunPlay::WriteIO1(uint16_t adr, uint8_t byte)
{
	bank = byte & 0x39;
	notEXROM = (byte & 0xc6) == 0x86;
}


// Super Games cartridge (banked 16K ROM cartridge)
CartridgeSuperGames::CartridgeSuperGames() : ROMCartridge(4, 0x4000)
{
	notEXROM = false;
	notGAME = false;
}

void CartridgeSuperGames::Reset()
{
	notEXROM = false;
	notGAME = false;

	bank = 0;
	disableIO2 = false;
}

uint8_t CartridgeSuperGames::ReadROML(uint16_t adr, uint8_t ram_byte, bool notLoram)
{
	return notLoram ? rom[adr + bank * bankSize] : ram_byte;
}

uint8_t CartridgeSuperGames::ReadROMH(uint16_t adr, uint8_t ram_byte, uint8_t basic_byte, bool notLoram, bool notHiram)
{
	return notHiram ? rom[adr + bank * bankSize + 0x2000] : ram_byte;
}

void CartridgeSuperGames::WriteIO2(uint16_t adr, uint8_t byte)
{
	if (! disableIO2) {
		bank = byte & 0x03;
		notEXROM = notGAME = byte & 0x04;
		disableIO2 = byte & 0x08;
	}
}


// C64 Games System cartridge (banked 8K ROM cartridge)
CartridgeC64GS::CartridgeC64GS() : ROMCartridge(64, 0x2000)
{
	notEXROM = false;
}

void CartridgeC64GS::Reset()
{
	bank = 0;
}

uint8_t CartridgeC64GS::ReadROML(uint16_t adr, uint8_t ram_byte, bool notLoram)
{
	return notLoram ? rom[adr + bank * bankSize] : ram_byte;
}

uint8_t CartridgeC64GS::ReadIO1(uint16_t adr, uint8_t bus_byte)
{
	bank = adr & 0x3f;
	return bus_byte;
}

void CartridgeC64GS::WriteIO1(uint16_t adr, uint8_t byte)
{
	bank = adr & 0x3f;
}


// Dinamic cartridge (banked 8K ROM cartridge)
CartridgeDinamic::CartridgeDinamic() : ROMCartridge(16, 0x2000)
{
	notEXROM = false;
}

void CartridgeDinamic::Reset()
{
	bank = 0;
}

uint8_t CartridgeDinamic::ReadROML(uint16_t adr, uint8_t ram_byte, bool notLoram)
{
	return notLoram ? rom[adr + bank * bankSize] : ram_byte;
}

uint8_t CartridgeDinamic::ReadIO1(uint16_t adr, uint8_t bus_byte)
{
	bank = adr & 0x0f;
	return bus_byte;
}


// Zaxxon cartridge (banked 16K ROM cartridge)
CartridgeZaxxon::CartridgeZaxxon() : ROMCartridge(3, 0x2000)
{
	notEXROM = false;
	notGAME = false;
}

void CartridgeZaxxon::Reset()
{
	bank = 0;
}

uint8_t CartridgeZaxxon::ReadROML(uint16_t adr, uint8_t ram_byte, bool notLoram)
{
	if (notLoram) {
		if (adr < 0x1000) {
			bank = 0;
			return rom[adr];
		} else {
			bank = 1;
			return rom[adr & 0xfff];
		}
	} else {
		return ram_byte;
	}
}

uint8_t CartridgeZaxxon::ReadROMH(uint16_t adr, uint8_t ram_byte, uint8_t basic_byte, bool notLoram, bool notHiram)
{
	return notHiram ? rom[adr + bank * bankSize + 0x2000] : ram_byte;
}


// Magic Desk / Marina64 cartridge (banked 8K ROM cartridge)
CartridgeMagicDesk::CartridgeMagicDesk() : ROMCartridge(128, 0x2000)
{
	notEXROM = false;
}

void CartridgeMagicDesk::Reset()
{
	notEXROM = false;

	bank = 0;
}

uint8_t CartridgeMagicDesk::ReadROML(uint16_t adr, uint8_t ram_byte, bool notLoram)
{
	return notLoram ? rom[adr + bank * bankSize] : ram_byte;
}

void CartridgeMagicDesk::WriteIO1(uint16_t adr, uint8_t byte)
{
	bank = byte & 0x7f;
	notEXROM = byte & 0x80;
}


// COMAL 80 cartridge (banked 16K ROM cartridge)
CartridgeComal80::CartridgeComal80() : ROMCartridge(4, 0x4000)
{
	notEXROM = false;
	notGAME = false;
}

void CartridgeComal80::Reset()
{
	bank = 0;
}

uint8_t CartridgeComal80::ReadROML(uint16_t adr, uint8_t ram_byte, bool notLoram)
{
	return notLoram ? rom[adr + bank * bankSize] : ram_byte;
}

uint8_t CartridgeComal80::ReadROMH(uint16_t adr, uint8_t ram_byte, uint8_t basic_byte, bool notLoram, bool notHiram)
{
	return notHiram ? rom[adr + bank * bankSize + 0x2000] : ram_byte;
}

void CartridgeComal80::WriteIO1(uint16_t adr, uint8_t byte)
{
	bank = byte & 0x03;
}


/*
 *  EasyFlash cartridge implementation
 *  Based on official EasyFlash Programmer's Guide by Thomas 'skoe' Giesel
 *  https://skoe.de/easyflash/files/devdocs/EasyFlash-ProgRef.pdf
 *
 *  Hardware:
 *  - Two 512 KiB flash chips (ROML and ROMH), 64 banks of 8 KiB each
 *  - 256 bytes of RAM at $DF00-$DFFF (always visible)
 *  - Bank register at $DE00 (write-only)
 *  - Control register at $DE02 (write-only)
 */

#ifdef FRODO_RP2350
#ifdef PSRAM_MAX_FREQ_MHZ
#include "psram_allocator.h"
#endif
#endif

CartridgeEasyFlash::CartridgeEasyFlash()
{
	// Allocate ROML and ROMH banks (64 * 8KB each = 512KB each)
#ifdef FRODO_RP2350
#ifdef PSRAM_MAX_FREQ_MHZ
	roml = (uint8_t *)psram_malloc(NUM_BANKS * BANK_SIZE);
	romh = (uint8_t *)psram_malloc(NUM_BANKS * BANK_SIZE);
#else
	roml = (uint8_t *)malloc(NUM_BANKS * BANK_SIZE);
	romh = (uint8_t *)malloc(NUM_BANKS * BANK_SIZE);
#endif
#else
	roml = new uint8_t[NUM_BANKS * BANK_SIZE];
	romh = new uint8_t[NUM_BANKS * BANK_SIZE];
#endif
	memset(roml, 0xff, NUM_BANKS * BANK_SIZE);
	memset(romh, 0xff, NUM_BANKS * BANK_SIZE);
	memset(ram, 0xff, sizeof(ram));

	// Boot jumper in "Boot" position (directly start cartridge)
	jumper = true;

	// Initial state: Ultimax mode (per official docs, boot starts in Ultimax)
	notEXROM = true;   // /EXROM high
	notGAME = false;   // /GAME low
}

CartridgeEasyFlash::~CartridgeEasyFlash()
{
#ifdef FRODO_RP2350
#ifdef PSRAM_MAX_FREQ_MHZ
	psram_free(roml);
	psram_free(romh);
#else
	free(roml);
	free(romh);
#endif
	delete[] roml;
	delete[] romh;
#endif
}

void CartridgeEasyFlash::Reset()
{
	// Per official docs: "The value after reset is $00" for both registers
	bank = 0;
	mode = 0;

	// Per official docs section 2.2:
	// "If the boot switch is in position 'Boot' and the computer is reset,
	//  EasyFlash is started normally: The Ultimax memory configuration is set,
	//  bank 0 selected. The CPU starts at the reset vector at $FFFC."
	// "In Ultimax mode the ROMH chip is banked in at $E000"
	if (jumper) {
		// Boot mode: Ultimax configuration
		notEXROM = true;   // /EXROM high
		notGAME = false;   // /GAME low
	} else {
		// Disable mode: Cartridge hidden
		notEXROM = true;   // /EXROM high
		notGAME = true;    // /GAME high
	}
}

/*
 *  Update memory configuration based on mode register ($DE02)
 *
 *  From official docs Table 2.4 - Bits of $DE02:
 *    Bit 7 (L): LED
 *    Bit 2 (M): GAME mode, 1 = controlled by bit G, 0 = from jumper "boot"
 *    Bit 1 (X): EXROM state, 0 = /EXROM high
 *    Bit 0 (G): GAME state if M = 1, 0 = /GAME high
 *
 *  From official docs Table 2.5 - MXG configurations:
 *    MXG=000: GAME from jumper, EXROM high (Ultimax or Off depending on jumper)
 *    MXG=001: Reserved
 *    MXG=010: GAME from jumper, EXROM low (16K or 8K depending on jumper)
 *    MXG=011: Reserved
 *    MXG=100: Cartridge ROM off (RAM at $DF00 still available)
 *    MXG=101: Ultimax (ROML at $8000, ROMH at $E000)
 *    MXG=110: 8K Cartridge (ROML at $8000)
 *    MXG=111: 16K Cartridge (ROML at $8000, ROMH at $A000)
 *
 *  From official docs Table 2.1 - /GAME and /EXROM states:
 *    /GAME=1, /EXROM=1: Invisible (off)
 *    /GAME=1, /EXROM=0: 8K mode
 *    /GAME=0, /EXROM=0: 16K mode
 *    /GAME=0, /EXROM=1: Ultimax mode
 */
void CartridgeEasyFlash::UpdateMemConfig()
{
	uint8_t M = (mode >> 2) & 1;  // Bit 2: GAME mode select
	uint8_t X = (mode >> 1) & 1;  // Bit 1: EXROM state
	uint8_t G = mode & 1;         // Bit 0: GAME state

	// Determine /EXROM: X=0 means /EXROM high (inactive), X=1 means /EXROM low (active)
	notEXROM = (X == 0);

	// Determine /GAME based on M bit
	if (M == 1) {
		// GAME controlled by G bit: G=0 means /GAME high, G=1 means /GAME low
		notGAME = (G == 0);
	} else {
		// GAME from jumper: jumper=true (Boot) means /GAME low, jumper=false (Disable) means /GAME high
		notGAME = !jumper;
	}
}

uint8_t CartridgeEasyFlash::ReadROML(uint16_t adr, uint8_t ram_byte, bool notLoram)
{
	// ROML is visible at $8000-$9FFF in these modes:
	// - 8K mode:    /GAME=1, /EXROM=0 -> ROML when LORAM high
	// - 16K mode:   /GAME=0, /EXROM=0 -> ROML when LORAM high
	// - Ultimax:    /GAME=0, /EXROM=1 -> ROML always visible

	if (!notEXROM) {
		// 8K or 16K mode: ROML at $8000 only when LORAM high
		return notLoram ? roml[bank * BANK_SIZE + adr] : ram_byte;
	} else if (!notGAME) {
		// Ultimax mode: ROML always visible at $8000
		return roml[bank * BANK_SIZE + adr];
	}

	// Cartridge off: return RAM
	return ram_byte;
}

uint8_t CartridgeEasyFlash::ReadROMH(uint16_t adr, uint8_t ram_byte, uint8_t basic_byte, bool notLoram, bool notHiram)
{
	// ROMH visibility:
	// - 16K mode:   /GAME=0, /EXROM=0 -> ROMH at $A000 when HIRAM high
	// - Ultimax:    /GAME=0, /EXROM=1 -> ROMH at $E000 always visible

	if (!notGAME && !notEXROM) {
		// 16K mode: ROMH at $A000 only when HIRAM high
		return notHiram ? romh[bank * BANK_SIZE + adr] : ram_byte;
	} else if (!notGAME && notEXROM) {
		// Ultimax mode: ROMH always visible at $E000
		return romh[bank * BANK_SIZE + adr];
	}

	// 8K mode or cartridge off: return BASIC ROM or RAM
	return notLoram ? basic_byte : ram_byte;
}

uint8_t CartridgeEasyFlash::ReadIO1(uint16_t adr, uint8_t bus_byte)
{
	// Per official docs: $DE00 and $DE02 are write-only registers
	// Reading returns open bus
	return bus_byte;
}

void CartridgeEasyFlash::WriteIO1(uint16_t adr, uint8_t byte)
{
	// I/O 1 area is $DE00-$DEFF, mirrored
	// Only $DE00 and $DE02 are used

	if ((adr & 0x02) == 0) {
		// $DE00, $DE04, $DE08, etc.: Bank register
		// Per official docs Table 2.2: bits 5-0 are bank, bits 7-6 must be 0
		bank = byte & 0x3f;
	} else {
		// $DE02, $DE06, $DE0A, etc.: Control register
		// Per official docs Table 2.3: bits used are 7 (LED), 2 (M), 1 (X), 0 (G)
		mode = byte & 0x87;  // Mask valid bits
		UpdateMemConfig();
	}
}

uint8_t CartridgeEasyFlash::ReadIO2(uint16_t adr, uint8_t bus_byte)
{
	// Per official docs section 2.3:
	// "An EasyFlash cartridge has 256 Bytes of RAM. This memory is always visible.
	//  It can be used to save small portions of code or data...
	//  The RAM is located at $DF00."
	return ram[adr & 0xff];
}

void CartridgeEasyFlash::WriteIO2(uint16_t adr, uint8_t byte)
{
	// Per official docs: RAM at $DF00-$DFFF is always writable
	ram[adr & 0xff] = byte;
}


/*
 *  Check whether file is a cartridge image file
 */

bool IsCartridgeFile(const std::string & path)
{
#ifndef FRODO_RP2350
	// Reject directories
	if (fs::is_directory(path))
		return false;
#endif

	// Read file header
	FILE * f = fopen(path.c_str(), "rb");
	if (f == nullptr)
		return false;

	uint8_t header[64];
	if (fread(header, sizeof(header), 1, f) != 1) {
		fclose(f);
		return false;
	}

	fclose(f);

	// Check for signature and version
	uint16_t version = (header[0x14] << 8) | header[0x15];
	return memcmp(header, "C64 CARTRIDGE   ", 16) == 0 && version == 0x0100;
}


/*
 *  Construct cartridge object from image file path, return nullptr on error
 */

Cartridge * Cartridge::FromFile(const std::string & path, std::string & ret_error_msg)
{
	// Empty path = no cartridge
	if (path.empty())
		return nullptr;

	ROMCartridge * cart = nullptr;
	FILE * f = nullptr;
	{
		// Read file header
		f = fopen(path.c_str(), "rb");
		if (f == nullptr) {
			ret_error_msg = "Can't open cartridge file";
			return nullptr;
		}

		uint8_t header[64];
		if (fread(header, sizeof(header), 1, f) != 1)
			goto error_read;

		// Check for signature and version
		uint16_t version = (header[0x14] << 8) | header[0x15];
		if (memcmp(header, "C64 CARTRIDGE   ", 16) != 0 || version != 0x0100)
			goto error_unsupp;

		// Create cartridge object according to type
		uint16_t type = (header[0x16] << 8) | header[0x17];
		uint8_t exrom = header[0x18];
		uint8_t game = header[0x19];

#ifdef FRODO_RP2350
		printf("CRT: type=%d exrom=%d game=%d\n", type, exrom, game);
#endif

		switch (type) {
			case 0:
				if (exrom != 0)		// Ultimax or not a ROM cartridge
					goto error_unsupp;
				if (game == 0) {
					cart = new Cartridge16K;
				} else {
					cart = new Cartridge8K;
				}
				break;
			case 4:
				cart = new CartridgeSimonsBasic;
				break;
			case 5:
				cart = new CartridgeOcean(game);
				break;
			case 7:
				cart = new CartridgeFunPlay;
				break;
			case 8:
				cart = new CartridgeSuperGames;
				break;
			case 15:
				cart = new CartridgeC64GS;
				break;
			case 17:
				cart = new CartridgeDinamic;
				break;
			case 18:
				cart = new CartridgeZaxxon;
				break;
			case 19:
				cart = new CartridgeMagicDesk;
				break;
			case 21:
				cart = new CartridgeComal80;
				break;
			case 32: {
				// EasyFlash cartridge - special handling (not ROMCartridge)
				CartridgeEasyFlash * ef = new CartridgeEasyFlash;

				// Load CHIP packets for EasyFlash
				while (true) {
					size_t actual = fread(header, 1, 16, f);
					if (actual == 0)
						break;
					if (actual != 16) {
						delete ef;
						goto error_read;
					}

					uint16_t chip_type  = (header[0x08] << 8) | header[0x09];
					uint16_t chip_bank  = (header[0x0a] << 8) | header[0x0b];
					uint16_t chip_start = (header[0x0c] << 8) | header[0x0d];
					uint16_t chip_size  = (header[0x0e] << 8) | header[0x0f];

					// chip_type: 0=ROM, 1=RAM, 2=Flash ROM (EasyFlash uses 2)
					if (memcmp(header, "CHIP", 4) != 0 ||
					    (chip_type != 0 && chip_type != 2) ||
					    chip_bank >= CartridgeEasyFlash::NUM_BANKS ||
					    chip_size > CartridgeEasyFlash::BANK_SIZE) {
						delete ef;
						goto error_unsupp;
					}

					// Load to ROML ($8000) or ROMH ($A000/$E000) based on chip_start
					uint8_t * dest;
					if (chip_start == 0x8000) {
						dest = ef->RomL() + chip_bank * CartridgeEasyFlash::BANK_SIZE;
					} else if (chip_start == 0xa000 || chip_start == 0xe000) {
						dest = ef->RomH() + chip_bank * CartridgeEasyFlash::BANK_SIZE;
					} else {
						delete ef;
						goto error_unsupp;
					}

					if (fread(dest, chip_size, 1, f) != 1) {
						delete ef;
						goto error_read;
					}
				}

				fclose(f);
				return ef;
			}
			default:
				goto error_unsupp;
		}

		// Load CHIP packets for standard ROMCartridge types
		while (true) {

			// Load packet header
			size_t actual = fread(header, 1, 16, f);
			if (actual == 0)
				break;
			if (actual != 16)
				goto error_read;

			// Check for signature and size
			uint16_t chip_type  = (header[0x08] << 8) | header[0x09];
			uint16_t chip_bank  = (header[0x0a] << 8) | header[0x0b];
			uint16_t chip_start = (header[0x0c] << 8) | header[0x0d];
			uint16_t chip_size  = (header[0x0e] << 8) | header[0x0f];
			if (memcmp(header, "CHIP", 4) != 0 || chip_type != 0 || chip_bank >= cart->numBanks || chip_size > cart->bankSize)
				goto error_unsupp;

			// Load packet contents
			uint32_t offset = chip_bank * cart->bankSize;

			if (type == 4 && chip_start == 0xa000) {			// Special mapping for Simons' BASIC
				offset = 0x2000;
			} else if (type == 18 && chip_start == 0xa000) {	// Special mapping for Zaxxon
				offset += 0x2000;
			}

			if (fread(cart->ROM() + offset, chip_size, 1, f) != 1)
				goto error_read;
		}

		fclose(f);
	}
	return cart;

error_read:
	delete cart;
	fclose(f);

	ret_error_msg = "Error reading cartridge file";
	return nullptr;

error_unsupp:
	delete cart;
	fclose(f);

	ret_error_msg = "Unsupported cartridge type";
	return nullptr;
}
