import ccip_if_pkg::*;
`include "vendor_defines.vh"
`include "cci_mpf_if.vh"



module vai_vtp_wrapper  
   (
    // CCI-P Clocks and Resets
    input  logic          pClk,                 // 400MHz - CCI-P clock domain. Primary interface clock
    input  logic          pClkDiv2,             // 200MHz - CCI-P clock domain.
    input  logic          pClkDiv4,             // 100MHz - CCI-P clock domain.
    input  logic          uClk_usr,             // User clock domain. Refer to clock programming guide  ** Currently provides fixed 300MHz clock **
    input  logic          uClk_usrDiv2,         // User clock domain. Half the programmed frequency  ** Currently provides fixed 150MHz clock **
    input  logic          pck_cp2af_softReset,  // CCI-P ACTIVE HIGH Soft Reset
    input  logic [1:0]    pck_cp2af_pwrState,   // CCI-P AFU Power State
    input  logic          pck_cp2af_error,      // CCI-P Protocol Error Detected

    // Interface structures
    input  t_if_ccip_Rx   pck_cp2af_sRx,        // CCI-P Rx Port
    output t_if_ccip_Tx   pck_af2cp_sTx,         // CCI-P Tx Port

    output	        t_if_ccip_Rx      afu_RxPort,  // to mgr rx port
    input     		t_if_ccip_Tx	  afu_TxPort		 	// from mgr tx port

    );

  
    localparam MPF_DFH_MMIO_ADDR = 'h800;

   
    cci_mpf_if fiu(.clk(pClk));

   
    ccip_wires_to_mpf
      #(
        // All inputs and outputs in PR region (AFU) must be registered!
        .REGISTER_INPUTS(1),
        .REGISTER_OUTPUTS(1)
        )
      map_ifc(.*);

    //
    // Instantiate MPF with the desired properties.
    //
    cci_mpf_if afu(.clk(pClk));

    cci_mpf
      #(
        
        .SORT_READ_RESPONSES(0),
        .PRESERVE_WRITE_MDATA(1),   
        .ENABLE_VTP(1),  
        .VTP_PT_MODE("SOFTWARE_SERVICE"),   
        .ENABLE_VC_MAP(0), 
        .ENABLE_DYNAMIC_VC_MAPPING(1),
        .ENFORCE_WR_ORDER(0),      
        .ENABLE_PARTIAL_WRITES(0),
        .DFH_MMIO_BASE_ADDR(MPF_DFH_MMIO_ADDR)
        )
      mpf
       (
        .clk(pClk),
        .fiu,
        .afu,
        .c0NotEmpty(),
        .c1NotEmpty()
        );


    // ====================================================================
    //
    //  Now CCI is exposed as an MPF interface through the object named
    //  "afu".  Two primary strategies are available for connecting
    //  a design to the interface:
    //
    //    (1) Use the MPF-provided constructor functions to generate
    //        CCI request structures and pass them directly to MPF.
    //        See, for example, cci_mpf_defaultReqHdrParams() and
    //        cci_c0_genReqHdr() in cci_mpf_if_pkg.sv.
    //
    //    (1) Map "afu" back to standard CCI wires.  This is the strategy
    //        used below to map an existing AFU to MPF.
    //
    // ====================================================================

    //
    // Convert MPF interfaces back to the standard CCI structures.
    //
    
    //
    // The cci_mpf module has already registered the Rx wires heading
    // toward the AFU, so wires are acceptable.
    //
    always_comb
    begin
        afu_RxPort.c0 = afu.c0Rx;
        afu_RxPort.c1 = afu.c1Rx;

        afu_RxPort.c0TxAlmFull = afu.c0TxAlmFull;
        afu_RxPort.c1TxAlmFull = afu.c1TxAlmFull;

        afu.c0Tx = cci_mpf_cvtC0TxFromBase(afu_TxPort.c0);
        if (cci_mpf_c0TxIsReadReq(afu.c0Tx))
        begin
            // Treat all addresses as virtual.
            afu.c0Tx.hdr.ext.addrIsVirtual = 1'b1;

            // Enable eVC_VA to physical channel mapping.  This will only
            // be triggered when ENABLE_VC_MAP is set above.
            afu.c0Tx.hdr.ext.mapVAtoPhysChannel = 1'b1;

            // Enforce load/store and store/store ordering within lines.
            // This will only be triggered when ENFORCE_WR_ORDER is set.
            afu.c0Tx.hdr.ext.checkLoadStoreOrder = 1'b1;
        end

        afu.c1Tx = cci_mpf_cvtC1TxFromBase(afu_TxPort.c1);
        if (cci_mpf_c1TxIsWriteReq(afu.c1Tx))
        begin
            // Treat all addresses as virtual.
            afu.c1Tx.hdr.ext.addrIsVirtual = 1'b1;

            // Enable eVC_VA to physical channel mapping.  This will only
            // be triggered when ENABLE_VC_MAP is set above.
            afu.c1Tx.hdr.ext.mapVAtoPhysChannel = 1'b1;

            // Enforce load/store and store/store ordering within lines.
            // This will only be triggered when ENFORCE_WR_ORDER is set.
            afu.c1Tx.hdr.ext.checkLoadStoreOrder = 1'b1;
        end

        afu.c2Tx = afu_TxPort.c2;
    end


   
endmodule