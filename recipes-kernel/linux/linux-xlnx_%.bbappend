FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append = " file://xdp-bpf.cfg file://display.cfg file://debug.cfg"

# xlnx DRM master filters components by a hardcoded compatible whitelist;
# out-of-tree digilent,hdmi encoder must be added or it never binds.
SRC_URI:append = " file://0001-drm-xlnx-allow-digilent-hdmi-component.patch"

do_configure:append() {
	cat ${WORKDIR}/display.cfg >> ${B}/.config
	cat ${WORKDIR}/debug.cfg >> ${B}/.config
	oe_runmake -C ${B} oldconfig
}
