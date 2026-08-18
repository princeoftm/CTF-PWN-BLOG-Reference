#include "redismodule.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHRONICLE_VERSION 1
#define CHRONICLE_NOTE 1U
#define CHRONICLE_RECOVERY 2U

#define CHRONICLE_PENDING 0U
#define CHRONICLE_RUNNING 1U
#define CHRONICLE_COMPLETE 2U
#define CHRONICLE_FAILED 3U

#define LABEL_CAPACITY 32U
#define NOTE_CAPACITY 80U
#define RESULT_CAPACITY 128U
#define WORKSPACE_CAPACITY 192U
#define MAX_DELAY_MS 86400000ULL

typedef struct ChronicleTask ChronicleTask;
typedef void (*ChronicleCompletion)(RedisModuleCtx *ctx, ChronicleTask *task);

struct ChronicleTask {
    uint64_t id;
    uint64_t delay_ms;
    uint32_t state;
    uint32_t kind;
    uint32_t note_length;
    uint32_t label_length;
    uint64_t ticket;
    char label[LABEL_CAPACITY];
    unsigned char note[NOTE_CAPACITY];
    ChronicleCompletion completion;
    char result[RESULT_CAPACITY];
    unsigned char workspace[WORKSPACE_CAPACITY];
    ChronicleTask *next;
    RedisModuleTimerID timer_id;
};

static ChronicleTask *tasks;
static uint64_t next_task_id = 4100;
static char recovery_value[RESULT_CAPACITY];
static size_t recovery_value_length;

static void discard_reply(RedisModuleCallReply *reply) {
    if (reply != NULL) RedisModule_FreeCallReply(reply);
}

static void seed_public_state(RedisModuleCtx *ctx) {
    discard_reply(RedisModule_Call(ctx, "HSET", "ccccccc",
        "tenant:atlas", "name", "Atlas Freight", "region", "ap-northeast", "plan", "enterprise"));
    discard_reply(RedisModule_Call(ctx, "HSET", "ccccccc",
        "tenant:ember", "name", "Ember Health", "region", "ap-southeast", "plan", "growth"));
    discard_reply(RedisModule_Call(ctx, "HSET", "ccccccc",
        "tenant:orbit", "name", "Orbit Retail", "region", "eu-west", "plan", "starter"));
    discard_reply(RedisModule_Call(ctx, "HSET", "ccccccc",
        "ledger:ingest:atlas", "records", "1842", "source", "warehouse-3", "state", "reconcile"));
    discard_reply(RedisModule_Call(ctx, "HSET", "ccccccc",
        "ledger:ingest:ember", "records", "913", "source", "claims-batch", "state", "review"));
    discard_reply(RedisModule_Call(ctx, "SET", "cc",
        "ledger:retention:policy", "180d-hot/365d-archive"));
    discard_reply(RedisModule_Call(ctx, "LPUSH", "cccc",
        "ledger:audit:recent", "export/orbit/304", "reconcile/ember/913", "ingest/atlas/1842"));
}

static uint64_t rotate_left(uint64_t value, unsigned int amount) {
    return (value << amount) | (value >> (64U - amount));
}

static uint64_t ticket_for(const ChronicleTask *task) {
    uint64_t salt = rotate_left(task->id * 0x9e3779b97f4a7c15ULL, 17U);
    return ((uint64_t)(uintptr_t)task->completion) ^ salt;
}

static int parse_u64(RedisModuleString *argument, uint64_t *out) {
    size_t length = 0;
    const char *data = RedisModule_StringPtrLen(argument, &length);
    uint64_t value = 0;

    if (length == 0) return 0;
    for (size_t index = 0; index < length; ++index) {
        unsigned char digit = (unsigned char)data[index];
        if (digit < '0' || digit > '9') return 0;
        digit = (unsigned char)(digit - '0');
        if (value > (UINT64_MAX - digit) / 10U) return 0;
        value = value * 10U + digit;
    }

    *out = value;
    return 1;
}

