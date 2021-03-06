// GPUSimSW.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <stdio.h>
#include <memory.h>

#include "GPUCommandGen.h"

class Vgpu;
#include "../../../rtl/obj_dir/Vgpu.h"

// My own scanner to generate VCD file.
#define VCSCANNER_IMPL
#include "VCScanner.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "MiniFB.h"

extern void loadImageToVRAMAsCommand(GPUCommandGen& commandGenerator, const char* fileName, int x, int y, bool imgDefaultFlag);
extern void loadImageToVRAM(Vgpu* mod, const char* filename, u8* target, int x, int y, bool flagValue);
extern void dumpFrame(Vgpu* mod, const char* name, const char* maskName, unsigned char* buffer, int clockCounter, bool saveMask);
extern void registerVerilatedMemberIntoScanner(Vgpu* mod, VCScanner* pScan);
extern void addEnumIntoScanner(VCScanner* pScan);
extern void drawCheckedBoard(unsigned char* buffer);
extern void backupFromStencil(Vgpu* mod, u8* refStencil);
extern void backupToStencil(Vgpu* mod, u8* refStencil);
extern bool ReadStencil(Vgpu* mod, int x, int y);
extern void setStencil(Vgpu* mod, int x,int y, bool v);

void Convert16To32(u8* buffer, u8* data);
void commandDecoder(u32* pStream, u32 size, struct mfb_window* window);

bool verifyStencilVRAM(Vgpu* mod, u16* buffer) {
	for (int y=0; y < 512; y++) {
		for (int x=0; x < 1024; x++) {
			bool stencil = ReadStencil(mod, x,y);
			bool bBit    = buffer[x + (y*1024)] & 0x8000 ? true : false;

			if (stencil != bBit) {
				return true;
			}
		}
	}
	return false;
}

int main2();
void main3();

extern void test(int from, int to, int l, int* adr, int* adrCount);

int adrStorage[2000];
int adrStorageCount;

void softVVCopy(int XSrc, int SrcY, int XDst, int DstY, int width, int height, u16* softbuffer, bool checkBit, bool forceBit) {
	u16 forceMask = (forceBit ? 0x8000 : 0);

	int xSEnd = XSrc + width;
	// Not on the same line dst -> This reference has no need to check.

	while (XSrc < xSEnd) {
		u16 v = softbuffer[((SrcY & 0x1FF)*1024) + (XSrc&0x3FF)];
		u16 dst = softbuffer[((DstY & 0x1FF)*1024) + (XDst&0x3FF)];
		if ((!checkBit) || ((dst&0x8000) == 0)) {
			softbuffer[((DstY & 0x1FF)*1024) + (XDst&0x3FF)] = v | forceMask;
		}
		XSrc++;
		XDst++;
	}
}

int mymemcmp(void* a_, void* b_, int count) {
	u16* a = (u16*)a_;
	u16* b = (u16*)b_;

	for (int n = 0; n < (count/sizeof(u16)); n++) {
		if (a[n] != b[n]) {
			int x = n % 1024;
			int y = n / 1024;

			printf("here");
			return -1;
		}
	}
	return 0;
}

u16 swBuffer[1024*512];
u8* buffer32Bit;

