//============================================================================
//  ColecoVision
//
//  Port to MiSTer
//  Copyright (C) 2017-2019 Sorgelig
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but WITHOUT
//  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
//  more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program; if not, write to the Free Software Foundation, Inc.,
//  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//============================================================================

module emu
(
	//Master input clock
	input         CLK_50M,

	//Async reset from top-level module.
	//Can be used as initial reset.
	input         RESET,

	//Must be passed to hps_io module
	inout  [45:0] HPS_BUS,

	//Base video clock. Usually equals to CLK_SYS.
	output        CLK_VIDEO,

	//Multiple resolutions are supported using different CE_PIXEL rates.
	//Must be based on CLK_VIDEO
	output        CE_PIXEL,

	//Video aspect ratio for HDMI. Most retro systems have ratio 4:3.
	output  [7:0] VIDEO_ARX,
	output  [7:0] VIDEO_ARY,

	output  [7:0] VGA_R,
	output  [7:0] VGA_G,
	output  [7:0] VGA_B,
	output        VGA_HS,
	output        VGA_VS,
	output        VGA_DE,    // = ~(VBlank | HBlank)
	output        VGA_F1,
	output [1:0]  VGA_SL,

	output        LED_USER,  // 1 - ON, 0 - OFF.

	// b[1]: 0 - LED status is system status OR'd with b[0]
	//       1 - LED status is controled solely by b[0]
	// hint: supply 2'b00 to let the system control the LED.
	output  [1:0] LED_POWER,
	output  [1:0] LED_DISK,

	// I/O board button press simulation (active high)
	// b[1]: user button
	// b[0]: osd button
	output  [1:0] BUTTONS,

	output [15:0] AUDIO_L,
	output [15:0] AUDIO_R,
	output        AUDIO_S, // 1 - signed audio samples, 0 - unsigned
	output  [1:0] AUDIO_MIX, // 0 - no mix, 1 - 25%, 2 - 50%, 3 - 100% (mono)

	//ADC
	inout   [3:0] ADC_BUS,

	//SD-SPI
	output        SD_SCK,
	output        SD_MOSI,
	input         SD_MISO,
	output        SD_CS,
	input         SD_CD,

	//High latency DDR3 RAM interface
	//Use for non-critical time purposes
	output        DDRAM_CLK,
	input         DDRAM_BUSY,
	output  [7:0] DDRAM_BURSTCNT,
	output [28:0] DDRAM_ADDR,
	input  [63:0] DDRAM_DOUT,
	input         DDRAM_DOUT_READY,
	output        DDRAM_RD,
	output [63:0] DDRAM_DIN,
	output  [7:0] DDRAM_BE,
	output        DDRAM_WE,

	//SDRAM interface with lower latency
	output        SDRAM_CLK,
	output        SDRAM_CKE,
	output [12:0] SDRAM_A,
	output  [1:0] SDRAM_BA,
	inout  [15:0] SDRAM_DQ,
	output        SDRAM_DQML,
	output        SDRAM_DQMH,
	output        SDRAM_nCS,
	output        SDRAM_nCAS,
	output        SDRAM_nRAS,
	output        SDRAM_nWE,

	input         UART_CTS,
	output        UART_RTS,
	input         UART_RXD,
	output        UART_TXD,
	output        UART_DTR,
	input         UART_DSR,

	// Open-drain User port.
	// 0 - D+/RX
	// 1 - D-/TX
	// 2..6 - USR2..USR6
	// Set USER_OUT to 1 to read from USER_IN.
	input   [6:0] USER_IN,
	output  [6:0] USER_OUT,

	input         OSD_STATUS
);

assign ADC_BUS = 'Z;
assign USER_OUT = '1;
assign {UART_RTS, UART_TXD, UART_DTR} = 0;
assign {SD_SCK, SD_MOSI, SD_CS} = 'Z;
//assign {DDRAM_CLK, DDRAM_BURSTCNT, DDRAM_ADDR, DDRAM_DIN, DDRAM_BE, DDRAM_RD, DDRAM_WE} = 0;
 
assign LED_USER  = ioctl_download;
assign LED_DISK  = 0;
assign LED_POWER = 0;
assign BUTTONS   = 0;

assign VIDEO_ARX = status[1] ? 8'd16 : 8'd4;
assign VIDEO_ARY = status[1] ? 8'd9  : 8'd3; 

