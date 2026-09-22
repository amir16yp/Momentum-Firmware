#include "tar_archive.h"

#include <microtar.h>
#include <storage/storage.h>
#include <furi.h>
#include <toolbox/path.h>
#include <toolbox/compress.h>

#define TAG "TarArch"

#define MAX_NAME_LEN    254
#define FILE_BLOCK_SIZE (10 * 1024)

#define FILE_OPEN_NTRIES      10
#define FILE_OPEN_RETRY_DELAY 25

TarOpenMode tar_archive_get_mode_for_path(const char* path) {
    char ext[8];

    FuriString* path_str = furi_string_alloc_set_str(path);
    path_extract_extension(path_str, ext, sizeof(ext));
    TarOpenMode open_mode;

    if(strcmp(ext, ".ths") == 0) {
        open_mode = TarOpenModeReadHeatshrink;
    } else if(strcmp(ext, ".tgz") == 0 || furi_string_end_with(path_str, ".tar.gz")) {
        open_mode = TarOpenModeReadGzip;
    } else {
        open_mode = TarOpenModeRead;
    }

    furi_string_free(path_str);
    return open_mode;
}

typedef struct TarArchive {
    Storage* storage;
    File* stream;
    mtar_t tar;
    tar_unpack_file_cb unpack_cb;
    void* unpack_cb_context;

    tar_unpack_read_cb read_cb;
    void* read_cb_context;
} TarArchive;

/* Plain file backend - uncompressed, supports read and write */
static int mtar_storage_file_write(void* stream, const void* data, unsigned size) {
    uint16_t bytes_written = storage_file_write(stream, data, size);
    return (bytes_written == size) ? bytes_written : MTAR_EWRITEFAIL;
}

static int mtar_storage_file_read(void* stream, void* data, unsigned size) {
    uint16_t bytes_read = storage_file_read(stream, data, size);
    return (bytes_read == size) ? bytes_read : MTAR_EREADFAIL;
}

static int mtar_storage_file_seek(void* stream, unsigned offset) {
    bool res = storage_file_seek(stream, offset, true);
    return res ? MTAR_ESUCCESS : MTAR_ESEEKFAIL;
}

static int mtar_storage_file_close(void* stream) {
    if(stream) {
        storage_file_close(stream);
    }
    return MTAR_ESUCCESS;
}

const struct mtar_ops filesystem_ops = {
    .read = mtar_storage_file_read,
    .write = mtar_storage_file_write,
    .seek = mtar_storage_file_seek,
    .close = mtar_storage_file_close,
};

/* Compressed stream backend, read-only */

typedef struct {
    CompressType type;
    union {
        CompressConfigHeatshrink heatshrink;
        CompressConfigGzip gzip;
    } config;
    File* stream;
    CompressStreamDecoder* decoder;
    mtar_t* tar;
} CompressedStream;

/* HSDS 'heatshrink data stream' header magic */
static const uint32_t HEATSHRINK_MAGIC = 0x53445348;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t window_sz2;
    uint8_t lookahead_sz2;
} FURI_PACKED HeatshrinkStreamHeader;
_Static_assert(sizeof(HeatshrinkStreamHeader) == 7, "Invalid HeatshrinkStreamHeader size");

static int mtar_compressed_file_close(void* stream) {
    CompressedStream* compressed_stream = stream;
    if(compressed_stream) {
        if(compressed_stream->decoder) {
            compress_stream_decoder_free(compressed_stream->decoder);
        }
        storage_file_close(compressed_stream->stream);
        free(compressed_stream);
    }
    return MTAR_ESUCCESS;
}

static int mtar_compressed_file_read(void* stream, void* data, unsigned size) {
    CompressedStream* compressed_stream = stream;
    bool read_success = compress_stream_decoder_read(compressed_stream->decoder, data, size);
    return read_success ? (int)size : MTAR_EREADFAIL;
}

