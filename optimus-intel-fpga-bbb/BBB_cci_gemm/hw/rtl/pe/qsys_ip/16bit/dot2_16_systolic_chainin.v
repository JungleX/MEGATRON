// dot2_16_systolic_chainin.v

// Generated using ACDS version 15.1 193

`timescale 1 ps / 1 ps
module dot2_16_systolic_chainin (
		input  wire        accumulate, // accumulate.accumulate
		input  wire [1:0]  aclr,       //       aclr.aclr
		input  wire [15:0] ax,         //         ax.ax
		input  wire [15:0] ay,         //         ay.ay
		input  wire [15:0] bx,         //         bx.bx
		input  wire [15:0] by,         //         by.by
		input  wire [63:0] chainin,    //    chainin.chainin
		input  wire [2:0]  clk,        //        clk.clk
		input  wire [2:0]  ena,        //        ena.ena
		input  wire        loadconst,  //  loadconst.loadconst
		input  wire        negate,     //     negate.negate
		output wire [33:0] resulta,    //    resulta.resulta
		input  wire        sub         //        sub.sub
	);

	dot2_16_systolic_chainin_altera_a10_native_fixed_point_dsp_151_a74gu7y a10_native_fixed_point_dsp_0 (
		.ay         (ay),         //         ay.ay
		.by         (by),         //         by.by
		.ax         (ax),         //         ax.ax
		.bx         (bx),         //         bx.bx
		.chainin    (chainin),    //    chainin.chainin
		.sub        (sub),        //        sub.sub
		.negate     (negate),     //     negate.negate
		.accumulate (accumulate), // accumulate.accumulate
		.loadconst  (loadconst),  //  loadconst.loadconst
		.resulta    (resulta),    //    resulta.resulta
		.clk        (clk),        //        clk.clk
		.ena        (ena),        //        ena.ena
		.aclr       (aclr)        //       aclr.aclr
	);

endmodule
