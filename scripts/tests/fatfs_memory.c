#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ff.h>
#include <diskio.h>

#define SECTORS 4096
static uint8_t disk_data[SECTORS * 512];
static bool fail_disk;
DSTATUS disk_initialize(BYTE drive) {
    assert(drive == 1);
    return 0;
}
DSTATUS disk_status(BYTE drive) {
    assert(drive == 1);
    return 0;
}
DRESULT disk_read(BYTE drive, BYTE* data, DWORD sector, UINT count) {
    assert(drive == 1);
    if(fail_disk) return RES_ERROR;
    assert(sector <= SECTORS && count <= SECTORS - sector);
    memcpy(data, disk_data + sector * 512, (size_t)count * 512);
    return RES_OK;
}
DRESULT disk_write(BYTE drive, const BYTE* data, DWORD sector, UINT count) {
    assert(drive == 1);
    if(fail_disk) return RES_ERROR;
    assert(sector <= SECTORS && count <= SECTORS - sector);
    memcpy(disk_data + sector * 512, data, (size_t)count * 512);
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void* data) {
    assert(drive == 1);
    switch(command) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(DWORD*)data = SECTORS;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD*)data = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
DWORD get_fattime(void) {
    return (2026U - 1980) << 25 | 1 << 21 | 1 << 16;
}

typedef FRESULT SDError;
typedef enum {
    FSE_OK,
    FSE_ALREADY_OPEN,
    FSE_NOT_READY,
    FSE_INVALID_PARAMETER,
    FSE_INTERNAL
} FS_Error;
enum {
    StorageStatusNotReady,
    StorageStatusNotMounted,
    StorageStatusOK
};
typedef struct {
    FATFS* fs;
    const char* path;
    bool sd_was_present;
} SDData;
typedef struct {
    SDData* data;
    int status;
    const void* fs_api;
} StorageData;
static void* mnt_image;
static StorageData* mnt_image_storage;
static const int mnt_driver;
static const int fs_api;
static size_t fs_allocations;

static void* tracked_malloc(size_t size) {
    if(size == sizeof(FATFS)) fs_allocations++;
    void* data = calloc(1, size);
    assert(data);
    return data;
}
static char* test_strdup(const char* source) {
    size_t size = strlen(source) + 1;
    char* data = malloc(size);
    assert(data);
    memcpy(data, source, size);
    return data;
}
static int FATFS_LinkDriver(const int* driver, char* path) {
    assert(driver == &mnt_driver);
    memcpy(path, "1:/", 4);
    return 0;
}

#define malloc tracked_malloc
#define strdup test_strdup
/* PRODUCTION_CODE */
#undef malloc
#undef strdup

int main(void) {
    StorageData storage = {.status = StorageStatusNotReady};
    storage_mnt_init(&storage);
    assert(storage.data->fs == NULL && fs_allocations == 0);
    assert(storage_process_virtual_mount(&storage) == FSE_NOT_READY);
    assert(fs_allocations == 0);

    storage.status = StorageStatusNotMounted;
    assert(storage_process_virtual_mount(&storage) == FSE_INVALID_PARAMETER);
    FATFS* stable_fs = storage.data->fs;
    assert(stable_fs && fs_allocations == 1);
    fail_disk = true;
    assert(storage_process_virtual_mount(&storage) == FSE_INTERNAL);
    fail_disk = false;

    uint8_t work[512];
    assert(f_mkfs("1:", FM_ANY | FM_SFD, 0, work, sizeof(work)) == FR_OK);
    for(size_t cycle = 0; cycle < 100; cycle++) {
        storage.status = StorageStatusNotMounted;
        assert(storage_process_virtual_mount(&storage) == FSE_OK);
        assert(storage_process_virtual_mount(&storage) == FSE_ALREADY_OPEN);
        assert(storage.data->fs == stable_fs && fs_allocations == 1);
        FIL file;
        assert(
            f_open(
                &file,
                "1:/long filename for lifetime regression.txt",
                FA_CREATE_ALWAYS | FA_WRITE | FA_READ) == FR_OK);
        const char content[] = "FatFs virtual mount lifetime";
        UINT count = 0;
        assert(
            f_write(&file, content, sizeof(content), &count) == FR_OK && count == sizeof(content));
        assert(f_sync(&file) == FR_OK);
        assert(f_lseek(&file, 0) == FR_OK);
        char copy[sizeof(content)];
        assert(f_read(&file, copy, sizeof(copy), &count) == FR_OK && count == sizeof(copy));
        assert(memcmp(content, copy, sizeof(content)) == 0);

        // An outstanding file still points at the filesystem after unmount.
        assert(storage_process_virtual_unmount(&storage) == FSE_OK);
        assert(f_read(&file, copy, sizeof(copy), &count) == FR_INVALID_OBJECT);
        assert(f_close(&file) == FR_INVALID_OBJECT);
        assert(storage_process_virtual_unmount(&storage) == FSE_NOT_READY);
        assert(storage_process_virtual_quit(&storage) == FSE_OK);
        assert(storage.data->fs == stable_fs && fs_allocations == 1);
    }
    assert(f_mount(NULL, "1:", 0) == FR_OK);
    free(storage.data->fs);
    free((void*)storage.data->path);
    free(storage.data);
    puts("FatFs: deferred allocation, failed mounts, 100 real mount/file/unmount cycles passed");
    return 0;
}