static int mtar_compressed_file_seek(void* stream, unsigned offset) {
    CompressedStream* compressed_stream = stream;
    bool success = false;
    if(offset == 0 && compress_stream_decoder_tell(compressed_stream->decoder) != 0) {
        uint32_t rewind_offset =
            compressed_stream->type == CompressTypeHeatshrink ? sizeof(HeatshrinkStreamHeader) : 0;
        success = storage_file_seek(compressed_stream->stream, rewind_offset, true) &&
                  compress_stream_decoder_rewind(compressed_stream->decoder);
    } else {
        size_t position = compress_stream_decoder_tell(compressed_stream->decoder);
        if(offset < position) return MTAR_ESEEKFAIL;
        success = true;
        // Microtar has already parsed the header into tar->header. Its raw header
        // buffer is idle during seeks, so use it to discard padding/skipped files.
        while(position < offset) {
            size_t count = MIN(offset - position, sizeof(compressed_stream->tar->buffer));
            if(!compress_stream_decoder_read(
                   compressed_stream->decoder, (uint8_t*)compressed_stream->tar->buffer, count)) {
                success = false;
                break;
            }
            position += count;
        }
    }
    return success ? MTAR_ESUCCESS : MTAR_ESEEKFAIL;
}

const struct mtar_ops compressed_ops = {
    .read = mtar_compressed_file_read,
    .write = NULL, // not supported
    .seek = mtar_compressed_file_seek,
    .close = mtar_compressed_file_close,
};

//////////////////////////////////////////////////////////////////////////

TarArchive* tar_archive_alloc(Storage* storage) {
    furi_check(storage);
    TarArchive* archive = malloc(sizeof(TarArchive));
    archive->storage = storage;
    archive->stream = storage_file_alloc(archive->storage);
    archive->unpack_cb = NULL;
    return archive;
}

static int32_t file_read_cb(void* context, uint8_t* buffer, size_t buffer_size) {
    File* file = context;
    return storage_file_read(file, buffer, buffer_size);
}

bool tar_archive_open(TarArchive* archive, const char* path, TarOpenMode mode) {
    furi_check(archive);
    FS_AccessMode access_mode;
    FS_OpenMode open_mode;
    bool compressed = false;
    int mtar_access = 0;

    switch(mode) {
    case TarOpenModeRead:
        mtar_access = MTAR_READ;
        access_mode = FSAM_READ;
        open_mode = FSOM_OPEN_EXISTING;
        break;
    case TarOpenModeWrite:
        mtar_access = MTAR_WRITE;
        access_mode = FSAM_WRITE;
        open_mode = FSOM_CREATE_ALWAYS;
        break;
    case TarOpenModeReadHeatshrink:
    case TarOpenModeReadGzip:
        mtar_access = MTAR_READ;
        access_mode = FSAM_READ;
        open_mode = FSOM_OPEN_EXISTING;
        compressed = true;
        break;
    default:
        return false;
    }

    File* stream = archive->stream;
    if(!storage_file_open(stream, path, access_mode, open_mode)) {
        return false;
    }

    if(!compressed) {
        mtar_init(&archive->tar, mtar_access, &filesystem_ops, stream);
        return true;
    }

    CompressedStream* compressed_stream = malloc(sizeof(CompressedStream));
    compressed_stream->stream = stream;
    compressed_stream->tar = &archive->tar;

    if(mode == TarOpenModeReadHeatshrink) {
        /* Read and validate stream header */
        HeatshrinkStreamHeader header;
        if(storage_file_read(stream, &header, sizeof(HeatshrinkStreamHeader)) !=
               sizeof(HeatshrinkStreamHeader) ||
           header.magic != HEATSHRINK_MAGIC) {
            storage_file_close(stream);
            free(compressed_stream);
            return false;
        }

        compressed_stream->type = CompressTypeHeatshrink;
        compressed_stream->config.heatshrink.window_sz2 = header.window_sz2;
        compressed_stream->config.heatshrink.lookahead_sz2 = header.lookahead_sz2;
        compressed_stream->config.heatshrink.input_buffer_sz = FILE_BLOCK_SIZE;

    } else if(mode == TarOpenModeReadGzip) {
        compressed_stream->type = CompressTypeGzip;
        compressed_stream->config.gzip.dict_sz = 32 * 1024;
        compressed_stream->config.gzip.input_buffer_sz = FILE_BLOCK_SIZE;
    }

    compressed_stream->decoder = compress_stream_decoder_alloc(
        compressed_stream->type, &compressed_stream->config, file_read_cb, stream);
    if(compressed_stream->decoder == NULL) {
        storage_file_close(stream);
        free(compressed_stream);
        return false;
    }

    mtar_init(&archive->tar, mtar_access, &compressed_ops, compressed_stream);

    return true;
}

