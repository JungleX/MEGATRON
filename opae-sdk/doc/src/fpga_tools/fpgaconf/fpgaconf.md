# fpgaconf #

## SYNOPSIS ##

`fpgaconf [-hvn] [-b <bus>] [-d <device>] [-f <function>] [-s <socket>] <gbs>`

## DESCRIPTION ##

fpgaconf configures the FPGA with the AF. It also checks the AF for compatibility with 
the targeted FPGA and the FPGA Interface Manager (FIM). fpgaconf takes the
following arguments:

`-h, --help`

	Prints usage information.

`-v, --verbose`

	Prints more verbose messages while enumerating and configuring. Can be
	requested more than once.

`-n, --dry-run`

	Performs enumeration. Skips any operations with side-effects such as the
	actual AF configuration. 

`-b, --bus`

	PCIe bus number of the target FPGA.

`-d, --device`

	PCIe device number of the target FPGA. 

`-f, --function`

	PCIe function number of the target FPGA.

`-s, --socket`

	Socket number of the target FPGA.

fpgaconf enumerates available FPGA devices in the system and selects
compatible FPGAs for configuration. If more than one FPGA is
compatible with the AF, fpgaconf exits and asks you to be
more specific in selecting the target FPGAs by specifying a
socket number or a PCIe BDF.

## EXAMPLES ##

`fpgaconf my_af.gbs`

	Program "my_af.gbs" to a compatible FPGA.

`fpgaconf -v -s 0 my_af.gbs`

	Program "my_af.gbs" to the FPGA in socket 0, if compatible,
	while printing out slightly more verbose information.
