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
DSTATUS disk_initialize(BYTE drive)
{
    assert(drive == 1);
    return 0;
}
DSTATUS disk_status(BYTE drive)
{
    assert(drive == 1);
    return 0;
}
DRESULT disk_read(BYTE drive, BYTE *data, DWORD sector, UINT count)
{
    assert(drive == 1);
    if (fail_disk)
        return RES_ERROR;
    assert(sector <= SECTORS && count <= SECTORS - sector);
    memcpy(data, disk_data + sector * 512, (size_t)count * 512);
    return RES_OK;
}
DRESULT disk_write(BYTE drive, const BYTE *data, DWORD sector, UINT count)
{
    assert(drive == 1);
    if (fail_disk)
        return RES_ERROR;
    assert(sector <= SECTORS && count <= SECTORS - sector);
    memcpy(disk_data + sector * 512, data, (size_t)count * 512);
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *data)
{
    assert(drive == 1);
    switch (command) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(DWORD *)data = SECTORS;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)data = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
DWORD get_fattime(void)
{
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
enum { StorageStatusNotReady, StorageStatusNotMounted, StorageStatusOK };
typedef struct {
    FATFS *fs;
    const char *path;
    bool sd_was_present;
} SDData;
typedef struct {
    SDData *data;
    int status;
    const void *fs_api;
} StorageData;
static void *mnt_image;
static StorageData *mnt_image_storage;
static const int mnt_driver;
static const int fs_api;
static size_t fs_allocations;

static void *tracked_malloc(size_t size)
{
    if (size == sizeof(FATFS))
        fs_allocations++;
    void *data = calloc(1, size);
    assert(data);
    return data;
}
static char *test_strdup(const char *source)
{
    size_t size = strlen(source) + 1;
    char *data = malloc(size);
    assert(data);
    memcpy(data, source, size);
    return data;
}
static int FATFS_LinkDriver(const int *driver, char *path)
{
    assert(driver == &mnt_driver);
    memcpy(path, "1:/", 4);
    return 0;
}

static size_t image_position;
static size_t transfer_calls;
static size_t seek_calls;
static size_t fail_transfer = SIZE_MAX;
static bool fail_seek;
static bool storage_ext_file_seek(void *storage, void *file, uint32_t offset, bool from_start)
{
    (void)storage;
    (void)file;
    assert(from_start);
    seek_calls++;
    image_position = offset;
    return !fail_seek;
}
static uint16_t storage_ext_file_read(void *storage, void *file, void *data, uint16_t size)
{
    (void)storage;
    (void)file;
    assert(size && size % 512 == 0 && image_position + size <= sizeof(disk_data));
    if (transfer_calls++ == fail_transfer)
        return size - 1;
    memcpy(data, disk_data + image_position, size);
    image_position += size;
    return size;
}
static uint16_t storage_ext_file_write(void *storage, void *file, const void *data, uint16_t size)
{
    (void)storage;
    (void)file;
    assert(size && size % 512 == 0 && image_position + size <= sizeof(disk_data));
    if (transfer_calls++ == fail_transfer)
        return size - 1;
    memcpy(disk_data + image_position, data, size);
    image_position += size;
    return size;
}
#define UNUSED(value) (void)(value)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define SCSI_BLOCK_SIZE 512UL
#define malloc tracked_malloc
#define strdup test_strdup
/* PRODUCTION_CODE */
#undef malloc
#undef strdup

static void test_transfers(void)
{
    const UINT counts[] = {1, 127, 128, 129, 257};
    uint8_t *data = malloc(257 * 512);
    assert(data);
    for (size_t c = 0; c < sizeof(counts) / sizeof(counts[0]); c++) {
        const size_t size = counts[c] * 512U;
        for (size_t i = 0; i < size; i++)
            data[i] = (uint8_t)(i * 13 + i / 512);
        seek_calls = transfer_calls = 0;
        assert(mnt_driver_write(1, data, 7, counts[c]) == RES_OK);
        assert(seek_calls == 1 && transfer_calls == (counts[c] + 126U) / 127U);
        assert(memcmp(disk_data + 7 * 512, data, size) == 0);
        memset(data, 0, size);
        seek_calls = transfer_calls = 0;
        assert(mnt_driver_read(1, data, 7, counts[c]) == RES_OK);
        assert(seek_calls == 1 && transfer_calls == (counts[c] + 126U) / 127U);
        for (size_t i = 0; i < size; i++)
            assert(data[i] == (uint8_t)(i * 13 + i / 512));
    }
    for (size_t failure = 0; failure < 3; failure++) {
        fail_transfer = failure;
        transfer_calls = 0;
        assert(mnt_driver_read(1, data, 7, 257) == RES_ERROR);
        assert(transfer_calls == failure + 1);
        transfer_calls = 0;
        assert(mnt_driver_write(1, data, 7, 257) == RES_ERROR);
        assert(transfer_calls == failure + 1);
    }
    fail_transfer = SIZE_MAX;
    fail_seek = true;
    transfer_calls = 0;
    assert(mnt_driver_read(1, data, 7, 128) == RES_ERROR);
    assert(mnt_driver_write(1, data, 7, 128) == RES_ERROR);
    assert(transfer_calls == 0);
    fail_seek = false;
    seek_calls = 0;
    assert(mnt_driver_read(1, NULL, 0, 0) == RES_PARERR);
    assert(mnt_driver_write(1, NULL, 0, 0) == RES_PARERR);
    assert(mnt_driver_read(1, NULL, UINT32_MAX / 512 + 1, 1) == RES_PARERR);
    assert(mnt_driver_write(1, NULL, UINT32_MAX / 512 + 1, 1) == RES_PARERR);
    assert(seek_calls == 0);
    free(data);
}

int main(void)
{
    StorageData storage = {.status = StorageStatusNotReady};
    storage_mnt_init(&storage);
    assert(storage.data->fs == NULL && fs_allocations == 0);
    assert(storage_process_virtual_mount(&storage) == FSE_NOT_READY);
    assert(fs_allocations == 0);

    storage.status = StorageStatusNotMounted;
    assert(storage_process_virtual_mount(&storage) == FSE_INVALID_PARAMETER);
    FATFS *stable_fs = storage.data->fs;
    assert(stable_fs && fs_allocations == 1);
    fail_disk = true;
    assert(storage_process_virtual_mount(&storage) == FSE_INTERNAL);
    fail_disk = false;

    uint8_t work[512];
    assert(f_mkfs("1:", FM_ANY | FM_SFD, 0, work, sizeof(work)) == FR_OK);
    for (size_t cycle = 0; cycle < 100; cycle++) {
        storage.status = StorageStatusNotMounted;
        assert(storage_process_virtual_mount(&storage) == FSE_OK);
        assert(storage_process_virtual_mount(&storage) == FSE_ALREADY_OPEN);
        assert(storage.data->fs == stable_fs && fs_allocations == 1);
        FIL file;
        assert(f_open(&file, "1:/long filename for lifetime regression.txt",
                      FA_CREATE_ALWAYS | FA_WRITE | FA_READ) == FR_OK);
        const char content[] = "FatFs virtual mount lifetime";
        UINT count = 0;
        assert(f_write(&file, content, sizeof(content), &count) == FR_OK &&
               count == sizeof(content));
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
    free((void *)storage.data->path);
    free(storage.data);
    test_transfers();
    puts("FatFs: deferred allocation, failed mounts, 100 real mount/file/unmount cycles passed");
    return 0;
}
