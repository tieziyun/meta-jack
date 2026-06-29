SUMMARY = "Digilent HDMI encoder DRM driver"
LICENSE = "CLOSED"

inherit module

SRC_URI = "file://Makefile file://digilent-hdmi.c"

S = "${WORKDIR}"
