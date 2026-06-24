FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# Inject system-user.dtsi — the recipe appends #include "system-user.dtsi" to the BASE_DTS.
# Add custom PL IP nodes in files/system-user.dtsi (base addresses, interrupts, compatible strings).
EXTRA_DT_INCLUDE_FILES += "system-user.dtsi"
SRC_URI:append = " file://system-user.dtsi"