static int copy_redis_string(RedisModuleString *argument, char *target, size_t capacity,
    uint32_t *length_out) {
    size_t length = 0;
    const char *data = RedisModule_StringPtrLen(argument, &length);

    if (length >= capacity) return 0;
    if (length != 0) memcpy(target, data, length);
    target[length] = '\0';
    *length_out = (uint32_t)length;
    return 1;
}

static const char *state_name(uint32_t state) {
    switch (state) {
        case CHRONICLE_PENDING: return "pending";
        case CHRONICLE_RUNNING: return "running";
        case CHRONICLE_COMPLETE: return "complete";
        default: return "failed";
    }
}

static void commit_annotation(RedisModuleCtx *ctx, ChronicleTask *task) {
    size_t copy_length = task->note_length;
    (void)ctx;

    if (copy_length > NOTE_CAPACITY) copy_length = NOTE_CAPACITY;
    if (copy_length != 0) memcpy(task->result, task->note, copy_length);
    task->result[copy_length] = '\0';
    task->state = CHRONICLE_COMPLETE;
}

static void materialize_anchor(RedisModuleCtx *ctx, ChronicleTask *task) {
    (void)ctx;

    if (recovery_value_length != 0) {
        memcpy(task->result, recovery_value, recovery_value_length);
    }
    task->result[recovery_value_length] = '\0';
    task->state = CHRONICLE_COMPLETE;
}

static ChronicleCompletion completion_for_kind(uint32_t kind) {
    if (kind == CHRONICLE_RECOVERY) return materialize_anchor;
    return commit_annotation;
}

static void dispatch_task(RedisModuleCtx *ctx, void *data) {
    ChronicleTask *task = data;

    task->timer_id = 0;
    task->state = CHRONICLE_RUNNING;
    if (task->completion == NULL) {
        task->state = CHRONICLE_FAILED;
        return;
    }
    task->completion(ctx, task);
    if (task->state == CHRONICLE_RUNNING) task->state = CHRONICLE_COMPLETE;
}

static ChronicleTask *find_task(uint64_t id) {
    ChronicleTask *task = tasks;
    while (task != NULL) {
        if (task->id == id) return task;
        task = task->next;
    }
    return NULL;
}

static ChronicleTask *allocate_task(uint64_t delay_ms, uint32_t kind) {
    ChronicleTask *task = RedisModule_Calloc(1, sizeof(*task));
    if (task == NULL) return NULL;

    task->id = ++next_task_id;
    task->delay_ms = delay_ms;
    task->state = CHRONICLE_PENDING;
    task->kind = kind;
    task->completion = completion_for_kind(kind);
    task->ticket = ticket_for(task);
    task->next = tasks;
    tasks = task;
    return task;
}

static int schedule_task(RedisModuleCtx *ctx, ChronicleTask *task) {
    task->timer_id = RedisModule_CreateTimer(ctx, (mstime_t)task->delay_ms,
        dispatch_task, task);
    return task->timer_id != 0;
}

static uint32_t fnv1a32(const unsigned char *data, size_t length) {
    uint32_t value = 2166136261U;
    for (size_t index = 0; index < length; ++index) {
        value ^= data[index];
        value *= 16777619U;
    }
    return value;
}

static void write_u32le(unsigned char *target, uint32_t value) {
    target[0] = (unsigned char)value;
    target[1] = (unsigned char)(value >> 8U);
    target[2] = (unsigned char)(value >> 16U);
    target[3] = (unsigned char)(value >> 24U);
}

static uint32_t read_u32le(const unsigned char *source) {
    return (uint32_t)source[0]
        | ((uint32_t)source[1] << 8U)
        | ((uint32_t)source[2] << 16U)
        | ((uint32_t)source[3] << 24U);
}

static size_t write_uvarint(unsigned char *target, uint64_t value) {
    size_t length = 0;
    do {
        unsigned char byte = (unsigned char)(value & 0x7fU);
        value >>= 7U;
        if (value != 0) byte |= 0x80U;
        target[length++] = byte;
    } while (value != 0);
    return length;
}

