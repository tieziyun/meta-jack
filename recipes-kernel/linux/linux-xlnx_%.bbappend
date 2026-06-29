FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append = " file://xdp-bpf.cfg file://display.cfg"

do_configure:append() {
	cat ${WORKDIR}/display.cfg >> ${B}/.config
	oe_runmake -C ${B} oldconfig
}