`include "build_id.v" 
parameter CONF_STR = {
	"Coleco;;",
	"-;",
	"F,COLBINROM;",
	"F,SG,Load SG-1000;",
	"-;",
	"O1,Aspect ratio,4:3,16:9;",
	"O79,Scandoubler Fx,None,HQ2x,CRT 25%,CRT 50%;",
	"-;",
	"O3,Joysticks swap,No,Yes;",
	"-;",
	"O45,RAM Size,1KB,8KB,SGM;",
	"R0,Reset;",
	"J1,Fire 1,Fire 2,*,#,0,1,2,3,4,5,6,7,8,9,Purple Tr,Blue Tr;",
	"V,v",`BUILD_DATE
};

/////////////////  CLOCKS  ////////////////////////

wire clk_sys;
wire pll_locked;

pll pll
(
	.refclk(CLK_50M),
	.rst(0),
	.outclk_0(clk_sys),
	.locked(pll_locked)
);

reg ce_10m7 = 0;
reg ce_5m3 = 0;
always @(posedge clk_sys) begin
	reg [2:0] div;
	
	div <= div+1'd1;
	ce_10m7 <= !div[1:0];
	ce_5m3  <= !div[2:0];
end

/////////////////  HPS  ///////////////////////////

wire [31:0] status;
wire  [1:0] buttons;

wire [31:0] joy0, joy1;

wire        ioctl_download;
wire  [7:0] ioctl_index;
wire        ioctl_wr;
wire [24:0] ioctl_addr;
wire  [7:0] ioctl_dout;
wire        forced_scandoubler;
wire [21:0] gamma_bus;
 
hps_io #(.STRLEN($size(CONF_STR)>>3)) hps_io
(
	.clk_sys(clk_sys),
	.HPS_BUS(HPS_BUS),

	.conf_str(CONF_STR),

	.buttons(buttons),
	.status(status),
	.forced_scandoubler(forced_scandoubler),
	.gamma_bus(gamma_bus),

	.ioctl_download(ioctl_download),
	.ioctl_index(ioctl_index),
	.ioctl_wr(ioctl_wr),
	.ioctl_addr(ioctl_addr),
	.ioctl_dout(ioctl_dout),

	.joystick_0(joy0),
	.joystick_1(joy1)
);

/////////////////  RESET  /////////////////////////

wire reset = RESET | status[0] | buttons[1] | ioctl_download;

/*
assign SDRAM_CLK = ~clk_sys;
sdram sdram
(
	.*,
	.init(~pll_locked),
	.clk(clk_sys),

   .wtbt(0),
   .addr(ioctl_download ? ioctl_addr : cart_a),
   .rd(cart_rd),
   .dout(cart_d),
   .din(ioctl_dout),
   .we(ioctl_wr),
   .ready()
);
*/