int main(int argcount, char** args)
{
	int sFrom = 0;
	int sTo   = 0;
	int sL    = 0;

	if (argcount > 3) {
		sscanf(args[1], "%i", &sFrom);
		sscanf(args[2], "%i", &sTo);
		sscanf(args[3], "%i", &sL);
	}
	// ------------------------------------------------------------------
	// SETUP : Export VCD Log for GTKWave ?
	// ------------------------------------------------------------------
	bool useScan = true;

	const int   useScanRange			= false;
	const int	scanStartCycle			= 30;
	const int	scanEndCycle			= 50;

	// ------------------------------------------------------------------
	// Export Buffer as PNG ?
	// ------------------------------------------------------------------
	const bool  useScreenShot				= false;
	const bool useMaskDump					= false;
	const int  startRange					= 300;
	const int  endRange						= 400;
	const int	screenShotmoduloSpeed		= 0xFFF; // 4096 cycles.

	// Put background for debug.
	const bool	useCheckedBoard				= false;

	// ------------------------------------------------------------------
	// Fake VRAM PSX
	// ------------------------------------------------------------------
	unsigned char* buffer     = new unsigned char[1024*1024];
	unsigned char* refBuffer  = new unsigned char[1024*1024];
	unsigned char* softbuffer = new unsigned char[1024*1024];
	unsigned char* refStencil = new unsigned char[16384 * 32]; 

	memset(buffer,0,1024*1024);
	memset(&buffer[2048],0x77,2048*511);

	if (useCheckedBoard) {
		drawCheckedBoard(buffer);
	}

	// ------------------------------------------------------------------
	// [Instance of verilated GPU & custom VCD Generator]
	// ------------------------------------------------------------------
	Vgpu* mod		= new Vgpu();
	VCScanner*	pScan = new VCScanner();
				pScan->init(2000);

	registerVerilatedMemberIntoScanner(mod, pScan);
	addEnumIntoScanner(pScan);
	
	// ------------------------------------------------------------------
	// Reset the chip for a few cycles at start...
	// ------------------------------------------------------------------
	mod->i_nrst = 0;
	for (int n=0; n < 10; n++) {
		mod->clk = 0; mod->eval(); mod->clk = 1; mod->eval();
	}
	mod->i_nrst = 1;

	// Not busy by default...
	mod->i_busy        = 0;
	mod->i_dataInValid = 0;
	for (int n=0; n < 8; n++) { mod->i_dataIn[n] = 0; }

	/*	
		I decided to setup the textures as GPU commands,
		this will allow you to extract the information more easily for simulation on another platform.
	*/

	// This is the object used in the main loop to store/send 32 bit word to the GPU.
	GPUCommandGen	commandGenerator;

	if (useScan) {
		pScan->addPlugin(new ValueChangeDump_Plugin("gpuLogFat.vcd"));
	}

#if 1
	enum DEMO {
		NO_TEXTURE,
		TEXTURE_TRUECOLOR_BLENDING,
		TEXTURE_PALETTE_BLENDING,
		COPY_CMD,
		TEST_EMU_DATA,
		USE_AVOCADO_DATA,
		PALETTE_FAIL_LATEST,
		POLY_FAIL,
		COPY_TORAM
	};

	DEMO demo = COPY_TORAM;

	if (demo == TEXTURE_TRUECOLOR_BLENDING) {
		// Load Gradient128x64.png at [0,0] in VRAM as true color 1555 (bit 15 = 0).
		// => Generate GPU upload stream. Will be used as TEXTURE SOURCE for TRUE COLOR TEXTURING.
		loadImageToVRAM(mod,"Gradient128x64.png",buffer,0,0,false);
	}

	if (demo == COPY_CMD) {
		loadImageToVRAM(mod,"Airship.png",buffer,0,0,true);
		memcpy(refBuffer,buffer,1024*1024);
		backupFromStencil(mod,refStencil);

//		loadImageToVRAMAsCommand(commandGenerator,"Line1.png",0,0,true);
		
		/*
		// loadImageToVRAMAsCommand(commandGenerator, "Gradient128x64.png", 0, 0,true);
		commandGenerator.writeRaw(0xa0000000);
		commandGenerator.writeRaw(16 | (0<<16));	// @16,0
		commandGenerator.writeRaw(64 | (1<<16));	// 32 pixel x1
		for (int n=0; n < 32; n++) { // 16 pairs.
			int v = (n*2);
			// 0,2,4,6 Are marked with '1' bit mask.
			commandGenerator.writeRaw((0x8000| (16+v)) | ((v+16+1)<<16));
		}
		*/

		commandGenerator.writeRaw(0xE6000000);						// Set Bit mask, no check.
	}

	if (demo == TEXTURE_PALETTE_BLENDING) {
		// -------------------------------------------------
		// Upload a 4 bit 32x32 pixel texture at 512,64
		// -------------------------------------------------
		commandGenerator.writeRaw(0xa0000000);
		commandGenerator.writeRaw(0 | (0<<16));
		commandGenerator.writeRaw(8 /*8 halfword*/ | (32<<16));
		for (int n=0; n < 32; n++) {
			commandGenerator.writeRaw(0x76543210); commandGenerator.writeRaw(0xFEDCBA98); commandGenerator.writeRaw(0x76543210); commandGenerator.writeRaw(0xFEDCBA98);	// 32 pixel texture 
		}

		// -------------------------------------------------
		// Upload a 4 bit palette : 16 entries. at 512,96
		// -------------------------------------------------
		commandGenerator.writeRaw(0xa0000000); 
		commandGenerator.writeRaw(0 | (64<<16));
		commandGenerator.writeRaw(16 /*8 halfword*/ | (1<<16));
		commandGenerator.writeRaw(0x11118000);
		commandGenerator.writeRaw(0x33332222);
		commandGenerator.writeRaw(0x55554444);
		commandGenerator.writeRaw(0x77776666);
		commandGenerator.writeRaw(0x99998888);
		commandGenerator.writeRaw(0xBBBBAAAA);
		commandGenerator.writeRaw(0xDDDDCCCC);
		commandGenerator.writeRaw(0xFFFFEEEE);
	}

	if (demo == TEST_EMU_DATA) {
		u32 commandsL[] = {
			0xe3000000,
			0xe4077e7f,

			0xe5000000,

			0xe100020a,

			0xe2000000,

			0xcccccccc,
			0x0,
			0x1e00280,
			0xcccccccc,
			0xf000c0,
			0x8cb2,
			0x700140,
			0x8cb2,
			0x1700140,
			0xb2,
			0xf001c0,
			0xcccccccc,
			0xf401b8,
			0x8cb2,
			0x7c0140,
			0x8cb2,
			0x16c0140,
			0xcccccccc,
			0xec00c8,
			0x8cb2,
			0x740140,
			0x8cb2,
			0x1640140,
			0xe3000000,
			0xe4077e7f,
			0xe5000000,
			0xe100020a,
			0xe2000000,
			0xcccccccc,
			0x0,
			0x1e00280,
			0xcccccccc,
			0xf000c0,
			0x8cb2,
			0x700140,
			0x8cb2,
			0x1700140,
			0xb2,
			0xf001c0,
			0xcccccccc,
			0xf501b6,
			0x8cb2,
			0x7e013f,
			0x8cb2,
			0x16c013f,
			0xcccccccc,
			0xeb00ca,
			0x8cb2,
			0x740141,
			0x8cb2,
			0x1620141,
		};


	}
	// ------------------------------------------------------------------
	// Reset the chip for a few cycles at start...
	// ------------------------------------------------------------------

	// Force MASK WRITE (easier for debug)
//	commandGenerator.writeRaw(0xE6000001);

	switch (demo) {
	case NO_TEXTURE:
	{
		commandGenerator.writeRaw(0x380000b2);
		commandGenerator.writeRaw((192<<0) | (240<<16));
		commandGenerator.writeRaw(0x00008cb2);
		commandGenerator.writeRaw((320<<0) | (112<<16));
		commandGenerator.writeRaw(0x00008cb2);
		commandGenerator.writeRaw((320<<0) | (368<<16));
		commandGenerator.writeRaw(0x000000b2);
		commandGenerator.writeRaw((448<<0) | (240<<16));
	}
	break;
	case TEXTURE_TRUECOLOR_BLENDING:
	{
		commandGenerator.writeRaw(0xE6000001);						// Set Bit mask, no check.

#if 1
		commandGenerator.writeRaw(0x27AABBCC);						// Polygon, 3 pts, opaque, raw texture
		// Vertex 1
		commandGenerator.writeRaw(0x01100100);						// [15:0] XCoordinate, [31:16] Y Coordinate (VERTEX 0)
		commandGenerator.writeRaw(0xFFF30000);						// [31:16]Color LUT : NONE, value ignored
																	// [15:8,7:0] Texture [0,0]
		// Vertex 2
		commandGenerator.writeRaw(0x00100180);						// [15:0] XCoordinate, [31:16] Y Coordinate (VERTEX 1)
		commandGenerator.writeRaw(((
			0 | (0<<4) | (0<<5) | (2<<7) | (0<<9) | (0<<11)			// [31:16] Texture at 0(4x64),0 ******4BPP******
			)<<16 )                  | 0x001F);						// [15:8,7:0] Texture [1F,0]
																	// Vertex 3
		commandGenerator.writeRaw(0x01200230);						// [15:0] XCoordinate, [31:16] Y Coordinate
		commandGenerator.writeRaw(0x00001F1F);						// Texture [1F,1F]
#else
		commandGenerator.writeRaw(0x30AABBCC);
		commandGenerator.writeRaw(0x01100100);

		commandGenerator.writeRaw(0x00FF0000);
		commandGenerator.writeRaw(0x00100180);

		commandGenerator.writeRaw(0x0000FF00);
		commandGenerator.writeRaw(0x01200230);						// [15:0] XCoordinate, [31:16] Y Coordinate

#endif
	}
	break;
	case TEXTURE_PALETTE_BLENDING:
	{
		//commandGenerator.writeRaw(0xn (8)	);
		// DO NOT MAKE THE TRIANGLE TOO LARGE : BUG MOSTLY DISAPPEAR BECAUSE CACHE MISS DONT OCCUR MUCH PER PIXEL... Only edge show the bug.
		// This is the best size to see the problem.
		/*
		commandGenerator.writeRaw(0x2cFFFFFF);
		commandGenerator.writeRaw(0x00800000);	// XY
		commandGenerator.writeRaw(0x10000000);
		commandGenerator.writeRaw(0x00800080);	// XY
		commandGenerator.writeRaw(0x00000020);
		commandGenerator.writeRaw(0x01000000);	// XY
		commandGenerator.writeRaw(0x00002000);
		commandGenerator.writeRaw(0x01000080);	// XY
		commandGenerator.writeRaw(0x00002020);
		*/

		commandGenerator.writeRaw(0x25AABBCC);						// Polygon, 3 pts, opaque, raw texture
		// Vertex 1
		commandGenerator.writeRaw(0x00400000);						// [15:0] XCoordinate, [31:16] Y Coordinate (VERTEX 0)
		commandGenerator.writeRaw(
			((0 | (0<<6))<<16) | 0x0000							// 512,96 Palette, TexUV[0,0]
		);
		// Vertex 2
		commandGenerator.writeRaw(0x00400040);						// [15:0] XCoordinate, [31:16] Y Coordinate (VERTEX 1)
		commandGenerator.writeRaw((
			// 4 BPP !!!!
			(0) | (0<<4) | (0<<5) | (0<<7) | (0<<9) | (0<<11)			// [31:16] Texture at 0(4x64),0 TRUE COLOR
			)<<16                   | 0x001F);						// [15:8,7:0] Texture [1F,0]
																	// Vertex 3
		commandGenerator.writeRaw(0x00800000);						// [15:0] XCoordinate, [31:16] Y Coordinate
		commandGenerator.writeRaw(0x00001F1F);						// Texture [1F,1F]
		break;
	}
	case POLY_FAIL:

#if 0
		/*
		commandGenerator.writeRaw(0xe2000000);
		commandGenerator.writeRaw(0xe3000000);
		commandGenerator.writeRaw(0xe403bd3f);
		commandGenerator.writeRaw(0xe5000000);
		commandGenerator.writeRaw(0xe6000000);
		commandGenerator.writeRaw(0xe100060a);

		commandGenerator.writeRaw(0x2c808080);
		commandGenerator.writeRaw(0x00500132);
		commandGenerator.writeRaw(0x7b0c7000);
		commandGenerator.writeRaw(0x00510132);
		commandGenerator.writeRaw(0x00187f00);
		commandGenerator.writeRaw(0x00510141);
		commandGenerator.writeRaw(0x0913707f);
		*/
		commandGenerator.writeRaw(0xE3000000);
		commandGenerator.writeRaw(0xE4077e7f);

		commandGenerator.writeRaw(0x30ff8080);
		commandGenerator.writeRaw(0x00f00140);
		commandGenerator.writeRaw(0x00200000);
		commandGenerator.writeRaw(0x00f0001e);
		commandGenerator.writeRaw(0x00200000);
		commandGenerator.writeRaw(0x015e0034);

		commandGenerator.writeRaw(0x30ff8080);
		commandGenerator.writeRaw(0x00f00140);
		commandGenerator.writeRaw(0x00200000);
		commandGenerator.writeRaw(0x00810034);
		commandGenerator.writeRaw(0x00200000);
		commandGenerator.writeRaw(0x00f0001e);
#endif
		break;
	case PALETTE_FAIL_LATEST:
		{
		u16* buff16 = (u16*)buffer;
		u16* pFill = &buff16[(1024*480) + 256];

		if (1) {
			// Patch palette.
			*pFill++ = 0x0000;
			*pFill++ = 0x1111;
			*pFill++ = 0x2222;
			*pFill++ = 0x3333;
			*pFill++ = 0x4444;
			*pFill++ = 0x5555;
			*pFill++ = 0x6666;
			*pFill++ = 0x7777;
			*pFill++ = 0x8888;
			*pFill++ = 0x9999;
			*pFill++ = 0xAAAA;
			*pFill++ = 0xBBBB;
			*pFill++ = 0xCCCC;
			*pFill++ = 0xDDDD;
			*pFill++ = 0xEEEE;
			*pFill++ = 0xFFFF;

			for (int y=0; y < 64; y++) {
				pFill = &buff16[(y*1024) + 896];
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
			}
		}

		commandGenerator.writeRaw(0x2c808080);
		commandGenerator.writeRaw(0x017e00c8);
		commandGenerator.writeRaw(0x78100000);
		commandGenerator.writeRaw(0x017e01b8);
		commandGenerator.writeRaw(0x000e00ef);
		commandGenerator.writeRaw(0x01ba00c8);
		commandGenerator.writeRaw(0x00003b00);
		commandGenerator.writeRaw(0x01ba01b8);
		commandGenerator.writeRaw(0x00003bef);
		}

		break;
	case USE_AVOCADO_DATA:
	{
		FILE* binSrc = fopen("W:\\JPSX\\Avocado\\TexTrueColorStarOcean","rb");
		// ----- Read VRAM
		u16* buff16 = (u16*)buffer;
		fread(buffer,sizeof(u16),1024*512,binSrc);

		// ----- Sync Stencil cache and VRAM state.
		for (int y=0; y < 512; y++) {
			for (int x=0; x < 1024; x++) {
				bool bBit    = (buff16[x + (y*1024)] & 0x8000) ? true : false;
				setStencil(mod,x,y,bBit);
			}
		}

		// ---- Setup
		u32 setupCommandCount;
		fread(&setupCommandCount, sizeof(u32), 1, binSrc);
		for (int n=0; n < setupCommandCount; n++) {
			u32 cmdSetup;
			fread(&cmdSetup, sizeof(u32),1, binSrc);
			commandGenerator.writeRaw(cmdSetup);
		}

#if 0
		int removeList[] = { 
			11,12,13,14,15,16,17,18,19,
			20,21,22,23,24,25,26,27,28,29,
			30,31,32,33,34,35,36,37,38,39,
			40,41,42,43,44,45,46,47,48,49,
			50,51,52,53,54,55,56,57,58,59,
			60,61,62,63,64,65,66,
			//	67
		};
#endif

		// ---- Commands itself.
		u32 logCommandCount;
		fread(&logCommandCount, sizeof(u32), 1, binSrc);
		for (int n=0; n < logCommandCount; n++) {
			u32 cmdLength;
			fread(&cmdLength, sizeof(u32),1, binSrc);

			bool removeCommand = false;
#if 0
			for (int s=0; s < sizeof(removeList)/sizeof(int); s++) {
				if (removeList[s] == n) {
					removeCommand = true;
				}
			}
#endif


			printf("Cmd Length : %i\n",cmdLength);
			bool ignoreCommand = false;
			bool isLine = false;
			int  lineCnt = 0;
			bool isMulti = false;
			bool isColored = false;

			for (int m=0; m < cmdLength; m++) {
				u32 operand;
				fread(&operand, sizeof(u32),1, binSrc);
				if (!removeCommand) {

					if (m==0) {
						printf("// LOG COMMAND Number %i [%x] (%i op)\n",n+1,operand>>24,cmdLength);
						if ((operand >> 24) == 0x02) {
							ignoreCommand;
						}
						isLine    = (((operand>>24) & 0xE0) == 0x40);
						isMulti   = (operand>>24) & 0x08;
						isColored = (operand>>24) & 0x10;
						lineCnt = 0;

						if (isLine) {
//							ignoreCommand = true;
						}
					}

					if (!ignoreCommand) {
						commandGenerator.writeRaw(operand);
						if (isLine) {
							printf("commandGenerator.writeRaw(0x%08x);\n",operand);
							/*
								GP0(50h) - Shaded line, opaque
								GP0(52h) - Shaded line, semi-transparent
								GP0(58h) - Shaded Poly-line, opaque
								GP0(5Ah) - Shaded Poly-line, semi-transparent

								  1st   Color1+Command    (CcBbGgRrh)
								  2nd   Vertex1           (YyyyXxxxh)
								  3rd   Color2            (00BbGgRrh)
								  4th   Vertex2           (YyyyXxxxh)
								 (...)  ColorN            (00BbGgRrh) (poly-line only)
								 (...)  VertexN           (YyyyXxxxh) (poly-line only)
								 (Last) Termination Code  (55555555h) (poly-line only)
							*/
							switch (lineCnt)
							{
							case 0:
								if (isMulti && (m!=0) && ((operand & 0x50005000) == 0x50005000)) {
									printf("END LINE MULTI.\n");
									lineCnt = 0;
								} else {
									printf("\nColor : %x\n",operand & 0xFFFFFF);
									lineCnt = 1;
								}
								break;
							case 1:
								if (isMulti && ((operand & 0x50005000) == 0x50005000)) {
									printf("END LINE MULTI.\n");
								} else {
									printf("Vertex : %i,%i\n",((int)(operand & 0xFFFF)<<16)>>16,(((int)(operand))>>16));
									if (isColored) {
										lineCnt = 0;
									} else {
										lineCnt = 1; // As Is
									}
								}
								break;
							case 2: break;
							case 3: break;
							default:
								break;
							}
						} else {
							printf("commandGenerator.writeRaw(0x%08x);\n",operand);
						}
					}
				}
			}

		}

		/*
		commandGenerator.resetBuffer();
		commandGenerator.writeRaw(0xE100000E);
		commandGenerator.writeRaw(0x64808080);
		commandGenerator.writeRaw(0x017a00dc);
		commandGenerator.writeRaw(0x78100000);
		commandGenerator.writeRaw(0x002c00c8);
		*/

		fclose(binSrc);

#if 0
		commandGenerator.resetBuffer();

		// LOG COMMAND Number 1460 [e3] (1 op)
		commandGenerator.writeRaw(0xe3041028);
		// LOG COMMAND Number 1461 [e4] (1 op)
		commandGenerator.writeRaw(0xe404b0ac);
		// LOG COMMAND Number 1462 [e5] (1 op)
		commandGenerator.writeRaw(0xe5082028);
		// LOG COMMAND Number 1463 [e1] (1 op)
		commandGenerator.writeRaw(0xe100065f);
		// LOG COMMAND Number 1464 [e2] (1 op)
		commandGenerator.writeRaw(0xe2000000);
		// LOG COMMAND Number 1465 [e6] (1 op)
		commandGenerator.writeRaw(0xe6000000);
		// LOG COMMAND Number 1466 [e1] (1 op)
		commandGenerator.writeRaw(0xe100021f);
		// LOG COMMAND Number 1467 [e2] (1 op)
		commandGenerator.writeRaw(0xe20e339c);
		// LOG COMMAND Number 1468 [38] (8 op)
		/* Blue rect.
		commandGenerator.writeRaw(0x38b00000);
		commandGenerator.writeRaw(0x00030003);
		commandGenerator.writeRaw(0xe3800000);
		commandGenerator.writeRaw(0x00030082);
		commandGenerator.writeRaw(0xe5500000);
		commandGenerator.writeRaw(0x00260003);
		commandGenerator.writeRaw(0xe2200000);
		commandGenerator.writeRaw(0x00260082);
		*/
		// LOG COMMAND Number 1469 [e1] (1 op)
		commandGenerator.writeRaw(0xe100023f);
		// LOG COMMAND Number 1470 [e2] (1 op)
		commandGenerator.writeRaw(0xe2000000);
		/* Four corners
		// LOG COMMAND Number 1471 [65] (4 op)
		commandGenerator.writeRaw(0x65800000);
		commandGenerator.writeRaw(0x00250081);
		commandGenerator.writeRaw(0x7810e41c);
		commandGenerator.writeRaw(0x00040004);
		// LOG COMMAND Number 1472 [65] (4 op)
		commandGenerator.writeRaw(0x6500023f);
		commandGenerator.writeRaw(0x00250000);
		commandGenerator.writeRaw(0x7810e410);
		commandGenerator.writeRaw(0x00040004);
		// LOG COMMAND Number 1473 [65] (4 op)
		commandGenerator.writeRaw(0x65041028);
		commandGenerator.writeRaw(0x00000081);
		commandGenerator.writeRaw(0x7810e80c);
		commandGenerator.writeRaw(0x00040004);
		// LOG COMMAND Number 1474 [65] (4 op)
		commandGenerator.writeRaw(0x65260082);
		commandGenerator.writeRaw(0x00000000);
		commandGenerator.writeRaw(0x7810e800);
		commandGenerator.writeRaw(0x00040004);
		*/
		// LOG COMMAND Number 1475 [e1] (1 op)
		commandGenerator.writeRaw(0xe100023f);
		// LOG COMMAND Number 1476 [e2] (1 op)
		commandGenerator.writeRaw(0xe20f03de);
		/*
		// LOG COMMAND Number 1477 [65] (4 op)
		commandGenerator.writeRaw(0x6510e800);
		commandGenerator.writeRaw(0x00040000);
		commandGenerator.writeRaw(0x78100000);
		commandGenerator.writeRaw(0x00210004);
		*/
		// LOG COMMAND Number 1478 [e1] (1 op)
		commandGenerator.writeRaw(0xe100023f);
		// LOG COMMAND Number 1479 [e2] (1 op)
		commandGenerator.writeRaw(0xe20f0bde);
		/*
		// LOG COMMAND Number 1480 [65] (4 op)
		commandGenerator.writeRaw(0x65100000);
		commandGenerator.writeRaw(0x00040081);
		commandGenerator.writeRaw(0x7810000c);
		commandGenerator.writeRaw(0x00210004);
		*/

		// LOG COMMAND Number 1481 [e1] (1 op)
		commandGenerator.writeRaw(0xe100023f);
		// LOG COMMAND Number 1482 [e2] (1 op)
		commandGenerator.writeRaw(0xe20e8bde);
		// LOG COMMAND Number 1483 [65] (4 op)
		commandGenerator.writeRaw(0x6510000c);
		commandGenerator.writeRaw(0x00250004);
		commandGenerator.writeRaw(0x78100c00);
		commandGenerator.writeRaw(0x0004007d);
#endif

#if 0
		commandGenerator.resetBuffer();

		u16* pFill = &buff16[(1024*480) + 256];

		if (0) {
			// Patch palette.
			*pFill++ = 0x0000;
			*pFill++ = 0x1111;
			*pFill++ = 0x2222;
			*pFill++ = 0x3333;
			*pFill++ = 0x4444;
			*pFill++ = 0x5555;
			*pFill++ = 0x6666;
			*pFill++ = 0x7777;
			*pFill++ = 0x8888;
			*pFill++ = 0x9999;
			*pFill++ = 0xAAAA;
			*pFill++ = 0xBBBB;
			*pFill++ = 0xCCCC;
			*pFill++ = 0xDDDD;
			*pFill++ = 0xEEEE;
			*pFill++ = 0xFFFF;

			for (int y=0; y < 64; y++) {
				pFill = &buff16[(y*1024) + 896];
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
				// 16 pix
				*pFill++ = 0x3210;
				*pFill++ = 0x7654;
				*pFill++ = 0xBA98;
				*pFill++ = 0xFEDC;
			}
		}

		/*
		commandGenerator.writeRaw(0x2c808080);
		commandGenerator.writeRaw(0x01670154);
		commandGenerator.writeRaw(0x78140000);
		commandGenerator.writeRaw(0x0167016c);
		commandGenerator.writeRaw(0x000f0017);
		commandGenerator.writeRaw(0x01730154);
		commandGenerator.writeRaw(0x00000b00);
		commandGenerator.writeRaw(0x0173016c);
		commandGenerator.writeRaw(0x00000b17);
		*/
		commandGenerator.writeRaw(0x2c808080);
		commandGenerator.writeRaw(0x017e00c8);
		commandGenerator.writeRaw(0x78100000);
		commandGenerator.writeRaw(0x017e01b8);
		commandGenerator.writeRaw(0x000e00ef);
		commandGenerator.writeRaw(0x01ba00c8);
		commandGenerator.writeRaw(0x00003b00);
		commandGenerator.writeRaw(0x01ba01b8);
		commandGenerator.writeRaw(0x00003bef);
		/*
		commandGenerator.writeRaw(0xE1000468);
		commandGenerator.writeRaw(0x64808080);
		commandGenerator.writeRaw(0x007e00c8);	// X,Y dest
		commandGenerator.writeRaw(0x78100000);	// CLUT
		commandGenerator.writeRaw(0x008000F0);
		*/

		/*
		commandGenerator.writeRaw(0x2c808080);
		commandGenerator.writeRaw(0x003800c8);
		commandGenerator.writeRaw(0x780c0000);
		commandGenerator.writeRaw(0x003801b8);
		commandGenerator.writeRaw(0x000d00ef);
		commandGenerator.writeRaw(0x006800c8);
		commandGenerator.writeRaw(0x00002f00);
		commandGenerator.writeRaw(0x006801b8);
		commandGenerator.writeRaw(0x00002fef);
		*/
#endif

	}
	break;
	case COPY_TORAM:
		{
			u8* target = buffer;
			// Transform each pixel RGB888 into a single bit.
			for (int py=0; py < 512; py++) {
				for (int px=0; px < 1024; px++) {
					int baseDst = ((px+py*1024)*2);
					target[baseDst  ]  = px;
					target[baseDst+1]  = py;
				}
			}
		}

		commandGenerator.writeRaw(0xC0000000);
		commandGenerator.writeRaw((0)|(0<<16));
		commandGenerator.writeRaw((2<<16) | 4); // W=4,H=2
	break;
	case COPY_CMD:
	{
		commandGenerator.writeRaw(0x80000000);
		commandGenerator.writeRaw((256)|(256<<16));
		commandGenerator.writeRaw(( 64)|( 64<<16));
		commandGenerator.writeRaw((16<<16) | 16); // W=16,H=16
#if 0
		int from =  3;
//		int to   = 12;
//		int l    =  31;
/*
		commandGenerator.writeRaw(0x80000000);
		commandGenerator.writeRaw(0x00000010);		// X=16,Y=0 SRC
		commandGenerator.writeRaw((   0)|(1<<16));	// X=0 ,Y=1 DST
		commandGenerator.writeRaw(0x00010000 | 17); // W=17,H=1
		*/

//		for (int from = 16; from < 32; from++) {

		int y = 50;
		// SrcX = 0..15
		// DstX = 0..15
		// Src > Dst and Src < Dst
//		for (int l = 1; l < 2; l++) {
		int l = 150;
		int srcX = 128;
//			for (int srcX=128; srcX < (128+16); srcX++) 
			//int x = 0;
			{

				int dstX = 128;
//				for (int dstX=(10); dstX < 400; dstX++) 
				{
					srcX = dstX;
					int v = 0;
//					for (v = 0; v < 8; v++)
					{
						commandGenerator.writeRaw(0x80000000);
						commandGenerator.writeRaw(srcX);
						commandGenerator.writeRaw((dstX+(v)*60) | (y<<16)); // Y Position
						commandGenerator.writeRaw((0x0001<<16) | l);
					}
					y++;
				}
			}
//		}
		/*
		commandGenerator.writeRaw(              from);
		commandGenerator.writeRaw((128 << 16) | (to));	// Y,X Dest
		commandGenerator.writeRaw((  1 << 16) |  (l)); // Height, Width
		test(from, to, l, adrStorage, &adrStorageCount);
		*/
#endif
	}
	break;
	}
	
	commandGenerator.writeRaw(0); // NOP
#endif

#if 0
	// FILL TEST
	commandGenerator.writeRaw(0x028000FF); // Red + Half Blue
	commandGenerator.writeRaw(0x00000000); // 0,0
	commandGenerator.writeRaw(0x00100010); // 16,16
#endif

#if 0
	// RECT FILL TEST
	commandGenerator.writeRaw(0x6000FF00); // Green, Rect (Variable Size)
	commandGenerator.writeRaw(0x00080008); // [8,8]
	commandGenerator.writeRaw(0x00100010); // 16x16
#endif

#if 0
	// GENERIC POLYGON FILL
	commandGenerator.writeRaw(0x380000b2);
	commandGenerator.writeRaw((192<<0) | (240<<16));
	commandGenerator.writeRaw(0x00008cb2);
	commandGenerator.writeRaw((320<<0) | (112<<16));
	commandGenerator.writeRaw(0x00008cb2);
	commandGenerator.writeRaw((320<<0) | (368<<16));
	commandGenerator.writeRaw(0x000000b2);
	commandGenerator.writeRaw((448<<0) | (240<<16));
#endif

#if 0
// GP0(40h) - Monochrome line, opaque
// GP0(42h) - Monochrome line, semi-transparent
	commandGenerator.writeRaw(0x4000FFFF);
	commandGenerator.writeRaw(0x00000000);
	commandGenerator.writeRaw(0x00100010);

	commandGenerator.writeRaw(0x40FF0000);
	commandGenerator.writeRaw(0x00000000);
	commandGenerator.writeRaw(0x00400010);
#endif

#if 0
	// Test CPU->VRAM
	loadImageToVRAMAsCommand(commandGenerator,"Gradient128x64.png",0,0,false);
#endif

#if 0
	//
	// TEXTURED POLYGON
	//
	loadImageToVRAM(mod,"Gradient128x64.png",buffer,0,0,false);
	commandGenerator.writeRaw(0x24FFFFFF);						// Polygon, 3 pts, opaque, raw texture
	// Vertex 1
	commandGenerator.writeRaw(0x01100100);						// [15:0] XCoordinate, [31:16] Y Coordinate (VERTEX 0)
	commandGenerator.writeRaw(0xFFF30000);						// [31:16]Color LUT : NONE, value ignored
																// [15:8,7:0] Texture [0,0]
	// Vertex 2
	commandGenerator.writeRaw(0x00100180);						// [15:0] XCoordinate, [31:16] Y Coordinate (VERTEX 1)
	commandGenerator.writeRaw(((
		0 | (0<<4) | (0<<5) | (2<<7) | (0<<9) | (0<<11)			// [31:16] Texture at 0(4x64),0 ******4BPP******
		)<<16 )                  | 0x001F);						// [15:8,7:0] Texture [1F,0]
																// Vertex 3
	commandGenerator.writeRaw(0x01200230);						// [15:0] XCoordinate, [31:16] Y Coordinate
	commandGenerator.writeRaw(0x00001F1F);						// Texture [1F,1F]
#endif

#if 0
	//commandGenerator.writeRaw(0xE100000F);

	
#endif

	// prepairListOfGPUCommands(0); // Simple Polygon, no texture, no alpha blending.
	// prepairListOfGPUCommands(1);	// Simple Polygon, true color texture, no alpha blending.
	// prepairListOfGPUCommands(2);	// Simple Polygon, 4 bit texture, no alpha blending.

	// ------------------------------------------------------------------
	// MAIN LOOP
	// ------------------------------------------------------------------
	int clockCnt  = 0;

	int SrcY = 17;
	int DstY = 57;

	// Test pattern
	bool performTest = true;


	int XSrc = 16;
	int XDst = 17;
	int W    = 1;
	int mode = 0;

#if 0
	for (mode = 0; mode < 4; mode++) 
	{
		for (XSrc = 16; XSrc <= 31; XSrc++) 
		{
			for (XDst = 0; XDst <= 47; XDst++) 
			{
				printf("Mode:%i SrcX:%i DstX:%i W:1..64\n",mode, XSrc,XDst);
				for (W = 1; W <= 64; W++) 
				{

				// Rollback and clean everything...
				memcpy(buffer    ,refBuffer,1024*1024);
				memcpy(softbuffer,refBuffer,1024*1024);
				backupToStencil(mod,refStencil);

				if (verifyStencilVRAM(mod, (u16*)softbuffer)) {
					printf("STENCIL$ DIFFERENCE");
					while (1) { printf(""); };
				}


				commandGenerator.writeRaw(0xE6000000 | mode);						// Set Bit mask, no check.

				// Prepare HW Command
				commandGenerator.writeRaw(0x80000000);
				commandGenerator.writeRaw((SrcY<<16) + XSrc);
				commandGenerator.writeRaw((DstY<<16) + XDst);
				commandGenerator.writeRaw(0x00010000 | W); 

				bool isChecking = mode & 2 ? true : false;
				bool isForcing  = mode & 1 ? true : false;


				// Execute SW Command
				softVVCopy(XSrc,SrcY,XDst,DstY,W,1,(u16*)softbuffer, isChecking, isForcing);
#endif


	int waitCount = 0;

	u8* bufferRGBA = new u8[1024*512*4];
	buffer32Bit = bufferRGBA;
	struct mfb_window *window = mfb_open_ex("my display", 1024, 512, WF_RESIZABLE);
	if (!window)
		return 0;
	mfb_set_viewport(window,0,0,1024,512);

	int stuckState = 0;
	int prevCommandParseState = -1;
	int prevCommandWorkState  = -1;
	int currentCommandID      =  0;

	bool NoHW = false;

	memcpy(swBuffer, buffer, 1024*1024);
	u32 commandCount;
	u32* p = commandGenerator.getRawCommands(commandCount);
	if (NoHW) {
		commandDecoder(p,commandCount,window);
	}

	int state;
	while (NoHW) {
		state = mfb_update(window,bufferRGBA);

		if (state < 0)
			break;
	}

	bool log
#ifdef RELEASE
		= false;
#else
		= true;
#endif

	while ((waitCount < 20)					// If GPU stay in default command wait mode for more than 20 cycle, we stop simulation...
		&& (stuckState < 2500)
//		&& (clockCnt < 150000)
	)
	{
		// By default consider stuck...
		stuckState++;

		if (log) {
			// If some work is done, reset stuckState.
			if (mod->gpu__DOT__currState     != prevCommandParseState) { 
				VCMember* pCurrState = pScan->findMemberFullPath("gpu.currState");
				printf("NEW STATE : %s\n", pCurrState->getEnum()[mod->gpu__DOT__currState].outputString);
	//			printf("NEW STATE : %i\n", mod->gpu__DOT__currState);
				stuckState = 0; prevCommandParseState = mod->gpu__DOT__currState; 
				if (prevCommandParseState == 1) {	// switched to LOAD_COMMAND
					printf("\t[%i] COMMAND : %x\n",currentCommandID,mod->gpu__DOT__command);
					currentCommandID++;				// Increment current command ID.
				}
			}
			if (mod->gpu__DOT__currWorkState != prevCommandWorkState)  {
				VCMember* pCurrWorkState = pScan->findMemberFullPath("gpu.currWorkState");
				printf("\tNEW WORK STATE : %s\n",pCurrWorkState->getEnum()[mod->gpu__DOT__currWorkState].outputString);
	//			printf("\t[%i] NEW WORK STATE : %i\n",currentCommandID-1,mod->gpu__DOT__currWorkState);
				/*stuckState = 0;*/ prevCommandWorkState = mod->gpu__DOT__currWorkState;  
			}
		
			//
			// Update window every 2048 cycle.
			//
			if ((clockCnt & 0xFFFF) == 0) {

				Convert16To32(buffer, bufferRGBA);
				state = mfb_update(window,bufferRGBA);

				if (state < 0)
					break;
			}
		}

		if (mod->gpu__DOT__currState == 0) {
			waitCount++; 
		} else {
			waitCount = 0; 
		}

		mod->clk    = 0;
		mod->eval();

		// Generate VCD if needed
		if (useScan) {
			if (!useScanRange || (useScanRange && ((clockCnt>>1) >= scanStartCycle) && ((clockCnt>>1) <= scanEndCycle))) {
				pScan->eval(clockCnt);
			}
		}
		clockCnt++;

		//-------------------------------------------------------------------------------------
		// SIMULATED VRAM READ/WRITE
		// Very basic and stupid protocol :
		// Cycle 1. REQ is detected -> We set ACK, and get the number of BURST we want to read.
		// Cycle 2. Read 4 byte until counter reach 0.
		//			Set ACK to ZERO when reach last element.
		//-------------------------------------------------------------------------------------
		/*
		static int cnt = 0;
		int workAdr;
		static bool firstRW = false;
		static int  currentItemCheckCount = 0;
		static bool stop = false;
		static bool waitComplete = false;
		static int from = sFrom;
		static int to = sTo;
		static int l = sL;
		*/
//		if (waitComplete) {
			/*
			if (mod->gpu__DOT__currWorkState == 0) {
				waitComplete = false;

				printf("%i.%i.%i\n", from, to, l);

				// Launch new test...
				test(from, to, l, adrStorage, &adrStorageCount);
				commandGenerator.writeRaw(0x80000000);
				commandGenerator.writeRaw(from);
				commandGenerator.writeRaw(to				// X Dest
					| (128 << 16));		// Y Dest
				commandGenerator.writeRaw(l			// Width
					| (1 << 16));			// Height

				l++;
				if (l == 1025) {
					l = 1;
					from++;
					if (from == 1024) {
						from = 0;
						to++;
						if (to == 1024) {
							stop = true;
						}
					}
				}

				currentItemCheckCount = 0;
			}
			*/
//		}

		static int busyCounter = 0;
		static bool isRead = false;
		static int readAdr = 0;
		static int readSize= 0;

		if (busyCounter) {
			busyCounter--;
			if (busyCounter == 1) {
				mod->i_dataInValid = 1;

				// Clear
				for (int n=0; n < 8; n++) { mod->i_dataIn[n] = 0; }

				for (int n=0; n < readSize; n++) {
					mod->i_dataIn[n] = buffer[readAdr  ] | (buffer[readAdr+1]<<8) | (buffer[readAdr+2]<<16) | (buffer[readAdr+3]<<24);
					readAdr += 4;
				}
			}

			if (busyCounter == 0) {
				mod->i_busy = 0;
				mod->i_dataInValid = 0;
				for (int n=0; n < 8; n++) {
					mod->i_dataIn[n] = 0; // Clean just for cleaning purpose
				}
			}

			mod->eval();
		}

		// Request
		if (mod->o_command == 1 && (mod->i_busy == 0)) {
			// Can do busy stuff if needed...
			int msk;
			int baseAdr;
			int sizeParam;

			switch (mod->o_commandSize) {
			case 0:	// 8 Byte
				msk = mod->o_writeMask & 0xF;
				baseAdr = (mod->o_adr<<5) + (mod->o_subadr<<2); // 8 byte
				sizeParam = 2;
				break;
			case 1: // 32 Byte
				msk		= mod->o_writeMask;
				baseAdr = mod->o_adr<<5; // 32 byte
				sizeParam = 8;
				break;
			case 2: // 4 Byte
				msk = mod->o_writeMask & 0x3;
				baseAdr = (mod->o_adr<<5) + (mod->o_subadr<<2); // 4 byte
				sizeParam = 1;
				break;
			}

			if (mod->o_write) {
				int selPix = mod->o_writeMask;
				for (int n=0; n < sizeParam; n++) {
					if (selPix &  1) { buffer[baseAdr  ] =  mod->o_dataOut[n]      & 0xFF; }
					if (selPix &  1) { buffer[baseAdr+1] = (mod->o_dataOut[n]>> 8) & 0xFF; }
					if (selPix &  2) { buffer[baseAdr+2] = (mod->o_dataOut[n]>>16) & 0xFF; }
					if (selPix &  2) { buffer[baseAdr+3] = (mod->o_dataOut[n]>>24) & 0xFF; }
					baseAdr += 4;
					selPix >>= 2;
				}

				busyCounter = 0;
			} else {
				// Read command... gives back result in 3 cycles...
				isRead = true;
				mod->i_busy = 1;
				readAdr   = baseAdr;
				readSize  = sizeParam;
				busyCounter = 4;
			}
		}

		mod->clk    = 1;
		mod->eval();

		// -----------------------------------------
		//   [REGISTER SETUP OF GPU FROM BUS]
		// -----------------------------------------
		mod->write		= 0;
		mod->gpuSel		= 0;
		mod->gpuAdrA2	= 0;
		mod->write		= 0;
		mod->cpuDataIn	= 0;

		// Cheat... should read system register like any normal CPU...
		if (!mod->dbg_canWrite) {
			// Push command inside the GPU as long as the GPU FIFO are not full
			// and as long as we have command to push...
			if (commandGenerator.stillHasCommand()) {
				mod->gpuSel		= 1;
				mod->gpuAdrA2	= 0;
				mod->write		= 1;
				mod->cpuDataIn	= commandGenerator.getRawCommand();
				// printf("Send Command : %i\n",mod->cpuDataIn);
			}
		}

		if (useScan) {
			if (!useScanRange || (useScanRange && ((clockCnt>>1) >= scanStartCycle) && ((clockCnt>>1) <= scanEndCycle))) {
				pScan->eval(clockCnt);
			}
		}

		// ----
		// PNG SCREEN SHOT PER CYCLE IF NEEDED.
		// ----
		clockCnt++;
		if ((clockCnt>>1) % screenShotmoduloSpeed == 0 && useScreenShot) {
//			if ((mod->mydebugCnt >= startRange) && (mod->mydebugCnt <= endRange)) {
				static int frameCount = 0;
				frameCount = mod->mydebugCnt;
				char strBuf[100];
				sprintf_s(strBuf,100,"movie/output%i.png",frameCount);
				char strBuf2[100];
				sprintf_s(strBuf2,100,"movie/output%i_msk.png",frameCount);
				dumpFrame(mod, strBuf,strBuf2,buffer,mod->mydebugCnt, useMaskDump);
				frameCount++;
//			}
		}
	}

	//
	// End Test, check buffer
	//
#if 0
	// 1. Verify coherency Stencil vs VRAM.

	// 2. Verify coherency VRAM vs Soft VRAM.
	if (performTest && mymemcmp(softbuffer,buffer,1024*1024)!=0) {
		printf(" DIFFERENCE !");
		dumpFrame(mod, "soft.png", "output_msk.png",softbuffer,clockCnt>>1, true);
		dumpFrame(mod, "hard.png", "output_msk.png",buffer    ,clockCnt>>1, true);
//		while (1) { printf(""); };
	}

	if (verifyStencilVRAM(mod, (u16*)softbuffer)) {
		printf("STENCIL$ DIFFERENCE");
//		while (1) { printf(""); };
	}

				}
			}
		}
	}
#endif

	 mfb_close(window);

	//
	// ALWAYS WRITE A FRAME AT THE END : BOTH STENCIL AND VRAM.
	//
	dumpFrame(mod, "output.png", "output_msk.png",buffer,clockCnt>>1, true);

	delete [] buffer;
	delete [] refBuffer;
	pScan->shutdown();
}

