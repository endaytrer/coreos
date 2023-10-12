#include "file.h"
#include "printk.h"
#include "fs_file.h"

i64 wrapped_read(File *file, u64 offset, void *buf, u64 size) {
    if (!(file->permission & PERMISSION_R)) {   
        printk("[kernel] read on unreadable file\n");
        return -1;
    }
    if (offset + size > *file->size) {
        return -1;
    }
    return file->read(file->super, offset, buf, size);
}

i64 wrapped_write(File *file, u64 offset, const void *buf, u64 size) {
    if (!(file->permission & PERMISSION_W)) {   
        printk("[kernel] write on unwritable file\n");
        return -1;
    }
    if (offset + size > *file->size) {
        printk("[kernel] write to large\n");
        return -1;
    }
    return file->write(file->super, offset, buf, size);
}

i64 trunc_file(File *file, u64 size) {
    if (file->type != DEVICE)
        return fs_file_truncate(file->super, size); // is a fs_file

    return -1;
}