gpu gpu_inst
(
	.clk( clk_sys ) ,					// input  clk
	.i_nrst( !reset ) ,				// input  i_nrst
	
	.IRQRequest(IRQRequest) ,		// output  IRQRequest
	
	.DMA_REQ(DMA_REQ) ,				// output  DMA_REQ
	.DMA_ACK( 1'b0 ) ,				// input  DMA_ACK
	
	.mydebugCnt(mydebugCnt) ,		// output [31:0] mydebugCnt
	.dbg_canWrite(dbg_canWrite) ,	// output  dbg_canWrite
	
	.clkBus( clk_sys ) ,				// input  clkBus
	
	.o_command(o_command) ,			// output  o_command
	.i_busy(i_busy) ,					// input  i_busy
	.o_commandSize(o_commandSize) ,	// output [1:0] o_commandSize
	.o_write(o_write) ,				// output  o_write
	.o_adr(o_adr) ,					// output [14:0] o_adr
	.o_subadr(o_subadr) ,			// output [2:0] o_subadr
	.o_writeMask(o_writeMask) ,	// output [15:0] o_writeMask
	.i_dataIn(i_dataIn) ,			// input [255:0] i_dataIn
	.i_dataInValid(i_dataInValid) ,	// input  i_dataInValid
	.o_dataOut(o_dataOut) ,			// output [255:0] o_dataOut
	
	.gpuAdrA2(gpuAdrA2) ,			// input  gpuAdrA2
	.gpuSel(gpuSel) ,					// input  gpuSel
	.write(write) ,					// input  write
	.read(read) ,						// input  read
	.cpuDataIn(cpuDataIn) ,			// input [31:0] cpuDataIn
	.cpuDataOut(cpuDataOut) ,		// output [31:0] cpuDataOut
	.validDataOut(validDataOut) 	// output  validDataOut
);

wire o_command;
wire i_busy = o_busyClient;
wire [1:0] o_commandSize;
wire o_write;
wire [14:0] o_adr;
wire [2:0] o_subadr;
wire [15:0] o_writeMask;
wire [255:0] i_dataIn = o_dataClient;
wire i_dataInValid = o_dataValidClient;
wire [255:0] o_dataOut;


wire i_command = o_command;
wire i_writeElseRead = o_write;
wire [1:0] i_commandSize = o_commandSize;
wire [14:0] i_targetAddr = o_adr;
wire [2:0] i_subAddr = o_subadr;
wire [15:0] i_writeMask = o_writeMask;
wire [255:0] i_dataClient = o_dataOut;

wire o_busyClient;
wire o_dataValidClient;
wire [255:0] o_dataClient;

PSX_DDR_Interface PSX_DDR_Interface_inst
(
	.i_clk( clk_sys ) ,						// input  i_clk
	.i_rst( !reset ) ,						// input  i_rst
	
	.i_command(i_command) ,					// input  i_command
	.i_writeElseRead(i_writeElseRead) ,	// input  i_writeElseRead
	.i_commandSize(i_commandSize) ,		// input [1:0] i_commandSize
	.i_targetAddr(i_targetAddr) ,			// input [14:0] i_targetAddr
	.i_subAddr(i_subAddr) ,					// input [2:0] i_subAddr
	.i_writeMask(i_writeMask) ,			// input [15:0] i_writeMask
	.i_dataClient(i_dataClient) ,			// input [255:0] i_dataClient
	.o_busyClient(o_busyClient) ,			// output  o_busyClient
	.o_dataValidClient(o_dataValidClient) ,	// output  o_dataValidClient
	.o_dataClient(o_dataClient) ,			// output [255:0] o_dataClient
	
	.i_busyMem(i_busyMem) ,					// input  i_busyMem
	.i_dataValidMem(i_dataValidMem) ,	// input  i_dataValidMem
	.i_dataMem(i_dataMem) ,					// input [31:0] i_dataMem
	.o_writeEnableMem(o_writeEnableMem) ,	// output  o_writeEnableMem
	.o_readEnableMem(o_readEnableMem) ,	// output  o_readEnableMem
	.o_burstLength(o_burstLength) ,		// output [7:0] o_burstLength
	.o_dataMem(o_dataMem) ,					// output [31:0] o_dataMem
	.o_targetAddr(o_targetAddr) ,			// output [25:0] o_targetAddr
	.o_byteEnableMem(o_byteEnableMem) 	// output [3:0] o_byteEnableMem
);

wire i_busyMem = DDRAM_BUSY;
wire i_dataValidMem = DDRAM_DOUT_READY;
wire [31:0] i_dataMem = DDRAM_DOUT[31:0];	// Note: DDRAM_DOUT is 64-bit.
wire o_writeEnableMem;
wire o_readEnableMem;
wire [7:0] o_burstLength;
wire [31:0] o_dataMem;
wire [25:0] o_targetAddr;
wire [3:0] o_byteEnableMem;


assign DDRAM_CLK = clk_sys;
assign DDRAM_BURSTCNT = o_burstLength;
assign DDRAM_ADDR = {3'b001, o_targetAddr};	// Not sure where to map this yet? ElectronAsh.
assign DDRAM_DIN = DDRAM_DIN;						// Note: DDRAM_DIN is 64-bit.
assign DDRAM_BE = {4'b0000, o_byteEnableMem};// Note: DDRAM_BE has 8 bits.
assign DDRAM_WR = o_writeEnableMem;
assign DDRAM_RD = o_readEnableMem;



assign VGA_F1 = 0;
assign VGA_SL = sl[1:0];

wire [2:0] scale = status[9:7];
wire [2:0] sl = scale ? scale - 1'd1 : 3'd0;

/*
reg hs_o, vs_o;
always @(posedge CLK_VIDEO) begin
	hs_o <= ~hsync;
	if(~hs_o & ~hsync) vs_o <= ~vsync;
end
*/

video_mixer #(.LINE_LENGTH(290), .GAMMA(1)) video_mixer
(
	.*,

	.clk_vid(CLK_VIDEO),
	.ce_pix(ce_5m3),
	.ce_pix_out(CE_PIXEL),

	.scanlines(0),
	.scandoubler(scale || forced_scandoubler),
	.hq2x(scale==1),

	.mono(0),

	.R(R),
	.G(G),
	.B(B),

	// Positive pulses.
	.HSync(hs_o),
	.VSync(vs_o),
	.HBlank(hblank),
	.VBlank(vblank)
);


endmodule
