# Break a circular dependency in meta-xilinx:
# u-boot-xlnx-uenv copies EXTRA_IMAGEDEPENDS into its DEPENDS
# (remove_task_from_depends), which includes the qemu-image-empty *image*
# (machine-xilinx-qemu.inc). That image in turn depends on this recipe via
# uEnv.txt in IMAGE_BOOT_FILES (machine-xilinx-default.inc WKS_FILE_DEPENDS),
# closing a loop through the SPDX tasks. uEnv generation needs nothing from
# qemu-image-empty, so drop the edge.
DEPENDS:remove = "qemu-image-empty"
