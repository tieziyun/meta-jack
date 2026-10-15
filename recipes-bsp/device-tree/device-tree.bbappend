FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# pynq-z2.dts does '#include "zynq-7000.dtsi"' straight from the kernel source
# tree instead of carrying a copy. meta-xilinx's device-tree.bb drops the
# kernel paths (its flow is kernel-independent): it blanks KERNEL_INCLUDE and
# sets DT_INCLUDE to DT_FILES_PATH only. Restore both to the devicetree.bbclass
# defaults. A non-empty KERNEL_INCLUDE also makes the class auto-depend on
# virtual/kernel, so the kernel source tree is guaranteed present.
# arch/${ARCH}/boot/dts/* globs in the xilinx/ subdir where zynq-7000.dtsi
# lives; include-prefixes provides <dt-bindings/...> if we ever want macros.
KERNEL_INCLUDE = " \
    ${STAGING_KERNEL_DIR}/arch/${ARCH}/boot/dts \
    ${STAGING_KERNEL_DIR}/arch/${ARCH}/boot/dts/* \
    ${STAGING_KERNEL_DIR}/scripts/dtc/include-prefixes \
"
DT_INCLUDE = "${DT_FILES_PATH} ${KERNEL_INCLUDE}"

# Our single custom dtsi (board + PL). EXTRA_DT_FILES copies it into
# DT_FILES_PATH so pynq-z2.dts's #include resolves; being a .dtsi it is not
# compiled standalone.
EXTRA_DT_FILES += "pynq-z2-pl.dtsi"
