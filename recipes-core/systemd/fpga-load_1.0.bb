SUMMARY = "FPGA bitstream loader service"
DESCRIPTION = "Systemd service to auto-load FPGA bitstream via fpga-manager on boot"
LICENSE = "CLOSED"



inherit systemd

SRC_URI = "file://fpga-load.service"
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SYSTEMD_SERVICE:${PN} = "fpga-load.service"

do_install() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/fpga-load.service ${D}${systemd_system_unitdir}/
}

FILES:${PN} = "${systemd_system_unitdir}/fpga-load.service"