typedef unsigned char u8;
typedef unsigned int  u32;
typedef unsigned short u16;

static u32* GP0 = (u32*)0x1F801810;
#ifdef _WIN32
u16* vrambuffer;

void Convert16To32(u8* buffer, u8* data) {
	for (int y=0; y < 512; y++) {
		for (int x=0; x < 1024; x++) {
			int adr = (x*2 + y*2048);
			int lsb = buffer[adr];
			int msb = buffer[adr+1];
			int c16 = lsb | (msb<<8);
			int r   = (     c16  & 0x1F);
			int g   = ((c16>>5)  & 0x1F);
			int b   = ((c16>>10) & 0x1F);
			r = (r >> 2) | (r << 3);
			g = (g >> 2) | (g << 3);
			b = (b >> 2) | (b << 3);
			int base = (x + y*1024)*4;
			data[base  ] = b;
			data[base+1] = g;
			data[base+2] = r;
			data[base+3] = 255;
		}
	}
}

void dumpVRAM(const char* name, u16* buffer) {
	unsigned char* data = new unsigned char[1024 * 4 * 512];

	for (int y = 0; y < 512; y++) {
		for (int x = 0; x < 1024; x++) {
			int adr = (x+ (y*1024));
			int c16 = buffer[adr];
			int r = (c16 & 0x1F);
			int g = ((c16 >> 5) & 0x1F);
			int b = ((c16 >> 10) & 0x1F); // 
			r = (r >> 2) | (r << 3);
			g = (g >> 2) | (g << 3);
			b = (b >> 2) | (b << 3);
			int base = (x + y * 1024) * 3;
			data[base] = r;
			data[base + 1] = g;
			data[base + 2] = b;
		}
	}

	int err = stbi_write_png(name, 1024, 512, 3, data, 1024 * 3);
	delete[] data;
}

