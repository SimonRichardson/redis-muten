/*
* muten - is a implementation of the roshi set using the redis module SDK rather
* than using the Lua vm.
*/

#include <stdio.h>
#include <string.h>
#include "../redismodule.h"

#define RM_MODULE_NAME "muten"
#define RLMODULE_VERSION "1.0.0"
#define RLMODULE_PROTO "1.0"
#define RLMODULE_DESC "Reasonable implmentation of the roshi set."

#define MUT_SIGNATURE "MUTEN:1.0:"

/* MUT.DEBUG Key
*/
int MUTDebugCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  if (argc != 2) {
    return RedisModule_WrongArity(ctx);
  }
  RedisModule_AutoMemory(ctx);

  RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ);

  /* Verify that the key is empty or a string. */
  int keyType = RedisModule_KeyType(key);
  if (keyType == REDISMODULE_KEYTYPE_EMPTY) {
    RedisModule_ReplyWithNull(ctx);
    return REDISMODULE_ERR;
  }
  if (keyType != REDISMODULE_KEYTYPE_STRING) {
    RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    return REDISMODULE_ERR;
  }

  RedisModule_ReplyWithSimpleString(ctx, "Hello");

  return REDISMODULE_OK;
}

int RedisModule_OnLoad(RedisModuleCtx *ctx) {
  if (RedisModule_Init(ctx, RM_MODULE_NAME, 1, REDISMODULE_APIVER_1) ==
      REDISMODULE_ERR)
    return REDISMODULE_ERR;

  if (RedisModule_CreateCommand(ctx, "mut.debug", MUTDebugCommand, "readonly",
                                2, 2, 1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  return REDISMODULE_OK;
}