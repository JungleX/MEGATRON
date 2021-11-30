# MEGATRON
This work was accepted by DAC 2021

There is a mistake in the published paper. The paper says "we believe 
that MMIO write latency is a little higher than PCIe read latency, 
since they share the same transaction path while MMIO needs extra CPU processing."
Indeed, it should be "MMIO is a little higher than half of the PCIe read latency."