#endif

/* Create a group of unique pixels */
void fillTexture(u16* buff64k) {
	for (int n = 0; n < 65536; n++) {
		buff64k[n] = n;
	}
}

void uploadToGPU(u16* buff512_128, u16 offX) {
#ifdef _WIN32
	for (int y = 0; y < 512; y++) {
		for (int x = 0; x < 128; x++) {
			vrambuffer[x + offX + (y * 1024)] = buff512_128[x + (y * 128)];
		}
	}
#else
	*GP0 = 0xA0000000;
	*GP0 = 0x00000000 | offX;
	*GP0 = 0x02000080; // 128x512

	u32* p32 = (u32*)buff512_128;
	for (int n = 0; n < 32768; n++) { // 32768 x 2 pixels
		*GP0 = *p32++;
	}
#endif
}

// Copy VRAM from-to
void vramCmd(u16 sx, u16 sy, u16 dx, u16 dy, u16 w, u16 h) {
#ifdef _WIN32
	printf("%i,%i->%i,%i (%i,%i) (V[%i,%i]\n", sx, sy, dx, dy, w, h, (int)dx - (int)sx, (int)dy - (int)sy);
	bool forceY = false;
	int srcY	= ((sy > dy) | forceY) ? sy : (sy + h - 1);
	int dstY    = ((sy > dy) | forceY) ? dy : (dy + h - 1);
	int dirY	= ((sy > dy) | forceY) ? +1 : -1;

	while (h != 0) {
		int tw = w;

		int srcX = (sx > dx) ? sx : (sx + w - 1);
		int dstX = (sx > dx) ? dx : (dx + w - 1);
		int dirX = (sx > dx) ? +1 : -1;

		while (tw != 0) {
// DEBUG : Show parts read and write during VRAM copy.
//			vrambuffer[(srcY * 1024) + srcX] = 0xFFFF;
//			vrambuffer[(dstY * 1024) + dstX] = 0xAAAA;
			vrambuffer[(dstY * 1024) + dstX] = vrambuffer[(srcY * 1024) + srcX];

			tw--;
			srcX += dirX;
			dstX += dirX;
		}
		h--;
		srcY += dirY;
		dstY += dirY;
	}
#else
	*GP0 = 0x80000000;
	*GP0 = ((sy << 16) | sx);
	*GP0 = ((dy << 16) | dx);
	*GP0 = ((h << 16) | w);
#endif
}

