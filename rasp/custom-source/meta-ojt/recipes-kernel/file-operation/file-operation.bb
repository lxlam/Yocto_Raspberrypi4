SUMMARY = "Example Linux kernel module using file_operations"
DESCRIPTION = "A simple character device driver for learning Linux device drivers"
DEPENDS = ""
LICENSE = "CLOSED"

inherit module

SRC_URI = "\
    file://file_operation_driver.c \
    file://Makefile \
"
S="${WORKDIR}" 

do_install() {
    install -d ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra

    install -m 0644 \
        ${B}/file_operation_driver.ko \
        ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra/
}