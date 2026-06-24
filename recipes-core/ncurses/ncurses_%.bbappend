SRC_URI:remove = "git://github.com/ThomasDickey/ncurses-snapshots.git;protocol=https;branch=master"
SRC_URI:prepend = "https://ftp.gnu.org/gnu/ncurses/ncurses-6.4.tar.gz "

SRC_URI[sha256sum] = "6931283d9ac87c5073f30b6290c4c75f21632bb4fc3603ac8100812bed248159"

S = "${WORKDIR}/ncurses-6.4"
