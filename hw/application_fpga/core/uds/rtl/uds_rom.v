//======================================================================
//
// uds_rom.v
// ---------
// UDS rom. Generated by instantiating named SB_LUT4 resources.
// Note: This makes the design technology specific.
//
//
// Author: Claire Xenia Wolf
// Copyright (C) 2023 - YosysHQ, Tillitis AB
// SPDX-License-Identifier: GPL-2.0-only
//
//======================================================================

`default_nettype none

module uds_rom(
	       input wire [2:0] addr,
	       input wire re,
	       output wire [31:0] data
	      );

  generate
    genvar ii;
    for (ii = 0; ii < 32; ii = ii + 1'b1) begin: luts
    (* uds_rom_idx=ii, keep *) SB_LUT4
      #(
        .LUT_INIT({8'ha6 ^ ii[7:0], 8'h00})
      ) lut_i (
        .I0(addr[0]), .I1(addr[1]), .I2(addr[2]), .I3(re),
        .O(data[ii])
      );
    end
  endgenerate
endmodule // uds_rom

//======================================================================
// EOF uds_rom.v
//======================================================================