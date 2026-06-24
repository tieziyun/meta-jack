# lsfd.c has a compound literal with a trailing comma that the preprocessor
# misreads as 6 args to the bsearch macro. Disable lsfd — not needed on embedded.
EXTRA_OECONF:append = " --disable-lsfd"