int main2()
{
	// ------------------------------------------------------------------
	// Fake VRAM PSX
	// ------------------------------------------------------------------
#ifdef _WIN32
	vrambuffer = new u16[1024 * 512];
	memset(vrambuffer, 0, 1024 * 1024);
#endif

	// TODO : Get 512x128x16 bit pixel RAM buffer
	u16 cpuBuffer[65536];

	// Create pattern and upload
	fillTexture(cpuBuffer);
	uploadToGPU(cpuBuffer, 0);
	uploadToGPU(cpuBuffer, 128);
	uploadToGPU(cpuBuffer, 256);
	uploadToGPU(cpuBuffer, 384);
	uploadToGPU(cpuBuffer, 512);

    /* Test VRAM<->VRAM
    [A] Odd/Even X pos for both SOURCE AND DEST variation.
	[B] 4 Y Pattern : SX < DX, SX >= DX
                      SY < DY, SY >= SY
    [C] Special case of size 0 (width and height)
    */

	// 
	if (1) {
		for (int c = 0; c < 4; c++) {
			// [A] Odd/Even for Source and Dest X
			u16 oddS = c & 1;
			u16 oddD = (c >> 1) & 1;

			// Width from 0..15
			for (int w = 1; w < 16; w++) {
				u16 sx = 10 + oddS + (c * 40);
				u16 dx = 10 + oddD + (c * 40) + 2;

				// Height from 0..3
				for (int h = 1; h < 4; h++) {
					s16 sy = 10 + ((h) * 6) + (w * 30);

					// [B] Direction of Y copy.
					s16 dy = sy + 1;    // Copy After Y
					s16 dy2 = sy - 1;    // Copy Before Y

					vramCmd(sx, sy, dx, dy, w, h);
					vramCmd(sx + 20, sy, dx + 20, dy2, w, h);
				}
			}
		}
	}

#ifdef _WIN32
	// Windows : Dump PNG
	dumpVRAM("output.png", vrambuffer);
#else
	// TODO : Capture VRAM PSX
#endif

	main3();

	return 0;
}

void main3() {
	// Load PNG
	int w, h, n;
	unsigned char* src = stbi_load("output.png", &w, &h, &n, 0);
	
	for (int y = 0; y < 512; y++) {
		for (int x = 0; x < 256; x++) {
			int bx = x >> 7;
			int by = y >> 8;

			int bas = (x + (y * 1024)) * 3;
			u8 r = src[bas];
			u8 g = src[bas+1];
			u8 b = src[bas+2];
			
			int px = ((r >> 3) + (((g>>3)&3)*32)) + (bx*128);
			int py = (g>>5)    + ((b >> 3)*8)     + (by * 256);

			if ((x != px) || (y != py)) {
				int dx = px - x;
				int dy = py - y;
				printf("%i,%i -> V(%i,%i)\n", x, y, dx, dy);
			} else {
				src[bas] = 0x20;
				src[bas+1] = 0x20;
				src[bas+2] = 0x20;
			}
			// printf("RGB -> %i,%i\n", px,py);
		}
	}

	int err = stbi_write_png("output_diff.png", 1024, 512, 3, src, 1024 * 3);

	delete[] src;
}

struct VertexRdr {
	VertexRdr() {}
	VertexRdr(int x_, int y_):x(x_),y(y_) {}

	u8 r;
	u8 g;
	u8 b;
	u16 u;	// Trick : can loop through texture. Bigger range for interpolation...
	u16 v;
	s16 x;
	s16 y;
};

struct GPURdrCtx {
	// E1
	u16 pageX4;
	u16 pageY1;
	u8 semiTransp2;
	u8 textFormat2;

	bool dither;
	bool displayAreaEnable;
	bool disableTexture;
	bool texXFlip;
	bool texYFlip;

	// E2
	u8 texMaskX5;
	u8 texMaskY5;
	u8 texOffsX5;
	u8 texOffsY5;

	// E3
	u16 drAreaX0_10;
	u16 drAreaY0_9;

	// E4
	u16 drAreaX1_10;
	u16 drAreaY1_9;

	// E5
	s16 offsetX_s11;
	s16 offsetY_s11;

	// E6
	bool forceMask;
	bool checkMask;

	//
	bool rtUseSemiTransp;
	bool rtIsTextured;
	bool rtIsTexModRGB;
	bool rtIsPerVtx;
	u16  rtClutX;
	u16  rtClutY;
};



u16 ConvertRGBTo555(u8 r8,u8 g8,u8 b8) {
	return (r8 >> 3) | ((g8 >> 3) << 5) | ((b8 >> 3) << 10);
}

void Convert555ToRGB(u16 rgb555, int& rN, int& gN, int& bN) {
	int rT = (rgb555 & 0x1F);
	int gT = ((rgb555>>5) & 0x1F);
	int bT = ((rgb555>>10) & 0x1F);
	rN = ((rT<<3) | (rT >> 2)) + (rT>>4);
	gN = ((gT<<3) | (gT >> 2)) + (gT>>4);
	bN = ((bT<<3) | (bT >> 2)) + (bT>>4);
}

GPURdrCtx psxGpuCtx;
	VertexRdr vtx[4];

void FillRect(VertexRdr& posAndCol, int width, int height, struct mfb_window *window) {
	u16 fillV = ConvertRGBTo555(posAndCol.r,posAndCol.g,posAndCol.b);
	int x = posAndCol.x;
	int y = posAndCol.y;

	// Support weird fill loop too.

	for (int sy=y; sy < y + height; sy++) {
		int ly = (sy & 0x1FF)*1024;
		for (int sx=x; sx < x + width; sx++) {
			int idx = (sx & 0x3FF) + ly;
			swBuffer[idx] = fillV;
		}

		Convert16To32((u8*)swBuffer, buffer32Bit);
		/*state = */mfb_update(window,buffer32Bit);
	}
}

void CPUToVRAM(VertexRdr& posAndCol, int width, int height, u32** pPixels, struct mfb_window *window) {
	int x = posAndCol.x;
	int y = posAndCol.y;
	int pos = 0;

	u32* pixels = *pPixels;
	// Support weird fill loop too.

	for (int sy=y; sy < y + height; sy++) {
		int ly = (sy & 0x1FF)*1024;
		for (int sx=x; sx < x + width; sx++) {
			int idx = (sx & 0x3FF) + ly;

			u16 pixV;
			if (pos & 1) {
				pixV = (*pixels) >> 16;
			} else {
				pixV = *pixels;
			}

			swBuffer[idx] = pixV;

			if (pos & 1) {
				pixels++;
			}
			pos++;
		}

		Convert16To32((u8*)swBuffer, buffer32Bit);
		/*state = */mfb_update(window,buffer32Bit);
	}

	if (pos & 1) { pixels++; }

	*pPixels = pixels;

}

void RenderTriangle(int v0Idx, int v1Idx, int v2Idx, struct mfb_window *window);
void drawTriangle(int v0Idx, int v1Idx, int v2Idx, struct mfb_window *window);