static int read_uvarint(const unsigned char **cursor, const unsigned char *end,
    uint64_t *out) {
    uint64_t value = 0;
    unsigned int shift = 0;

    while (*cursor < end && shift < 64U) {
        unsigned char byte = *(*cursor)++;
        if (shift == 63U && (byte & 0x7eU) != 0) return 0;
        value |= (uint64_t)(byte & 0x7fU) << shift;
        if ((byte & 0x80U) == 0) {
            *out = value;
            return 1;
        }
        shift += 7U;
    }
    return 0;
}

static int command_new(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    uint64_t delay_ms = 0;
    ChronicleTask *task;

    if (argc != 4) return RedisModule_WrongArity(ctx);
    if (!parse_u64(argv[1], &delay_ms) || delay_ms < 10U || delay_ms > MAX_DELAY_MS) {
        return RedisModule_ReplyWithError(ctx, "ERR invalid delay");
    }

    task = allocate_task(delay_ms, CHRONICLE_NOTE);
    if (task == NULL) return RedisModule_ReplyWithError(ctx, "ERR allocation failed");
    if (!copy_redis_string(argv[2], task->label, sizeof(task->label), &task->label_length)
        || !copy_redis_string(argv[3], (char *)task->note, sizeof(task->note),
            &task->note_length)) {
        return RedisModule_ReplyWithError(ctx, "ERR annotation is too large");
    }
    if (!schedule_task(ctx, task)) return RedisModule_ReplyWithError(ctx, "ERR scheduling failed");
    return RedisModule_ReplyWithLongLong(ctx, (long long)task->id);
}

static int command_show(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    uint64_t id = 0;
    ChronicleTask *task;

    if (argc != 2) return RedisModule_WrongArity(ctx);
    if (!parse_u64(argv[1], &id)) return RedisModule_ReplyWithError(ctx, "ERR invalid id");
    task = find_task(id);
    if (task == NULL) return RedisModule_ReplyWithError(ctx, "ERR unknown task");

    RedisModule_ReplyWithArray(ctx, 6);
    RedisModule_ReplyWithLongLong(ctx, (long long)task->id);
    RedisModule_ReplyWithSimpleString(ctx, state_name(task->state));
    RedisModule_ReplyWithStringBuffer(ctx, task->label, task->label_length);
    RedisModule_ReplyWithLongLong(ctx, (long long)task->ticket);
    RedisModule_ReplyWithLongLong(ctx, (long long)task->delay_ms);
    RedisModule_ReplyWithStringBuffer(ctx, task->result, strlen(task->result));
    return REDISMODULE_OK;
}

static int command_export(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    uint64_t id = 0;
    ChronicleTask *task;
    unsigned char *archive;
    size_t prefix_length;
    size_t varint_length;
    size_t archive_length;
    size_t offset;

    if (argc != 2) return RedisModule_WrongArity(ctx);
    if (!parse_u64(argv[1], &id)) return RedisModule_ReplyWithError(ctx, "ERR invalid id");
    task = find_task(id);
    if (task == NULL) return RedisModule_ReplyWithError(ctx, "ERR unknown task");

    prefix_length = 4U + 1U + 1U + 2U + 4U + 1U + task->label_length;
    varint_length = task->note_length < 128U ? 1U : 2U;
    archive_length = prefix_length + varint_length + task->note_length + 4U;
    archive = RedisModule_Alloc(archive_length);
    if (archive == NULL) return RedisModule_ReplyWithError(ctx, "ERR allocation failed");

    offset = 0;
    memcpy(archive + offset, "CHRN", 4U);
    offset += 4U;
    archive[offset++] = CHRONICLE_VERSION;
    archive[offset++] = CHRONICLE_NOTE;
    archive[offset++] = 0;
    archive[offset++] = 0;
    write_u32le(archive + offset, (uint32_t)task->delay_ms);
    offset += 4U;
    archive[offset++] = (unsigned char)task->label_length;
    memcpy(archive + offset, task->label, task->label_length);
    offset += task->label_length;
    offset += write_uvarint(archive + offset, task->note_length);
    memcpy(archive + offset, task->note, task->note_length);
    offset += task->note_length;
    write_u32le(archive + offset, fnv1a32(archive, offset));

    RedisModule_ReplyWithStringBuffer(ctx, (const char *)archive, archive_length);
    RedisModule_Free(archive);
    return REDISMODULE_OK;
}

