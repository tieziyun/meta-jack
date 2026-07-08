SUMMARY = "FPGA bitstream extracted from design XSA"
LICENSE = "CLOSED"

inherit deploy

DEPENDS = "virtual/bootloader bootgen-native"

S = "${WORKDIR}"

BITSTREAM_NAME ?= "${MACHINE}"
BITSTREAM_BASE_NAME ?= "${BITSTREAM_NAME}${IMAGE_VERSION_SUFFIX}"

PROVIDES = "virtual/bitstream"

# Extract bitstream from XSA (ZIP archive) and convert to byte-swapped .bin for fpga-manager
do_compile() {
	xsa="${HDF_PATH}"

	if [ ! -f "$xsa" ]; then
		bberror "XSA file not found: $xsa"
		exit 1
	fi

	python3 -c "
import zipfile, shutil, os

xsa = '${HDF_PATH}'
dest = '${S}'

with zipfile.ZipFile(xsa) as z:
	for name in z.namelist():
		if name.endswith('.bit'):
			# normalize to a fixed name regardless of design/wrapper naming
			with z.open(name) as src, open(os.path.join(dest, 'design_1_wrapper.bit'), 'wb') as out:
				shutil.copyfileobj(src, out)
			break
	else:
		raise FileNotFoundError('No .bit file found in XSA')
"

	# Convert .bit to byte-swapped .bin using bootgen
	# -process_bitstream bin: extract and byte-swap bitstream (not full boot image)
	bitfile="${S}/design_1_wrapper.bit"
	binfile="${S}/design_1_wrapper.bin"
	biffile="${S}/bitstream.bif"

	# Create BIF file that bootgen can read (with absolute path)
	printf "all: {\n\t${S}/design_1_wrapper.bit\n}\n" > "$biffile"

	bootgen -image "$biffile" -arch zynq -process_bitstream bin -w on -o "$binfile"

	# bootgen appends .bin to input, so rename to expected name
	if [ -f "${bitfile}.bin" ] && [ ! -f "$binfile" ]; then
		mv "${bitfile}.bin" "$binfile"
	fi
}

do_install() {
	binfile="${S}/design_1_wrapper.bin"
	if [ ! -f "$binfile" ]; then
		bberror "Byte-swapped bitstream not found"
		exit 1
	fi

	# Install .bin to /lib/firmware for fpga-manager runtime loading
	install -Dm 0644 "$binfile" ${D}${nonarch_base_libdir}/firmware/design_1_wrapper.bin

	# Install to /boot root: the wks builds the FAT boot partition from rootfs
	# /boot, and U-Boot (uEnv.txt loadfpga) fatloads this exact name from the
	# FAT root. Keep flat — subdirectories break u-boot-xlnx-scr's sed.
	install -Dm 0644 "$binfile" ${D}/boot/${BITSTREAM_NAME}.bin
}

do_deploy() {
	binfile="${S}/design_1_wrapper.bin"
	if [ -f "$binfile" ]; then
		# Deploy with design name
		install -Dm 0644 "$binfile" ${DEPLOYDIR}/design_1_wrapper.bin
		# Also create symlink with machine name
		ln -sf design_1_wrapper.bin ${DEPLOYDIR}/${BITSTREAM_NAME}.bin
	fi

}

addtask deploy before do_build after do_compile

FILES:${PN} = "${nonarch_base_libdir}/firmware/ /boot/${BITSTREAM_NAME}.bin"