void tar_archive_free(TarArchive* archive) {
    furi_check(archive);
    if(mtar_is_open(&archive->tar)) {
        mtar_close(&archive->tar);
    }
    storage_file_free(archive->stream);
    free(archive);
}

void tar_archive_set_file_callback(TarArchive* archive, tar_unpack_file_cb callback, void* context) {
    furi_check(archive);
    archive->unpack_cb = callback;
    archive->unpack_cb_context = context;
}

void tar_archive_set_read_callback(TarArchive* archive, tar_unpack_read_cb callback, void* context) {
    furi_check(archive);
    archive->read_cb = callback;
    archive->read_cb_context = context;
}

static int tar_archive_entry_counter(mtar_t* tar, const mtar_header_t* header, void* param) {
    UNUSED(tar);
    UNUSED(header);
    furi_assert(param);
    int32_t* counter = param;
    (*counter)++;
    return 0;
}

int32_t tar_archive_get_entries_count(TarArchive* archive) {
    furi_check(archive);
    int32_t counter = 0;
    if(mtar_foreach(&archive->tar, tar_archive_entry_counter, &counter) != MTAR_ESUCCESS) {
        counter = -1;
    }
    return counter;
}

bool tar_archive_get_read_progress(TarArchive* archive, int32_t* processed, int32_t* total) {
    furi_check(archive);
    if(mtar_access_mode(&archive->tar) != MTAR_READ) {
        return false;
    }

    if(processed) {
        *processed = storage_file_tell(archive->stream);
    }
    if(total) {
        *total = storage_file_size(archive->stream);
    }
    return true;
}

bool tar_archive_dir_add_element(TarArchive* archive, const char* dirpath) {
    furi_check(archive);
    return mtar_write_dir_header(&archive->tar, dirpath) == MTAR_ESUCCESS;
}

bool tar_archive_finalize(TarArchive* archive) {
    furi_check(archive);
    return mtar_finalize(&archive->tar) == MTAR_ESUCCESS;
}

bool tar_archive_store_data(
    TarArchive* archive,
    const char* path,
    const uint8_t* data,
    const int32_t data_len) {
    furi_check(archive);

    return tar_archive_file_add_header(archive, path, data_len) &&
           tar_archive_file_add_data_block(archive, data, data_len) &&
           tar_archive_file_finalize(archive);
}

bool tar_archive_file_add_header(TarArchive* archive, const char* path, const int32_t data_len) {
    furi_check(archive);

    return mtar_write_file_header(&archive->tar, path, data_len) == MTAR_ESUCCESS;
}

bool tar_archive_file_add_data_block(
    TarArchive* archive,
    const uint8_t* data_block,
    const int32_t block_len) {
    furi_check(archive);

    return mtar_write_data(&archive->tar, data_block, block_len) == block_len;
}

bool tar_archive_file_finalize(TarArchive* archive) {
    furi_check(archive);
    return mtar_end_data(&archive->tar) == MTAR_ESUCCESS;
}

typedef struct {
    TarArchive* archive;
    const char* work_dir;
    TarArchiveNameConverter converter;
    uint8_t* readbuf;
    FuriString* path;
    FuriString* converted_name;
    size_t archive_size;
} TarArchiveDirectoryOpParams;