static int command_import(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    size_t archive_length = 0;
    const unsigned char *archive;
    const unsigned char *cursor;
    const unsigned char *checksum;
    uint64_t body_length = 0;
    uint64_t delay_ms;
    uint32_t label_length;
    uint32_t claimed_checksum;
    ChronicleTask *task;

    if (argc != 2) return RedisModule_WrongArity(ctx);
    archive = (const unsigned char *)RedisModule_StringPtrLen(argv[1], &archive_length);
    if (archive_length < 18U) return RedisModule_ReplyWithError(ctx, "ERR malformed archive");
    if (memcmp(archive, "CHRN", 4U) != 0 || archive[4] != CHRONICLE_VERSION
        || archive[5] != CHRONICLE_NOTE || archive[6] != 0 || archive[7] != 0) {
        return RedisModule_ReplyWithError(ctx, "ERR unsupported archive");
    }

    delay_ms = read_u32le(archive + 8U);
    if (delay_ms < 10U || delay_ms > MAX_DELAY_MS) {
        return RedisModule_ReplyWithError(ctx, "ERR invalid delay");
    }
    label_length = archive[12];
    cursor = archive + 13U;
    checksum = archive + archive_length - 4U;
    if (label_length >= LABEL_CAPACITY || cursor + label_length >= checksum) {
        return RedisModule_ReplyWithError(ctx, "ERR malformed archive");
    }
    cursor += label_length;
    if (!read_uvarint(&cursor, checksum, &body_length)
        || body_length != (uint64_t)(checksum - cursor)) {
        return RedisModule_ReplyWithError(ctx, "ERR malformed archive");
    }
    claimed_checksum = read_u32le(checksum);
    if (claimed_checksum != fnv1a32(archive, archive_length - 4U)) {
        return RedisModule_ReplyWithError(ctx, "ERR archive seal mismatch");
    }
    if ((uint8_t)body_length > NOTE_CAPACITY) {
        return RedisModule_ReplyWithError(ctx, "ERR annotation is too large");
    }

    task = allocate_task(delay_ms, CHRONICLE_NOTE);
    if (task == NULL) return RedisModule_ReplyWithError(ctx, "ERR allocation failed");
    memcpy(task->label, archive + 13U, label_length);
    task->label[label_length] = '\0';
    task->label_length = label_length;
    task->note_length = (uint32_t)body_length;
    memcpy(task->note, cursor, (size_t)body_length);
    if (!schedule_task(ctx, task)) return RedisModule_ReplyWithError(ctx, "ERR scheduling failed");
    return RedisModule_ReplyWithLongLong(ctx, (long long)task->id);
}

static int load_recovery_value(void) {
    FILE *stream = fopen("/tmp/.chronicle-anchor", "rb");
    if (stream == NULL) return 0;
    recovery_value_length = fread(recovery_value, 1U, sizeof(recovery_value) - 1U, stream);
    fclose(stream);
    while (recovery_value_length != 0
        && (recovery_value[recovery_value_length - 1U] == '\n'
            || recovery_value[recovery_value_length - 1U] == '\r')) {
        --recovery_value_length;
    }
    recovery_value[recovery_value_length] = '\0';
    return recovery_value_length != 0;
}

int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    (void)argv;
    (void)argc;

    if (RedisModule_Init(ctx, "chronicle", 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }
    if (!load_recovery_value()) {
        RedisModule_Log(ctx, "warning", "chronicle recovery state unavailable");
        return REDISMODULE_ERR;
    }
    if (RedisModule_CreateCommand(ctx, "chronicle.new", command_new, "write", 0, 0, 0)
        == REDISMODULE_ERR
        || RedisModule_CreateCommand(ctx, "chronicle.show", command_show, "readonly", 0, 0, 0)
        == REDISMODULE_ERR
        || RedisModule_CreateCommand(ctx, "chronicle.export", command_export, "readonly", 0, 0, 0)
        == REDISMODULE_ERR
        || RedisModule_CreateCommand(ctx, "chronicle.import", command_import, "write", 0, 0, 0)
        == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }
    seed_public_state(ctx);
    return REDISMODULE_OK;
}
