#include "record.h"
#include "check.h"
#include "mutex.h"
#include "event_flag.h"

#include <m-dict.h>
#include <toolbox/m_cstr_dup.h>

#define FURI_RECORD_FLAG_READY (0x1)

typedef struct {
    FuriEventFlag *flags;
    void *data;
    size_t holders_count;
} FuriRecordData;

DICT_DEF2(FuriRecordDataDict, const char *, M_CSTR_DUP_OPLIST, FuriRecordData, M_POD_OPLIST)

typedef struct {
    FuriMutex *mutex;
    FuriRecordDataDict_t records;
} FuriRecord;

static FuriRecord *furi_record = NULL;

static FuriRecordData *furi_record_get(const char *name)
{
    return FuriRecordDataDict_get(furi_record->records, name);
}

static void furi_record_put(const char *name, FuriRecordData *record_data)
{
    FuriRecordDataDict_set_at(furi_record->records, name, *record_data);
}

static void furi_record_erase(const char *name, FuriRecordData *record_data)
{
    if (record_data->flags) {
        furi_event_flag_free(record_data->flags);
    }
    FuriRecordDataDict_erase(furi_record->records, name);
}

void furi_record_init(void)
{
    furi_record = malloc(sizeof(FuriRecord));
    furi_record->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    FuriRecordDataDict_init(furi_record->records);
}

static FuriRecordData *furi_record_data_get_or_create(const char *name)
{
    furi_check(furi_record);
    FuriRecordData *record_data = furi_record_get(name);
    if (!record_data) {
        FuriRecordData new_record;
        new_record.flags = NULL;
        new_record.data = NULL;
        new_record.holders_count = 0;
        furi_record_put(name, &new_record);
        record_data = furi_record_get(name);
    }
    return record_data;
}

static void furi_record_lock(void)
{
    furi_check(furi_mutex_acquire(furi_record->mutex, FuriWaitForever) == FuriStatusOk);
}

static void furi_record_unlock(void)
{
    furi_check(furi_mutex_release(furi_record->mutex) == FuriStatusOk);
}

bool furi_record_exists(const char *name)
{
    furi_check(furi_record);
    furi_check(name);

    bool ret = false;

    furi_record_lock();
    ret = (furi_record_get(name) != NULL);
    furi_record_unlock();

    return ret;
}

void furi_record_create(const char *name, void *data)
{
    furi_check(furi_record);
    furi_check(name);
    furi_check(data);

    furi_record_lock();

    // Get record data and fill it
    FuriRecordData *record_data = furi_record_data_get_or_create(name);
    furi_check(record_data->data == NULL);
    record_data->data = data;
    if (record_data->flags) {
        furi_event_flag_set(record_data->flags, FURI_RECORD_FLAG_READY);
    }

    furi_record_unlock();
}

bool furi_record_destroy(const char *name)
{
    furi_check(furi_record);
    furi_check(name);

    bool ret = false;

    furi_record_lock();

    FuriRecordData *record_data = furi_record_get(name);
    furi_check(record_data);
    if (record_data->holders_count == 0) {
        furi_record_erase(name, record_data);
        ret = true;
    }

    furi_record_unlock();

    return ret;
}

void *furi_record_open(const char *name)
{
    furi_check(furi_record);
    furi_check(name);

    furi_record_lock();

    FuriRecordData *record_data = furi_record_data_get_or_create(name);
    record_data->holders_count++;

    void *data = record_data->data;
    FuriEventFlag *flags = NULL;
    if (!data) {
        // Ready records need neither an event group nor an RTOS wait.
        if (!record_data->flags) {
            record_data->flags = furi_event_flag_alloc();
        }
        flags = record_data->flags;
    }

    furi_record_unlock();

    if (!data) {
        // holders_count keeps the record and its event group alive while waiting.
        furi_check(furi_event_flag_wait(flags, FURI_RECORD_FLAG_READY,
                                        FuriFlagWaitAny | FuriFlagNoClear,
                                        FuriWaitForever) == FURI_RECORD_FLAG_READY);

        furi_record_lock();
        // Other records may have been inserted while the lock was released.
        data = furi_record_get(name)->data;
        furi_record_unlock();
    }

    return data;
}

void furi_record_close(const char *name)
{
    furi_check(furi_record);
    furi_check(name);

    furi_record_lock();

    FuriRecordData *record_data = furi_record_get(name);
    furi_check(record_data);
    furi_check(record_data->holders_count > 0);
    record_data->holders_count--;

    furi_record_unlock();
}
