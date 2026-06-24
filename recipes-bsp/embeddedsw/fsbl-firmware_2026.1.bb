require recipes-bsp/embeddedsw/fsbl-firmware.inc

SKIP_RECIPE[fsbl-firmware] = ""

inherit xlnx-embeddedsw deploy

ESW_VER = "2026.1"
LIC_FILES_CHKSUM = "file://license.txt;md5=${ESW_LIC_FILES_CHKSUM}"
SRC_URI = "${EMBEDDEDSW_SRCURI}"
SRCREV = "${SRCREV_embeddedsw}"

PARALLEL_MAKE = "-j1"

COMPILER_FLAGS     = "-O2 -c"
EXTRA_COMPILER_FLAGS = "-g -Wall -Wextra"

# FSBL standalone library uses conditional ARM instructions (strexeq) that are
# illegal in Thumb mode without IT blocks.
ARM_INSTRUCTION_SET:eabi:arm = "arm"

def bsp_make_vars(d):
    cc   = d.getVar('CC')
    ar   = d.getVar('AR')
    dump = d.getVar('OBJDUMP') + ' -xSD'
    return ('"COMPILER=%(cc)s" "CC=%(cc)s" "COMPILER_FLAGS=%(cf)s" '
            '"EXTRA_COMPILER_FLAGS=%(ecf)s" "ARCHIVER=%(ar)s" "AR=%(ar)s" '
            '"AS=%(as_)s" "LINKER=%(cc)s" "DUMP=%(dump)s"') % {
        'cc': cc, 'cf': d.getVar('COMPILER_FLAGS'),
        'ecf': d.getVar('EXTRA_COMPILER_FLAGS'),
        'ar': ar, 'as_': cc, 'dump': dump,
    }

EXTRA_OEMAKE = "${@bsp_make_vars(d)}"

B = "${S}/lib/sw_apps/zynq_fsbl/src"

FSBL_IMAGE_NAME  ?= "fsbl-${MACHINE}"
FSBL_BASE_NAME   ?= "${FSBL_IMAGE_NAME}-${PKGE}-${PKGV}-${PKGR}-${MACHINE}${IMAGE_VERSION_SUFFIX}"

PACKAGES = ""
ESW_COMPONENT = "fsbl.elf"

do_configure() {
    local MISC="${B}/../misc"
    local BOARD_DIR="${MISC}/pynq-z2"

    cp -r ${MISC}/zc702 ${BOARD_DIR}

    python3 -c "
import zipfile, xml.etree.ElementTree as ET

xsa  = '${HDF_PATH}'
dest = '${BOARD_DIR}'

with zipfile.ZipFile(xsa) as z:
    for name in ['ps7_init.c', 'ps7_init.h', 'ps7_init_gpl.c', 'ps7_init_gpl.h']:
        if name in z.namelist():
            z.extract(name, dest)
    with z.open('design_1.hwh') as f:
        root = ET.fromstring(f.read())

ps7 = next(m for m in root.findall('MODULES/MODULE')
           if m.get('MODTYPE') == 'processing_system7')

def pcw(name):
    for p in ps7.findall('PARAMETERS/PARAMETER'):
        if p.get('NAME') == name:
            return p.get('VALUE', '')
    return ''

uart_base = pcw('PCW_UART0_BASEADDR')
uart_clk  = int(float(pcw('PCW_ACT_UART_PERIPHERAL_FREQMHZ')) * 1e6)
ddr_high  = pcw('PCW_DDR_RAM_HIGHADDR')

assert pcw('PCW_UART0_PERIPHERAL_ENABLE') == '1', 'UART0 not enabled in XSA'

xpar = open(dest + '/xparameters.h').read()
xpar = xpar.replace('0xE0001000', uart_base)
xpar = xpar.replace('XPAR_PS7_UART_1_UART_CLK_FREQ_HZ 50000000',
                    f'XPAR_PS7_UART_0_UART_CLK_FREQ_HZ {uart_clk}')
xpar = xpar.replace('XPAR_XUARTPS_0_UART_CLK_FREQ_HZ 50000000',
                    f'XPAR_XUARTPS_0_UART_CLK_FREQ_HZ {uart_clk}')
xpar = xpar.replace('PS7_UART_1', 'PS7_UART_0')
xpar = xpar.replace('0x3FFFFFFF', ddr_high)
open(dest + '/xparameters.h', 'w').write(xpar)
"

    ${MISC}/copy_bsp.sh pynq-z2 arm-none-eabi-gcc
}

do_compile() {
    local MISC="${B}/../misc"
    oe_runmake -C ${MISC} ${EXTRA_OEMAKE}
    oe_runmake -C ${B} BOARD=pynq-z2 ${EXTRA_OEMAKE}
}

do_install() {
    :
}

do_deploy() {
    install -Dm 0644 ${B}/${ESW_COMPONENT} ${DEPLOYDIR}/${FSBL_BASE_NAME}.elf
    ln -sf ${FSBL_BASE_NAME}.elf ${DEPLOYDIR}/${FSBL_IMAGE_NAME}.elf
}

addtask deploy before do_build after do_install

INSANE_SKIP:${PN} += "buildpaths"
