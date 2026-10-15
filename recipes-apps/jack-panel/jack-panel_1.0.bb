SUMMARY = "Phone-style Qt status panel for the PYNQ-Z2 HDMI display"
DESCRIPTION = "Fullscreen Qt Widgets UI (software-rendered via linuxfb) showing \
FPGA state, CPU load, memory and display IRQ rate. Autostarts on tty1's slot."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit qt6-cmake systemd

SRC_URI = " \
    file://CMakeLists.txt \
    file://main.cpp \
    file://jack-panel.service \
"

S = "${UNPACKDIR}"

DEPENDS = "qtbase"

do_install:append() {
    install -Dm 0644 ${UNPACKDIR}/jack-panel.service \
        ${D}${systemd_system_unitdir}/jack-panel.service
}

SYSTEMD_SERVICE:${PN} = "jack-panel.service"
SYSTEMD_AUTO_ENABLE = "enable"

FILES:${PN} += "${systemd_system_unitdir}/jack-panel.service"
