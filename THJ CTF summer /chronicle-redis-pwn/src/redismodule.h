#ifndef REDISMODULE_H
#define REDISMODULE_H

#include <stddef.h>
#include <stdint.h>

#define REDISMODULE_OK 0
#define REDISMODULE_ERR 1
#define REDISMODULE_APIVER_1 1

typedef long long mstime_t;
typedef uint64_t RedisModuleTimerID;

typedef struct RedisModuleCtx RedisModuleCtx;
typedef struct RedisModuleString RedisModuleString;
typedef struct RedisModuleCallReply RedisModuleCallReply;

typedef int (*RedisModuleCmdFunc)(RedisModuleCtx *ctx, RedisModuleString **argv, int argc);
typedef void (*RedisModuleTimerProc)(RedisModuleCtx *ctx, void *data);

static int (*RedisModule_GetApi)(const char *funcname, void *targetPtr);
static void *(*RedisModule_Alloc)(size_t bytes);
static void *(*RedisModule_Calloc)(size_t nmemb, size_t size);
static void (*RedisModule_Free)(void *ptr);
static RedisModuleCallReply *(*RedisModule_Call)(RedisModuleCtx *ctx, const char *cmdname,
    const char *fmt, ...);
static void (*RedisModule_FreeCallReply)(RedisModuleCallReply *reply);
static int (*RedisModule_CreateCommand)(RedisModuleCtx *ctx, const char *name,
    RedisModuleCmdFunc cmdfunc, const char *strflags, int firstkey, int lastkey,
    int keystep);
static void (*RedisModule_SetModuleAttribs)(RedisModuleCtx *ctx, const char *name,
    int ver, int apiver);
static int (*RedisModule_WrongArity)(RedisModuleCtx *ctx);
static int (*RedisModule_ReplyWithLongLong)(RedisModuleCtx *ctx, long long ll);
static int (*RedisModule_ReplyWithError)(RedisModuleCtx *ctx, const char *err);
static int (*RedisModule_ReplyWithSimpleString)(RedisModuleCtx *ctx, const char *msg);
static int (*RedisModule_ReplyWithArray)(RedisModuleCtx *ctx, long len);
static int (*RedisModule_ReplyWithStringBuffer)(RedisModuleCtx *ctx, const char *buf,
    size_t len);
static const char *(*RedisModule_StringPtrLen)(const RedisModuleString *str,
    size_t *len);
static RedisModuleTimerID (*RedisModule_CreateTimer)(RedisModuleCtx *ctx,
    mstime_t period, RedisModuleTimerProc callback, void *data);
static void (*RedisModule_Log)(RedisModuleCtx *ctx, const char *level,
    const char *fmt, ...);

#define REDISMODULE_GET_API(name) \
    do { \
        if (RedisModule_GetApi("RedisModule_" #name, (void **)&RedisModule_ ## name) \
            == REDISMODULE_ERR) return REDISMODULE_ERR; \
    } while (0)

static int RedisModule_Init(RedisModuleCtx *ctx, const char *name, int ver, int apiver) {
    RedisModule_GetApi = (int (*)(const char *, void *))(unsigned long)((void **)ctx)[0];
    REDISMODULE_GET_API(Alloc);
    REDISMODULE_GET_API(Calloc);
    REDISMODULE_GET_API(Free);
    REDISMODULE_GET_API(Call);
    REDISMODULE_GET_API(FreeCallReply);
    REDISMODULE_GET_API(CreateCommand);
    REDISMODULE_GET_API(SetModuleAttribs);
    REDISMODULE_GET_API(WrongArity);
    REDISMODULE_GET_API(ReplyWithLongLong);
    REDISMODULE_GET_API(ReplyWithError);
    REDISMODULE_GET_API(ReplyWithSimpleString);
    REDISMODULE_GET_API(ReplyWithArray);
    REDISMODULE_GET_API(ReplyWithStringBuffer);
    REDISMODULE_GET_API(StringPtrLen);
    REDISMODULE_GET_API(CreateTimer);
    REDISMODULE_GET_API(Log);
    RedisModule_SetModuleAttribs(ctx, name, ver, apiver);
    return REDISMODULE_OK;
}

#endif