bool logAll;
void commandDecoder(u32* pStream, u32 size, struct mfb_window *window) {
	u32* pStreamE = &pStream[size];

	psxGpuCtx.drAreaX0_10 = 0;
	psxGpuCtx.drAreaY0_9  = 0;
	psxGpuCtx.drAreaX1_10 = 1024;
	psxGpuCtx.drAreaY1_9  = 512;

	logAll = false;

	/*


	for (int n=0; n < 3; n++) {
		vtx[n].r = (n==0) ? 255 : 0;
		vtx[n].g = (n==1) ? 255 : 0;
		vtx[n].b = (n==2) ? 255 : 0;
		vtx[n].u = 0;
		vtx[n].v = 0;
	}

	vtx[0].x =   0; vtx[0].y =  0;
	vtx[1].x = 100; vtx[1].y =  0;
	vtx[2].x = 100; vtx[2].y = 45;

	drawTriangle(0, 1, 2, window); //  CW

	vtx[0].x = 200; vtx[0].y =  0;
	vtx[1].x = 300; vtx[1].y =  0;
	vtx[2].x = 300; vtx[2].y = 45;

	drawTriangle(0, 2, 1, window); // CCW

	vtx[0].x = 400; vtx[0].y =  0;
	vtx[1].x = 500; vtx[1].y = 45;
	vtx[2].x = 400; vtx[2].y = 45;

	drawTriangle(0, 1, 2, window); //  CW

	vtx[0].x = 600; vtx[0].y =  0;
	vtx[1].x = 700; vtx[1].y = 45;
	vtx[2].x = 600; vtx[2].y = 45;

	drawTriangle(0, 2, 1, window); // CCW

	while (true) {
		int		state = mfb_update(window,swBuffer);

		if (state < 0)
			break;
	}
	*/
	int commandID = 0;

nextCommand:

	// 1.
	u32 command = *pStream++;

	enum EPrimitives {
		SPECIAL_CMD		= 0,
		PRIM_TRI		= 1,
		PRIM_LINE		= 2,
		PRIM_RECT		= 3,
		CP_VRAM_VRAM	= 4,
		CP_CPU_VRAM		= 5,
		CP_VRAM_CPU		= 6,
		SPECIAL_SETTINGS= 7,
	};

	bool	isMultiCmd		= (command >> 27) & 1; // Quad or polyline...
	bool	isPerVtxCol		= (command >> 28) & 1; // Gouraud... But we interpolated always.
	EPrimitives primitive	= (EPrimitives)((command >> 29) & 7);

	bool	isSizedPrimitive=false;
	bool	isFirstVertex   =true;
	int		isHardCodedSize =0;
	int		width,height = 0;
	int		vtxCount     = 0;
	int		vtxCountMax	= 0;
	u32		operand			= command;

	// ---------- For rendering ------------------
	psxGpuCtx.rtIsTexModRGB	  = !((command >> 24) & 1);	// Textured Tri or Rect only. 
	psxGpuCtx.rtIsTextured	  =   (command >> 26) & 1;
	psxGpuCtx.rtUseSemiTransp =   (command >> 25) & 1;
	psxGpuCtx.rtIsPerVtx      =  isPerVtxCol;
//	u16    Clut;
//	u16    PageTex;
//	u8		ClutX;
	// -------------------------------------------

	bool continueLoop = true;

	printf("[%i] EXECUTE :%x\n",commandID, command>>24);

	commandID++;

	switch (primitive) {
	case SPECIAL_CMD:
		// Nop, unssupported, FillRect, IRQ, ...
		switch (command>>24) {
		case 0:
			continueLoop = false; // Nop
			break;
		case 2:
			// Fill command.
			isSizedPrimitive = true;
			break;

		}
		psxGpuCtx.rtIsTextured		= false;
		break;
	case PRIM_TRI:
		vtxCountMax		= isMultiCmd ? 4 : 3;
		break;
	case PRIM_LINE:
		// TODO vtxCount.
		psxGpuCtx.rtIsTextured		= false;
		break;
	case PRIM_RECT:
		isSizedPrimitive	= true;
		isHardCodedSize		= ((command >> 27) & 0x3); // (0=Var, 1=1x1, 2=8x8, 3=16x16)
		break;
	case CP_VRAM_VRAM:
		isSizedPrimitive = true;
		isPerVtxCol		= false;
		isFirstVertex	= false;
		psxGpuCtx.rtIsTextured		= false;
		break;
	case CP_VRAM_CPU:
		isSizedPrimitive = true;
		isPerVtxCol		= false;
		isFirstVertex	= false;
		psxGpuCtx.rtIsTextured		= false;
		break;
	case CP_CPU_VRAM:
		isSizedPrimitive = true;
		isPerVtxCol		= false;
		isFirstVertex	= false;
		psxGpuCtx.rtIsTextured		= false;
		break;
	case SPECIAL_SETTINGS:
		continueLoop = false;

		switch (command>>24) {
		case 0xE1:
			psxGpuCtx.pageX4		= (command & 0xF) * 64;
			psxGpuCtx.pageY1		= ((command>>4) & 0x1) * 256;
			psxGpuCtx.semiTransp2	= (command>>5) & 0x3;
			psxGpuCtx.textFormat2	= (command>>7) & 0x3;

			psxGpuCtx.dither		= (command>>9) & 0x1;
			psxGpuCtx.displayAreaEnable = (command>>10) & 0x1;
			psxGpuCtx.disableTexture = (command>>11) & 0x1;
			psxGpuCtx.texXFlip		= (command>>12) & 0x1;
			psxGpuCtx.texYFlip		= (command>>13) & 0x1;
			break;
		case 0xE2:
			psxGpuCtx.texMaskX5		= command & 0x1F;
			psxGpuCtx.texMaskY5		= (command>>5) & 0x1F;
			psxGpuCtx.texOffsX5		= (command>>10) & 0x1F;
			psxGpuCtx.texOffsY5		= (command>>15) & 0x1F;
			break;
		case 0xE3:
			psxGpuCtx.drAreaX0_10	= (command>>0 ) & 0x3FF;
			psxGpuCtx.drAreaY0_9	= (command>>10) & 0x1FF;
			break;
		case 0xE4:
			psxGpuCtx.drAreaX1_10	= (command>>0 ) & 0x3FF;
			psxGpuCtx.drAreaY1_9	= (command>>10) & 0x1FF;
			break;
		case 0xE5:
			psxGpuCtx.offsetX_s11	= ((command<<(16+5))>>(16+5));
			psxGpuCtx.offsetY_s11	= ((command<<(  10))>>(16+5));
			break;
		case 0xE6:
			psxGpuCtx.forceMask		= command & 1 ? true : false;
			psxGpuCtx.checkMask		= command & 2 ? true : false;
			break;
		}
		psxGpuCtx.rtIsTextured		= false;
		break;
	}

	while (continueLoop) {

		// -----------------------------------------
		// Always before vertex
		// -----------------------------------------
	
		if (!psxGpuCtx.rtIsTexModRGB && psxGpuCtx.rtIsTextured) {
			operand = 0xFFFFFF; // Force white color for RGB.
		}

		if (isPerVtxCol || isFirstVertex) {
			// [Load Color]
			vtx[vtxCount & 3].r = (operand    ) & 0xFF;
			vtx[vtxCount & 3].g = (operand>>8 ) & 0xFF;
			vtx[vtxCount & 3].b = (operand>>16) & 0xFF;
		} else {
			vtx[vtxCount & 3].r = vtx[0].r;
			vtx[vtxCount & 3].g = vtx[0].g;
			vtx[vtxCount & 3].b = vtx[0].b;
		}

		// -----------------------------------------
		// [Load Vertex]
		operand = *pStream++;
		s16 x = (((s32)operand)<<(16+5))>>(16+5);
		s16 y = (((s32)operand)<<    5 )>>(16+5);
		u8  topV = (operand>>24);
		vtx[vtxCount & 3].x= x + psxGpuCtx.offsetX_s11;
		vtx[vtxCount & 3].y= y + psxGpuCtx.offsetY_s11;
		// -----------------------------------------

		if (psxGpuCtx.rtIsTextured) {
			operand = *pStream++;

			// Read TexCoord + Palette or texPage or nothing (step 0,1, Nothing=2,3)
			// [Load UV]
			vtx[vtxCount & 3].u = operand & 0xFF;
			vtx[vtxCount & 3].v = (operand>>8) & 0xFF;
			switch (vtxCount) {
			case 0:
				psxGpuCtx.rtClutX = ((operand >> 16)     & 0x3F) * 16;
				psxGpuCtx.rtClutY = (operand >> (16+6)) & 0x1FF;
				break;
			case 1: 
				psxGpuCtx.pageX4  = ((operand >> 16) & 0xF) * 64;
				psxGpuCtx.pageY1  = ((operand >> 20) & 0x1) * 256;
				psxGpuCtx.semiTransp2  = (operand >> 21) & 0x3;
				psxGpuCtx.textFormat2  = (operand >> 23) & 0x3;
				// [TODO] Does not support texture disable for now...
				break;
			// Other don't care...
			}
		}

		if (isSizedPrimitive) {
			switch (isHardCodedSize)
			{
			case 0:
				// [Load Size]
				operand = *pStream++;
				width	=   operand     & 0x03FF;
				height	= (operand>>16) & 0x01FF;
				break;
			case 1:
				width = 1; height = 1;
				break;
			case 2:
				width = 8; height = 8;
				break;
			case 3:
				width = 16; height = 16;
				break;
			default:
				break;
			}
		}

		switch (primitive) {
		case PRIM_TRI:
			continueLoop = (vtxCount != (vtxCountMax-1));

			if (vtxCount == 2) {
				// Emit Triangle 0,1,2
				// [TODO INVOKE]
				drawTriangle(0,1,2, window);
			}
			if (vtxCount == 3) {
				// Emit Triangle 1,2,3
				// [TODO INVOKE]
				drawTriangle(1,2,3, window);
			}

			break;
		case PRIM_LINE:
			// [TODO INVOKE]
			assert(false); // NEVER TESTED / IMPLEMENTED.

			continueLoop = isMultiCmd;
			if (isMultiCmd) {
				if (isPerVtxCol) {
					continueLoop = ((operand >> 24) != 0x55);
				} else {
					continueLoop = (topV != 0x55);
				}
			}
			break;
		case PRIM_RECT:
			// Convert rect into triangle.
			printf("Rect @ %i,%i [%i,%i]\n",vtx[0].x,vtx[0].y,width,height);
			if ((vtx[0].x == -6) && (vtx[0].y == 403)) {
				printf("it !\n");
				logAll = true;
			} else {
				logAll = false;
			}

			for (int n=1;n < 4; n++) {
				vtx[n] = vtx[0];
			}
			vtx[1].x += width; vtx[1].y +=      0; vtx[1].u += width; vtx[1].v +=      0; 
			vtx[2].x +=     0; vtx[2].y += height; vtx[2].u +=     0; vtx[2].v += height;
			vtx[3].x += width; vtx[3].y += height; vtx[3].u += width; vtx[3].v += height;

			switch (psxGpuCtx.textFormat2) {
			case 0: printf("  4 bit.\n"); break;
			case 1: printf("  8 bit.\n"); break;
			case 2:
			case 3: printf("  16 bit.\n"); break;
			}

			drawTriangle(0,1,2,window);
			drawTriangle(1,2,3,window);

			continueLoop = false;

			break;

		case SPECIAL_CMD:
			// [TODO INVOKE]
			// [TODO : Patch width , height based on FILL, VRAM Copy commands...]

			if ((command >> 24) == 0x2) {
				vtx[0].x += psxGpuCtx.offsetX_s11;
				vtx[0].y += psxGpuCtx.offsetY_s11;

				vtx[0].x &= 0x3F0;
				vtx[0].y &= 0x1FF;
				width	  = ((width & 0x3FF)+15) & (~0xF);
				height    = height & 0x1FF;
				// Contains color
				FillRect(vtx[0],width,height,window);
			} else {
				// assert(false); // NEVER TESTED / IMPLEMENTED.
			}
			continueLoop = false;
			break;
		case CP_VRAM_VRAM:
			// [TODO INVOKE]
			// assert(false); // NEVER TESTED / IMPLEMENTED.
			continueLoop = false;
			break;
		case CP_VRAM_CPU:
			// [TODO INVOKE]
			assert(false); // NEVER TESTED / IMPLEMENTED.
			continueLoop = false;
			break;
		case CP_CPU_VRAM:
			// [TODO INVOKE]
			CPUToVRAM(vtx[0],width,height,&pStream, window);
			continueLoop = false;
			break;
		}

		if (isPerVtxCol && continueLoop) {
			operand	= *pStream++; // Load NEXT COLOR IF NEEDED.
		}

		isFirstVertex = false;
		vtxCount++;
	}


	if (pStream != pStreamE) {
		goto nextCommand;
	}
	/*
GP0(20h..7Fh) - Render Command Bits

  0-23  Color for (first) Vertex                   (Not for Raw-Texture)
  24    Texture Mode      (0=Blended, 1=Raw)       (Textured-Polygon/Rect only)
  25    Semi Transparency (0=Off, 1=On)            (All Render Types)
  26    Texture Mapping   (0=Off, 1=On)            (Polygon/Rectangle only)
  27-28 Rect Size   (0=Var, 1=1x1, 2=8x8, 3=16x16) (Rectangle only)
  27    Num Vertices      (0=Triple, 1=Quad)       (Polygon only)
  27    Num Lines         (0=Single, 1=Poly)       (Line only)
  28    Shading           (0=Flat, 1=Gouroud)      (Polygon/Line only)
  29-31 Primitive Type    (1=Polygon, 2=Line, 3=Rectangle)



	GP0(20h) - Monochrome three-point polygon, opaque
	GP0(22h) - Monochrome three-point polygon, semi-transparent
	GP0(28h) - Monochrome four-point polygon, opaque
	GP0(2Ah) - Monochrome four-point polygon, semi-transparent
		1st  Color+Command     (CcBbGgRrh)
		2nd  Vertex1           (YyyyXxxxh)
		3rd  Vertex2           (YyyyXxxxh)
		4th  Vertex3           (YyyyXxxxh)
		(5th) Vertex4           (YyyyXxxxh) (if any)
	GP0(24h) - Textured three-point polygon, opaque, texture-blending
	GP0(25h) - Textured three-point polygon, opaque, raw-texture
	GP0(26h) - Textured three-point polygon, semi-transparent, texture-blending
	GP0(27h) - Textured three-point polygon, semi-transparent, raw-texture
	GP0(2Ch) - Textured four-point polygon, opaque, texture-blending
	GP0(2Dh) - Textured four-point polygon, opaque, raw-texture
	GP0(2Eh) - Textured four-point polygon, semi-transparent, texture-blending
	GP0(2Fh) - Textured four-point polygon, semi-transparent, raw-texture
		1st  Color+Command     (CcBbGgRrh) (color is ignored for raw-textures)
		2nd  Vertex1           (YyyyXxxxh)
		3rd  Texcoord1+Palette (ClutYyXxh)
		4th  Vertex2           (YyyyXxxxh)
		5th  Texcoord2+Texpage (PageYyXxh)
		6th  Vertex3           (YyyyXxxxh)
		7th  Texcoord3         (0000YyXxh)
		(8th) Vertex4           (YyyyXxxxh) (if any)
		(9th) Texcoord4         (0000YyXxh) (if any)
	GP0(30h) - Shaded three-point polygon, opaque
	GP0(32h) - Shaded three-point polygon, semi-transparent
	GP0(38h) - Shaded four-point polygon, opaque
	GP0(3Ah) - Shaded four-point polygon, semi-transparent
		1st  Color1+Command    (CcBbGgRrh)
		2nd  Vertex1           (YyyyXxxxh)
		3rd  Color2            (00BbGgRrh)
		4th  Vertex2           (YyyyXxxxh)
		5th  Color3            (00BbGgRrh)
		6th  Vertex3           (YyyyXxxxh)
		(7th) Color4            (00BbGgRrh) (if any)
		(8th) Vertex4           (YyyyXxxxh) (if any)
	GP0(34h) - Shaded Textured three-point polygon, opaque, texture-blending
	GP0(36h) - Shaded Textured three-point polygon, semi-transparent, tex-blend
	GP0(3Ch) - Shaded Textured four-point polygon, opaque, texture-blending
	GP0(3Eh) - Shaded Textured four-point polygon, semi-transparent, tex-blend
		1st  Color1+Command    (CcBbGgRrh)
		2nd  Vertex1           (YyyyXxxxh)
		3rd  Texcoord1+Palette (ClutYyXxh)
		4th  Color2            (00BbGgRrh)
		5th  Vertex2           (YyyyXxxxh)
		6th  Texcoord2+Texpage (PageYyXxh)
		7th  Color3            (00BbGgRrh)
		8th  Vertex3           (YyyyXxxxh)
		9th  Texcoord3         (0000YyXxh)
		(10th) Color4           (00BbGgRrh) (if any)
		(11th) Vertex4          (YyyyXxxxh) (if any)
		(12th) Texcoord4        (0000YyXxh) (if any)

	*/
}



