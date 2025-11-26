# owon\_dge20xx\_linux

This is a simple demo of sending SCPI commands to the Owon DGE2070 signal generator.

It configures the two channels, enables them for a few seconds, then turms them off.

Tested with OWON DGE2070, but should also work with OWON DGE2035.

NOTE: If you don't have permissions to the USB device you may need to run this with "sudo":

    $ sudo ./owon_dge 
    .. OWON DGE2070 found
    .. Turn channels off
    .. Configuure both channels
    .. Turn both channels on
    .. Turn both channels off

Largely based on owondump by Michael Murphy <ee07m060@elec.qmul.ac.uk>
