#include "storage_sd_api.h"

const char *sd_api_get_fs_type_text(SDFsType fs_type)
{
    switch (fs_type) {
    case (FST_FAT12):
        return "FAT12";
    case (FST_FAT16):
        return "FAT16";
    case (FST_FAT32):
        return "FAT32";
    case (FST_EXFAT):
        return "EXFAT";
    default:
        return "UNKNOWN";
    }
}
