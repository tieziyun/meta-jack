SUMMARY = "WiFi client setup for the RTL8188EUS USB dongle"
DESCRIPTION = "Pulls in the rtl8xxxu driver, its firmware and wpa_supplicant, \
names the dongle wlan0 and brings it up with DHCP via systemd-networkd."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://10-wlan0.link \
    file://30-wlan0.network \
"

S = "${UNPACKDIR}"

# Credentials come from the gitignored conf/jack-local.conf. Left empty, the
# image boots with no network configured; add one on the board with wpa_cli
# (update_config=1 makes save_config persist it).
WIFI_SSID ?= ""
WIFI_PSK ?= ""

# Rebuild when the credentials change, but record only a digest in the task
# signature: bitbake writes signature inputs in plain text to sstate siginfo.
WIFI_DIGEST = "${@__import__('hashlib').sha256((d.getVar('WIFI_SSID') + '\0' + d.getVar('WIFI_PSK')).encode()).hexdigest()}"
WIFI_DIGEST[vardepvalue] = "${WIFI_DIGEST}"
WIFI_DIGEST[vardepsexclude] = "WIFI_SSID WIFI_PSK"
do_compile[vardeps] += "WIFI_DIGEST"
do_compile[vardepsexclude] += "WIFI_SSID WIFI_PSK"

python do_compile() {
    import hashlib, os
    ssid = d.getVar('WIFI_SSID')
    psk = d.getVar('WIFI_PSK')
    conf = [
        'ctrl_interface=/run/wpa_supplicant',
        'update_config=1',
        '',
    ]
    if ssid:
        # Store the derived PSK, not the passphrase (same as wpa_passphrase)
        key = hashlib.pbkdf2_hmac('sha1', psk.encode(), ssid.encode(), 4096, 32)
        conf += ['network={', '\tssid="%s"' % ssid, '\tpsk=%s' % key.hex(), '}', '']
    with open(os.path.join(d.getVar('B'), 'wpa_supplicant-wlan0.conf'), 'w') as f:
        f.write('\n'.join(conf))
}

do_install() {
    install -Dm 0600 ${B}/wpa_supplicant-wlan0.conf \
        ${D}${sysconfdir}/wpa_supplicant/wpa_supplicant-wlan0.conf
    install -Dm 0644 ${UNPACKDIR}/10-wlan0.link \
        ${D}${sysconfdir}/systemd/network/10-wlan0.link
    install -Dm 0644 ${UNPACKDIR}/30-wlan0.network \
        ${D}${sysconfdir}/systemd/network/30-wlan0.network

    # Start wpa_supplicant when wlan0 appears rather than from multi-user:
    # with the dongle unplugged, a multi-user dependency would stall boot
    # for the 90 s device timeout.
    install -d ${D}${sysconfdir}/systemd/system/sys-subsystem-net-devices-wlan0.device.wants
    ln -sf ${systemd_system_unitdir}/wpa_supplicant@.service \
        ${D}${sysconfdir}/systemd/system/sys-subsystem-net-devices-wlan0.device.wants/wpa_supplicant@wlan0.service
}

FILES:${PN} = "${sysconfdir}"
CONFFILES:${PN} = "${sysconfdir}/wpa_supplicant/wpa_supplicant-wlan0.conf"

RDEPENDS:${PN} = " \
    wpa-supplicant \
    iw \
    kernel-module-rtl8xxxu \
    linux-firmware-rtl8188 \
    wireless-regdb-static \
"