int min3(int a, int b, int c) {
	int p1 = a < b ? a  : b;
	return  p1 < c ? p1 : c;
}

int max3(int a, int b, int c) {
	int p1 = a > b ? a  : b;
	return  p1 > c ? p1 : c;
}

int max(int a, int b) {
	return a > b ? a  : b;
}

int min(int a, int b) {
	return a < b ? a  : b;
}

/*
int orient2d(const VertexRdr& a, const VertexRdr& b, const VertexRdr& c)
{
	return (b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x);
}
*/


void RenderPixel(int x, int y, int* varAttrb, int log) {
	int index = x + y*1024;
	u16 src   = swBuffer[index];
	if ((psxGpuCtx.checkMask && (src & 0x8000)) || !psxGpuCtx.checkMask) {
		// RGB already computed...
		
		int RT,GT,BT; // RGB from texture.
		u16 pixel = 0xFFFF;

		if (!psxGpuCtx.disableTexture && psxGpuCtx.rtIsTextured) {
			// UV to RGB : Texture sampler.

			int U = varAttrb[3];
			int V = varAttrb[4];
			// Texture is repeated outside of 256x256 window
			U &= 0xFF;
			V &= 0xFF;

			// Texture masking
			// texel = (texel AND(NOT(Mask * 8))) OR((Offset AND Mask) * 8)
			U = (U & ~(psxGpuCtx.texMaskX5 * 8)) | ((psxGpuCtx.texOffsX5 & psxGpuCtx.texMaskX5) * 8);
			V = (V & ~(psxGpuCtx.texMaskY5 * 8)) | ((psxGpuCtx.texOffsY5 & psxGpuCtx.texMaskY5) * 8);


			// TODO : Support weird mode color * 2 when mod texture.
			// TODO : Texture sampler : loop, masking, pixel format (True color, 8 bit, 4 bit)

			switch (psxGpuCtx.textFormat2) {
			case 0:
			{
				// 4 Bit
				int subPix = U & 3;
				U = (U>>2) + (psxGpuCtx.pageX4);
				V = V      + (psxGpuCtx.pageY1);
				int vramAdr = (U & 0x3FF) + (V*1024);
				pixel = swBuffer[vramAdr];

				if (log) { printf("  X,Y %i,%i / U,V : %i,%i -> @adr = %i\n",x,y,U,V,vramAdr); }

				uint8_t palIndex = (pixel >> (subPix * 4)) & 0xf;

				pixel = swBuffer[((palIndex + psxGpuCtx.rtClutX) & 0x3FF) + (psxGpuCtx.rtClutY*1024)];
			}
			break;
			case 1:
			{
				// 8 Bit
				int subPix = U & 1;
				U = (U>>1)  + (psxGpuCtx.pageX4);
				V = V       + (psxGpuCtx.pageY1);
				
				int vramAdr = (U & 0x3FF) + (V*1024);

				pixel = swBuffer[vramAdr];

				if (log || logAll) { printf("  X,Y %i,%i / U,V : %i,%i -> @adr = %x -> V=%x ",x,y,U,V,vramAdr,pixel); }

				uint8_t palIndex = (pixel >> (subPix * 8)) & 0xFF;

				pixel = swBuffer[((palIndex + (psxGpuCtx.rtClutX)) & 0x3FF) + (psxGpuCtx.rtClutY*1024)];

				if (log || logAll) { printf("palIdx:%x palV =%x \n",palIndex,pixel); }

			}
			break;
			case 2:
			case 3:
				// 15 bit.
			{
				U = U + psxGpuCtx.pageX4;
				V = V + psxGpuCtx.pageY1;
				
				int vramAdr = (U & 0x3FF) + (V*1024);

				if (log) { printf("  X,Y %i,%i / U,V : %i,%i -> @adr = %i\n",x,y,U,V,vramAdr); }

				pixel = swBuffer[vramAdr];

			}
			break;
			
			} // End switch case.
			
			// 555 -> 888 1.0=256.
			Convert555ToRGB(pixel,RT,GT,BT);

			if (psxGpuCtx.rtIsTexModRGB) {
				// Color multiplier by 2
				// RGB x 2 in case of texture modulation ?
				varAttrb[0] <<= 1;
				varAttrb[1] <<= 1;
				varAttrb[2] <<= 1;
				varAttrb[0]--;
				varAttrb[1]--;
				varAttrb[2]--;
			}

			if (pixel == 0) {
				return;
			}
		} else {
			// Return white pixel, end of story.
			RT = 0x100;
			GT = 0x100;
			BT = 0x100;
		}

		int FR,FG,FB;

		// Modulate RGB
		FR = (RT * varAttrb[0]) >> 8;
		FG = (GT * varAttrb[1]) >> 8;
		FB = (BT * varAttrb[2]) >> 8;

		if (psxGpuCtx.rtUseSemiTransp) { // ([TODO Bit 15 of Texel skip transp -> Full opaque] || noTexture)
			// V 0..31
			int BG_R = (src    ) & 0x1F;
			int BG_G = (src>> 5) & 0x1F;
			int BG_B = (src>>10) & 0x1F;

			BG_R = (BG_R<<3) | (BG_R>>2);
			BG_G = (BG_G<<3) | (BG_G>>2);
			BG_B = (BG_B<<3) | (BG_B>>2);

			// 5-6 Semi Transparency (0=B/2+F/2, 1=B+F, 2=B-F, 3=B+F/4)  ;GP0(E1h).5-6
			switch (psxGpuCtx.semiTransp2) {
			case 0:
				FR = (BG_R+FR)>>1;
				FG = (BG_G+FG)>>1;
				FB = (BG_B+FB)>>1;
				break;
			case 1:
				FR = BG_R+FR;
				FG = BG_G+FG;
				FB = BG_B+FB;
				break;
			case 2:
				FR = BG_R - FR;
				FG = BG_G - FG;
				FB = BG_B - FB;
				break;
			case 3:
				FR = BG_R+(FR>>2);
				FG = BG_G+(FG>>2);
				FB = BG_B+(FB>>2);
				break;
			}
		}

		static const int8_t ditherTable[4][4] = {
			{-4, +0, -3, +1},  //
			{+2, -2, +3, -1},  //
			{-3, +1, -4, +0},  //
			{+3, -1, +2, -2}   //
		};

		if (psxGpuCtx.dither && (psxGpuCtx.disableTexture || (psxGpuCtx.rtIsTexModRGB && psxGpuCtx.rtIsPerVtx))) {
			int d = ditherTable[y & 3][x & 3];
			FR += d;
			FG += d;
			FB += d;
		}


		if (FR < 0)   { FR = 0; }
		if (FG < 0)   { FG = 0; }
		if (FB < 0)   { FB = 0; }
		if (FR > 255) { FR = 255; }
		if (FG > 255) { FG = 255; }
		if (FB > 255) { FB = 255; }

		// Just write...
		swBuffer[index] = ConvertRGBTo555(FR,FG,FB) | (psxGpuCtx.forceMask ? 0x8000 : 0);
	} // Else do not write...
}

// Is Horizontal and going from 
static bool isTopLeft(const VertexRdr& e) { return e.y < 0 || (e.y == 0 && e.x < 0); }
static bool isTopLeft(int x, int y)       { return   y < 0 || (  y == 0 &&   x < 0); }

int orient2d(const VertexRdr& a, const VertexRdr& b, const VertexRdr& c)
{
    return (b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x);
}

void RenderTriangle(int v0Idx, int v1Idx, int v2Idx, struct mfb_window *window)
{
	VertexRdr& v0 = vtx[v0Idx];
	VertexRdr& v1 = vtx[v1Idx];
	VertexRdr& v2 = vtx[v2Idx];

	// Compute triangle bounding box
	int minX = min3(v0.x, v1.x, v2.x);
	int minY = min3(v0.y, v1.y, v2.y);
	int maxX = max3(v0.x, v1.x, v2.x);
	int maxY = max3(v0.y, v1.y, v2.y);

	// Clip against screen bounds
	minX = max(minX, psxGpuCtx.drAreaX0_10);
	minY = max(minY, psxGpuCtx.drAreaY0_9);
	maxX = min(maxX, psxGpuCtx.drAreaX1_10);
	maxY = min(maxY, psxGpuCtx.drAreaY1_9);

	// Rasterize
	VertexRdr p;

	// ---------------------------------------------------
	//   Per Triangle
	// ---------------------------------------------------
	// 1. Triangle as a 2D Matrix
	//
	//  11 bit signed coord, 11 bit delta X, 10 bit delta Y (overflow -> reject)
	//
	// => Compute a,b,d,c as 12 bit and compute rejection here...
	//    but will use a,b,c,d wire as 11 bit for further computation.
	int nv0x	= -v0.x;
	int nv0y	= -v0.y;

	int preA	= v2.x + nv0x; int preB = v2.y + nv0y;	// V02 : OK.
	int c		= v1.x + nv0x; int    d = v1.y + nv0y;		// V01 : Ignore.
	int e		= v2.x - v1.x; int    f = v1.y - v2.y;		// V21 : f only OK., -e for other direction.


	int nv1x	= -v1.x;
	int nv1y	= -v1.y;
	int negc    = v0.x + nv1x; int negd = v0.y + nv1y;	// V10 : OK.
	int nege	= v1.x - v2.x;
	int negb    = -preA;
	int nega    = -preB;

	bool lineMode = false;

	// 2. DET result 
	int D = preA*d - preB*c;

	int C20iR = v2.r - v0.r;
	int C10iR = v1.r - v0.r;
	int C20iG = v2.g - v0.g;
	int C10iG = v1.g - v0.g;
	int C20iB = v2.b - v0.b;
	int C10iB = v1.b - v0.b;
	int C20iU = v2.u - v0.u;
	int C10iU = v1.u - v0.u;
	int C20iV = v2.v - v0.v;
	int C10iV = v1.v - v0.v;

	int PREC = 11; // 9 or 10... Float also generate same line error.

	// 10b + 8b + 10b = 28
	int uhiR  = ((    d * C20iR)<<PREC)/D;
	int vhiR  = (( negc * C20iR)<<PREC)/D;
	int uviR  = (( negb * C10iR)<<PREC)/D;
	int vviR  = (( preA * C10iR)<<PREC)/D;
	int uxR   = uhiR+uviR;
	int vyR   = vhiR+vviR;

	// 2 DIV Unit in // is NICE. -> Faster setup, but easier addition too. (Same timing)
	int uhiG  = (( d * C20iG)<<PREC)/D;
	int vhiG  = ((negc * C20iG)<<PREC)/D;
	int uviG  = ((negb * C10iG)<<PREC)/D;
	int vviG  = (( preA * C10iG)<<PREC)/D;
	int uxG   = uhiG+uviG;
	int vyG   = vhiG+vviG;

	int uhiB  = (( d * C20iB)<<PREC)/D;
	int vhiB  = ((negc * C20iB)<<PREC)/D;
	int uviB  = ((negb * C10iB)<<PREC)/D;
	int vviB  = (( preA * C10iB)<<PREC)/D;
	int uxB   = uhiB+uviB;
	int vyB   = vhiB+vviB;

	int uhiU  = (( d * C20iU)<<PREC)/D;
	int vhiU  = ((negc * C20iU)<<PREC)/D;
	int uviU  = ((negb * C10iU)<<PREC)/D;
	int vviU  = (( preA * C10iU)<<PREC)/D;
	int uxU   = uhiU+uviU;
	int vyU   = vhiU+vviU;

	int uhiV  = (( d * C20iV)<<PREC)/D;
	int vhiV  = ((negc * C20iV)<<PREC)/D;
	int uviV  = ((negb * C10iV)<<PREC)/D;
	int vviV  = (( preA * C10iV)<<PREC)/D;
	int uxV   = uhiV+uviV;
	int vyV   = vhiV+vviV;

	// Delta constants
	const VertexRdr D12(e,  f);
	const VertexRdr D20(nega, preB);
	const VertexRdr D01(c, negd); // Warning Y is V0-V1, not V1-V0

	int pixelCounter = 0;

	for (p.y = minY; p.y <= maxY; p.y++) {
		for (p.x = minX; p.x <= maxX; p.x++) {
			int distX = p.x + nv0x;
			int distY = p.y + nv0y;

			int offR  = (distX*uxR + distY*vyR) + (1<<(PREC-1));
			int offG  = (distX*uxG + distY*vyG) + (1<<(PREC-1));
			int offB  = (distX*uxB + distY*vyB) + (1<<(PREC-1));
			int offU  = (distX*uxU + distY*vyU) + (1<<(PREC-1));
			int offV  = (distX*uxV + distY*vyV) + (1<<(PREC-1));

			int   compiRL   = v0.r +  (offR>>PREC);
			int   compiGL   = v0.g +  (offG>>PREC);
			int   compiBL   = v0.b +  (offB>>PREC);
			int   compiUL   = v0.u +  (offB>>PREC);
			int   compiVL   = v0.v +  (offB>>PREC);


			// edgeFunction(const Vec2f &a, const Vec3f &b, const Vec2f &c) return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
			// float w0 = edgeFunction(v1, v2, p); // signed area of the triangle v1v2p multiplied by 2 
			// float w1 = edgeFunction(v2, v0, p); // signed area of the triangle v2v0p multiplied by 2 
			// float w2 = edgeFunction(v0, v1, p); // signed area of the triangle v0v1p multiplied by 2 
			
			// int w0 = (p.x-v1.x) * (v2.y-v1.y) - (p.y-v1.y) * (v2.x-v1.x);
			// int w1 = (p.x-v2.x) * (v0.y-v2.y) - (p.y-v2.y) * (v0.x-v2.x);
			// int w2 = (p.x-v0.x) * (v1.y-v0.y) - (p.y-v0.y) * (v1.x-v0.x);
			int w0 = orient2d(v1,v2,p); // (v2.x-v1.x)*(p.y-v2.y) - (v2.y-v1.y)*(p.x-v1.x)
			int w1 = orient2d(v2,v0,p);
			int w2 = orient2d(v0,v1,p);

			// Determine barycentric coordinates
			// W0L : f is opposite -> transform - into +.
			// 6 MUL, 3 SUB // -(b.y-a.y) -> + (a.y-b.y)
			
			// If p is on or inside all edges, render pixel.
//			if (((w0L | w1L | w2L) >= 0) || ((w0L & w1L & w2L) < 0)) { // (HW w0&w1&w2 < 0 is just LAST BIT AND == 1)
				// if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)||lineMode) {
			if ((w0 >= 0) && (w1 >= 0) && (w2 >= 0)) {
				int var[5];
				var[0] = compiRL;
				var[1] = compiGL;
				var[2] = compiBL;
				var[3] = compiUL;
				var[4] = compiVL;

				RenderPixel(p.x,p.y,var,(pixelCounter == 0));
				pixelCounter++;
			}
		}

		Convert16To32((u8*)swBuffer, buffer32Bit);
		mfb_update(window,buffer32Bit);
	}
}


