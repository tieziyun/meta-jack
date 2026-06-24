SUMMARY = "Out-of-tree kernel module for the AXI-Stream PL packet filter"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=12f884d2ae1ff87c09e5b7ccc2c4ca7e"

inherit module

SRC_URI = "file://."

S = "${WORKDIR}"

# Module source lives at the root of this recipe's files/ directory.
# Required files: Makefile, xdp-pl-filter.c, COPYING
