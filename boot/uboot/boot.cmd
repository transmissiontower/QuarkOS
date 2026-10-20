echo Sodium: loading kernel from virtio FAT disk
virtio scan
setenv kernel_load_addr 0x42000000
if fatload virtio 0:1 ${kernel_load_addr} SODIUM.BIN; then
    cp.b ${kernel_load_addr} 0x40010000 ${filesize}
    echo Sodium: starting kernel at 0x40010000
    go 0x40010000
fi
echo Sodium: kernel load failed