void drawTriangle(int v0Idx, int v1Idx, int v2Idx, struct mfb_window *window)
{
	VertexRdr& v0 = vtx[v0Idx];
	VertexRdr& v1 = vtx[v1Idx];
	VertexRdr& v2 = vtx[v2Idx];


	// Compute triangle bounding box
	int minX = min3(v0.x, v1.x, v2.x);
	int minY = min3(v0.y, v1.y, v2.y);
	int maxX = max3(v0.x, v1.x, v2.x);
	int maxY = max3(v0.y, v1.y, v2.y);

	// Clip against screen bounds
	minX = max(minX, psxGpuCtx.drAreaX0_10);
	minY = max(minY, psxGpuCtx.drAreaY0_9);
	maxX = min(maxX, psxGpuCtx.drAreaX1_10);
	maxY = min(maxY, psxGpuCtx.drAreaY1_9);

	// TODO : https://www.scratchapixel.com/lessons/3d-basic-rendering/rasterization-practical-implementation/rasterization-stage
	// 
	// Rasterize
	VertexRdr p;


	// ---------------------------------------------------
	//   Per Triangle
	// ---------------------------------------------------
	// 1. Triangle as a 2D Matrix
	//
	//  11 bit signed coord, 11 bit delta X, 10 bit delta Y (overflow -> reject)
	//
	// => Compute a,b,d,c as 12 bit and compute rejection here...
	//    but will use a,b,c,d wire as 11 bit for further computation.
	int nv0x	= -v0.x;
	int nv0y	= -v0.y;
	int a		= v2.x + nv0x; int b = v2.y + nv0y;
	int c		= v1.x + nv0x; int d = v1.y + nv0y;
	int e		= v2.x - v1.x; int f = v1.y - v2.y;

	// Delta constants
	const VertexRdr D12(e,  f);
	const VertexRdr D20(-a, b);
	const VertexRdr D01(c, -d); // Warning Y is V0-V1, not V1-V0

	// Fill rule
	const int bias[3] = {
		isTopLeft(D12) ? -1 : 0,  //
		isTopLeft(D20) ? -1 : 0,  //
		isTopLeft(D01) ? -1 : 0   //
	};

#if 0
	if (lineMode) {
		a =  d;
		b = -c;
		minX =0;minY=0;
		maxX=buffer.widthPixel  - 1;
		maxY=buffer.heightPixel  - 1;

	}
#endif

	// 2. DET result 
	int D = a*d - b*c;

	if (D == 0) {
		return;
	}

#if 0
	// 3. Inverse Gradient
	// TODO : By doing the division at EARLIER stage with intermediate value instead of final division, we need more precision now.
	//        IMPLEMENT HW bit precision here... 23 bit seems big, 16 bit seems good as guestimate, 20 still OK anyway.

	// LINE MODE SHOULD AVOID using /D and use only [c,d] vector directly.

	// COMPUTATION NO NEEDED IN LINE MODE. (UH/VH)
	float uh = (lineMode ? 0 : ((float) d)/D);	// Screen space X Step
	float vh = (lineMode ? 0 : ((float)-c)/D);

	float uv = ((float)-b)/D;	// Screen space Y Step
	float vv = ((float) a)/D;

	//   Per Component inside a triangle
	// ---------------------------------------------------

	// 1. Selector compo C0,C1,C2 (9 bit unsigned)

	// 2. Hard code the operation
	float C20 = v2.compo - v0.compo; // => 10 bit signed   COMPUTATION NO NEEDED IN LINE MODE.
	float C10 = v1.compo - v0.compo;

	float C20r = v2.r - v0.r; // => 10 bit signed   COMPUTATION NO NEEDED IN LINE MODE.
	float C20g = v2.g - v0.g; // => 10 bit signed   COMPUTATION NO NEEDED IN LINE MODE.
	float C20b = v2.b - v0.b; // => 10 bit signed   COMPUTATION NO NEEDED IN LINE MODE.
	float C10r = v1.r - v0.r;
	float C10g = v1.g - v0.g;
	float C10b = v1.b - v0.b;

	// float dxC = ((d  * C20) - (b * C10)) / ((float)D);
	//          20* 10 + 20*10  = 31 bit signed.
	float dxC  = (lineMode ? 0 : uh*C20) + uv*C10; // Same as previous comment.
	float dxCR = (lineMode ? 0 : uh*C20r) + uv*C10r; // Same as previous comment.
	float dxCG = (lineMode ? 0 : uh*C20g) + uv*C10g; // Same as previous comment.
	float dxCB = (lineMode ? 0 : uh*C20b) + uv*C10b; // Same as previous comment.

	// float dyC = ((a * C10) - (c * C20)) / ((float)D); // <= ((-c * C20) + (a * C10)) / ((float)D);
	float dyC  = (lineMode ? 0 : vh*C20) + vv*C10; // Same as previous comment.
	float dyCR = (lineMode ? 0 : vh*C20r) + vv*C10r; // Same as previous comment.
	float dyCG = (lineMode ? 0 : vh*C20g) + vv*C10g; // Same as previous comment.
	float dyCB = (lineMode ? 0 : vh*C20b) + vv*C10b; // Same as previous comment.
#endif

	int C20iR = v2.r - v0.r;
	int C10iR = v1.r - v0.r;
	int C20iG = v2.g - v0.g;
	int C10iG = v1.g - v0.g;
	int C20iB = v2.b - v0.b;
	int C10iB = v1.b - v0.b;
	int C20iU = v2.u - v0.u;
	int C10iU = v1.u - v0.u;
	int C20iV = v2.v - v0.v;
	int C10iV = v1.v - v0.v;

	int PREC = 11; // 9 or 10... Float also generate same line error.

	// 10b + 8b + 10b = 28
	int uhiR  = (( d * C20iR)<<PREC)/D;
	int vhiR  = ((-c * C20iR)<<PREC)/D;
	int uviR  = ((-b * C10iR)<<PREC)/D;
	int vviR  = (( a * C10iR)<<PREC)/D;
	int uxR   = uhiR+uviR;
	int vyR   = vhiR+vviR;

	// 2 DIV Unit in // is NICE. -> Faster setup, but easier addition too. (Same timing)
	int uhiG  = (( d * C20iG)<<PREC)/D;
	int vhiG  = ((-c * C20iG)<<PREC)/D;
	int uviG  = ((-b * C10iG)<<PREC)/D;
	int vviG  = (( a * C10iG)<<PREC)/D;
	int uxG   = uhiG+uviG;
	int vyG   = vhiG+vviG;

	int uhiB  = (( d * C20iB)<<PREC)/D;
	int vhiB  = ((-c * C20iB)<<PREC)/D;
	int uviB  = ((-b * C10iB)<<PREC)/D;
	int vviB  = (( a * C10iB)<<PREC)/D;
	int uxB   = uhiB+uviB;
	int vyB   = vhiB+vviB;

	int uhiU  = (( d * C20iU)<<PREC)/D;
	int vhiU  = ((-c * C20iU)<<PREC)/D;
	int uviU  = ((-b * C10iU)<<PREC)/D;
	int vviU  = (( a * C10iU)<<PREC)/D;
	int uxU   = uhiU+uviU;
	int vyU   = vhiU+vviU;

	int uhiV  = (( d * C20iV)<<PREC)/D;
	int vhiV  = ((-c * C20iV)<<PREC)/D;
	int uviV  = ((-b * C10iV)<<PREC)/D;
	int vviV  = (( a * C10iV)<<PREC)/D;
	int uxV   = uhiV+uviV;
	int vyV   = vhiV+vviV;

	int pixCounter = 0;

	for (p.y = minY; p.y <= maxY; p.y++) {
		for (p.x = minX; p.x <= maxX; p.x++) {
			int distX = p.x + nv0x;
			int distY = p.y + nv0y;

			int offR  = (distX*uxR + distY*vyR) + (1<<(PREC-1));
			int offG  = (distX*uxG + distY*vyG) + (1<<(PREC-1));
			int offB  = (distX*uxB + distY*vyB) + (1<<(PREC-1));
			int offU  = (distX*uxU + distY*vyU) + (1<<(PREC-1));
			int offV  = (distX*uxV + distY*vyV) + (1<<(PREC-1));
			int   compiRL   = v0.r +  (offR>>PREC);
			int   compiGL   = v0.g +  (offG>>PREC);
			int   compiBL   = v0.b +  (offB>>PREC);
			int   compiUL   = v0.u +  (offU>>PREC);
			int   compiVL   = v0.v +  (offV>>PREC);

			// Determine barycentric coordinates
			// W0L : f is opposite -> transform - into +.
			int w0L =    e*(p.y-v1.y) + f*(p.x-v1.x) /*orient2d(v1, v2, p)*/ + bias[0]; // return (b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x);			2 MUL, 1 SUB
			int w1L = (-a)*(p.y-v2.y) + b*(p.x-v2.x) /*orient2d(v2, v0, p)*/ + bias[1];
			int w2L =    c*distY      - d*distX      /*orient2d(v0, v1, p)*/ + bias[2];
			
			// If p is on or inside all edges, render pixel.
			if (((w0L | w1L | w2L) >= 0) || ((w0L & w1L & w2L) < 0)) { // (HW w0&w1&w2 < 0 is just LAST BIT AND == 1)
				// if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)||lineMode) {
				int var[5];
				var[0] = compiRL;
				var[1] = compiGL;
				var[2] = compiBL;
				var[3] = compiUL;
				var[4] = compiVL;

				RenderPixel(p.x,p.y,var, (pixCounter == 0));
				pixCounter++;
			}
		}
	}

	Convert16To32((u8*)swBuffer, buffer32Bit);
	mfb_update(window,buffer32Bit);
}