static bool archive_extract_current_file(
    TarArchive* archive,
    const char* dst_path,
    uint8_t* readbuf,
    size_t archive_size) {
    mtar_t* tar = &archive->tar;
    File* out_file = storage_file_alloc(archive->storage);

    bool success = true;
    uint8_t n_tries = FILE_OPEN_NTRIES;
    do {
        while(n_tries-- > 0) {
            if(storage_file_open(out_file, dst_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
                break;
            }
            FURI_LOG_W(TAG, "Failed to open '%s', reties: %d", dst_path, n_tries);
            storage_file_close(out_file);
            furi_delay_ms(FILE_OPEN_RETRY_DELAY);
        }

        if(!storage_file_is_open(out_file)) {
            success = false;
            break;
        }

        while(!mtar_eof_data(tar)) {
            int32_t readcnt = mtar_read_data(tar, readbuf, FILE_BLOCK_SIZE);
            if(readcnt <= 0 ||
               storage_file_write(out_file, readbuf, (size_t)readcnt) != (size_t)readcnt) {
                success = false;
                break;
            }

            if(archive->read_cb) {
                archive->read_cb(
                    storage_file_tell(archive->stream), archive_size, archive->read_cb_context);
            }
        }
    } while(false);
    storage_file_free(out_file);

    return success;
}

static int archive_extract_foreach_cb(mtar_t* tar, const mtar_header_t* header, void* param) {
    UNUSED(tar);
    TarArchiveDirectoryOpParams* op_params = param;
    TarArchive* archive = op_params->archive;

    bool skip_entry = false;
    if(archive->unpack_cb) {
        skip_entry = !archive->unpack_cb(
            header->name, header->type == MTAR_TDIR, archive->unpack_cb_context);
    }

    if(skip_entry) {
        FURI_LOG_W(TAG, "filter: skipping entry \"%s\"", header->name);
        return 0;
    }

    FuriString* full_extracted_fname = op_params->path;
    if(header->type == MTAR_TDIR) {
        // Skip "/" entry since concat would leave it dangling, also want caller to mkdir destination
        if(strcmp(header->name, "/") == 0) {
            return 0;
        }

        path_concat(op_params->work_dir, header->name, full_extracted_fname);

        bool create_res =
            storage_simply_mkdir(archive->storage, furi_string_get_cstr(full_extracted_fname));
        return create_res ? 0 : -1;
    }

    if(header->type != MTAR_TREG) {
        FURI_LOG_W(TAG, "not extracting unsupported type \"%s\"", header->name);
        return 0;
    }

    FURI_LOG_D(TAG, "Extracting %u bytes to '%s'", header->size, header->name);

    const char* name = header->name;
    if(op_params->converter) {
        furi_string_set(op_params->converted_name, name);
        op_params->converter(op_params->converted_name);
        name = furi_string_get_cstr(op_params->converted_name);
    }
    path_concat(op_params->work_dir, name, full_extracted_fname);

    // Allocate once, and only if there is a file that passes the filter.
    if(!op_params->readbuf) op_params->readbuf = malloc(FILE_BLOCK_SIZE);
    bool success = archive_extract_current_file(
        archive,
        furi_string_get_cstr(full_extracted_fname),
        op_params->readbuf,
        op_params->archive_size);
    return success ? 0 : MTAR_EFAILURE;
}

bool tar_archive_unpack_to(
    TarArchive* archive,
    const char* destination,
    TarArchiveNameConverter converter) {
    furi_check(archive);
    TarArchiveDirectoryOpParams param = {
        .archive = archive,
        .work_dir = destination,
        .converter = converter,
        .path = furi_string_alloc(),
        .converted_name = converter ? furi_string_alloc() : NULL,
        .archive_size = archive->read_cb ? storage_file_size(archive->stream) : 0,
    };

    FURI_LOG_I(TAG, "Restoring '%s'", destination);

    bool success = mtar_foreach(&archive->tar, archive_extract_foreach_cb, &param) ==
                   MTAR_ESUCCESS;
    free(param.readbuf);
    furi_string_free(param.path);
    if(param.converted_name) furi_string_free(param.converted_name);
    return success;
}

bool tar_archive_add_file(
    TarArchive* archive,
    const char* fs_file_path,
    const char* archive_fname,
    const int32_t file_size) {
    furi_check(archive);
    uint8_t* file_buffer = malloc(FILE_BLOCK_SIZE);
    bool success = false;
    File* src_file = storage_file_alloc(archive->storage);
    uint8_t n_tries = FILE_OPEN_NTRIES;
    do {
        while(n_tries-- > 0) {
            if(storage_file_open(src_file, fs_file_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
                break;
            }
            FURI_LOG_W(TAG, "Failed to open '%s', reties: %d", fs_file_path, n_tries);
            storage_file_close(src_file);
            furi_delay_ms(FILE_OPEN_RETRY_DELAY);
        }

        if(!storage_file_is_open(src_file) ||
           !tar_archive_file_add_header(archive, archive_fname, file_size)) {
            break;
        }

        success = true; // if file is empty, that's not an error
        uint16_t bytes_read = 0;
        while((bytes_read = storage_file_read(src_file, file_buffer, FILE_BLOCK_SIZE))) {
            success = tar_archive_file_add_data_block(archive, file_buffer, bytes_read);
            if(!success) {
                break;
            }
        }

        success = success && tar_archive_file_finalize(archive);
    } while(false);

    storage_file_free(src_file);
    free(file_buffer);
    return success;
}

bool tar_archive_add_dir(TarArchive* archive, const char* fs_full_path, const char* path_prefix) {
    furi_check(archive);
    furi_check(path_prefix);

    File* directory = storage_file_alloc(archive->storage);
    FileInfo file_info;

    FURI_LOG_I(TAG, "Backing up '%s', '%s'", fs_full_path, path_prefix);
    char* name = malloc(MAX_NAME_LEN);
    bool success = false;

    do {
        if(!storage_dir_open(directory, fs_full_path)) {
            break;
        }

        while(true) {
            if(!storage_dir_read(directory, &file_info, name, MAX_NAME_LEN)) {
                success = true; /* empty dir / no more files */
                break;
            }

            FuriString* element_name = furi_string_alloc();
            FuriString* element_fs_abs_path = furi_string_alloc();

            path_concat(fs_full_path, name, element_fs_abs_path);
            if(strlen(path_prefix)) {
                path_concat(path_prefix, name, element_name);
            } else {
                furi_string_set(element_name, name);
            }

            if(file_info_is_dir(&file_info)) {
                success =
                    tar_archive_dir_add_element(archive, furi_string_get_cstr(element_name)) &&
                    tar_archive_add_dir(
                        archive,
                        furi_string_get_cstr(element_fs_abs_path),
                        furi_string_get_cstr(element_name));
            } else {
                success = tar_archive_add_file(
                    archive,
                    furi_string_get_cstr(element_fs_abs_path),
                    furi_string_get_cstr(element_name),
                    file_info.size);
            }
            furi_string_free(element_name);
            furi_string_free(element_fs_abs_path);

            if(!success) {
                break;
            }
        }
    } while(false);

    free(name);
    storage_file_free(directory);
    return success;
}

bool tar_archive_unpack_file(
    TarArchive* archive,
    const char* archive_fname,
    const char* destination) {
    furi_check(archive);
    furi_check(archive_fname);
    furi_check(destination);
    if(mtar_find(&archive->tar, archive_fname) != MTAR_ESUCCESS) {
        return false;
    }
    uint8_t* readbuf = malloc(FILE_BLOCK_SIZE);
    size_t archive_size = archive->read_cb ? storage_file_size(archive->stream) : 0;
    bool success = archive_extract_current_file(archive, destination, readbuf, archive_size);
    free(readbuf);
    return success;
}
