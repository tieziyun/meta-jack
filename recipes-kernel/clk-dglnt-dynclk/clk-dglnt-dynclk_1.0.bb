SUMMARY = "Recipe for  build an external clk-dglnt-dynclk Linux kernel module"
SECTION = "PETALINUX/modules"
LICENSE = "CLOSED"

inherit module

INHIBIT_PACKAGE_STRIP = "1"

SRC_URI = "file://Makefile \
           file://clk-dglnt-dynclk.c"

S = "${WORKDIR}"

