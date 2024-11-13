//======================================================================
//
// rom.v
// ------
// Firmware ROM module. Implemented using Embedded Block RAM
// in the FPGA.
//
//
// Author: Joachim Strombergson
// Copyright (C) 2022 - Tillitis AB
// SPDX-License-Identifier: GPL-2.0-only
//
//======================================================================

`default_nettype none

module rom (
    input wire clk,
    input wire reset_n,

    input  wire          cs,
    /* verilator lint_off UNUSED */
    input  wire [11 : 0] address,
    /* verilator lint_on UNUSED */
    output wire [31 : 0] read_data,
    output wire          ready
);


  //----------------------------------------------------------------
  // Registers, memories with associated wires.
  //----------------------------------------------------------------
  // Size of the sysMem Embedded Block RAM (EBR) memory primarily
  // used for code storage (ROM). The size is number of
  // 32-bit words. Each EBR is 4kbit in size, and (at most)
  // 16-bit wide. Thus means that we use pairs of EBRs, and
  // each pair store 256 32bit words.
  // The size of the EBR allocated to memory must match the
  // size of the firmware file generated by the Makefile.
  //
  // Max size for the ROM is 3072 words, and the address is
  // 12 bits to support ROM with this number of words.
  localparam EBR_MEM_SIZE = `BRAM_FW_SIZE;
  reg [31 : 0] memory[0 : (EBR_MEM_SIZE - 1)];
  initial $readmemh(`FIRMWARE_HEX, memory);

  reg [31 : 0] rom_rdata;
  reg          ready_reg;


  //----------------------------------------------------------------
  // Concurrent assignments of ports.
  //----------------------------------------------------------------
  assign read_data = rom_rdata;
  assign ready     = ready_reg;


  //----------------------------------------------------------------
  // reg_update
  //----------------------------------------------------------------
  always @(posedge clk) begin : reg_update
    if (!reset_n) begin
      ready_reg <= 1'h0;
    end
    else begin
      ready_reg <= cs;
    end
  end  // reg_update


  //----------------------------------------------------------------
  // rom_logic
  //----------------------------------------------------------------
  always @* begin : rom_logic
    /* verilator lint_off WIDTH */
    rom_rdata = memory[address];
    /* verilator lint_on WIDTH */
  end

endmodule  // rom

//======================================================================
// EOF rom.v
//======================================================================
