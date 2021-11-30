// fifo.v

// Generated using ACDS version 17.0 290

`timescale 1 ps / 1 ps
module fifo (
		input  wire [511:0] data,  //  fifo_input.datain
		input  wire         wrreq, //            .wrreq
		input  wire         rdreq, //            .rdreq
		input  wire         clock, //            .clk
		output wire [511:0] q,     // fifo_output.dataout
		output wire [4:0]   usedw, //            .usedw
		output wire         full,  //            .full
		output wire         empty  //            .empty
	);

	fifo_fifo_170_qq322xi fifo_0 (
		.data  (data),  //  fifo_input.datain
		.wrreq (wrreq), //            .wrreq
		.rdreq (rdreq), //            .rdreq
		.clock (clock), //            .clk
		.q     (q),     // fifo_output.dataout
		.usedw (usedw), //            .usedw
		.full  (full),  //            .full
		.empty (empty)  //            .empty
	);

endmodule