# device-tree.bb names the output DTB after CONFIG_DTFILE's basename (pynq-z2.dtb).
# machine-xilinx-zynq.inc expects system-top.dtb — redirect to our actual output.
BIF_PARTITION_IMAGE[device-tree] = "${RECIPE_SYSROOT}/boot/devicetree/pynq-z2.dtb"
