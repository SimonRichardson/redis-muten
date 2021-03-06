/*
* Copyright (C) 2017 Simon Richardson
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Affero General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
* muten - is a implementation of the roshi set using the redis module SDK rather
* than using the Lua vm.
*/

#include <stdio.h>
#include <string.h>
#include "../deps/redismodule.h"
#include "../deps/rmutil/alloc.h"
#include "../deps/rmutil/sds.h"
#include "../deps/rmutil/logging.h"
#include "muten.h"

#define RM_MODULE_NAME "muten"
#define RLMODULE_VERSION "1.0.0"
#define RLMODULE_PROTO "1.0"
#define RLMODULE_DESC "Reasonable implmentation of the roshi set."

#define MUTEN_SIGNATURE "MUTEN:1.0:"

int Muten_Validate(char *str, long long score, const char *txn) {
  return MUTEN_OK;
}

struct muten_key_s {
  RedisModuleString *rms;
  RedisModuleKey *rmk;
};

typedef struct muten_key_s muten_key;

muten_key *key_new(RedisModuleString *rms, RedisModuleKey *rmk) {
  muten_key *k;
  if ((k = malloc(sizeof(muten_key))) == NULL) {
    return NULL;
  }

  k->rms = rms;
  k->rmk = rmk;

  return k;
}

muten_key *NewMutenKey(RedisModuleCtx *ctx, const char *key, const char *suffix) {
  sds sk = sdsempty();
  sk = sdscat(sk, key);
  sk = sdscat(sk, suffix);

  RedisModuleString *rms = RedisModule_CreateString(ctx, (const char *)sk, sdslen(sk));

  sdsfree(sk);

  RedisModuleKey *rmk =
      RedisModule_OpenKey(ctx, rms, REDISMODULE_READ | REDISMODULE_WRITE);
  if ((RedisModule_KeyType(rmk) != REDISMODULE_KEYTYPE_EMPTY) &&
      (RedisModule_KeyType(rmk) != REDISMODULE_KEYTYPE_HASH)) {
    RedisModule_ReplyWithError(ctx, MUTEN_ERRORMSG_WRONGKEY);
    return NULL;
  }

  return key_new(rms, rmk);
}

struct muten_data_s {
  long long score;
  const char *txn;
  const char *data;
};

typedef struct muten_data_s muten_data;

muten_data *data_new(long long score, const char *txn, const char *data) {
  muten_data *d;
  if ((d = malloc(sizeof(muten_data))) == NULL) {
    return NULL;
  }

  d->score = score;
  d->txn = txn;
  d->data = data;

  return d;
}

muten_data *Muten_ParseHashFieldData(RedisModuleCtx *ctx, RedisModuleString *data) {
  int tokcnt;
  sds s = sdsempty();
  s = sdscat(s, (const char *)data);

  sds *toks = sdssplitlen(s, sdslen(s), MUTEN_SEPARATOR, 1, &tokcnt);

  if (tokcnt != 3) {
    return NULL;
  }

  RedisModuleString *sscore = RedisModule_CreateString(ctx, toks[0], sdslen(toks[0]));
  if (!sscore) {
    return NULL;
  }

  long long score;
  if (RedisModule_StringToLongLong(sscore, &score) != REDISMODULE_OK) {
    return NULL;
  }

  return data_new(score, toks[1], toks[2]);
}

int Muten_ValidHashFieldData(muten_key *key, RedisModuleString *field, long long score, const char *txn) {
  RedisModuleString *data;
  int err = RedisModule_HashGet(key->rmk, REDISMODULE_HASH_NONE, field, &data, NULL);
  if (err == REDISMODULE_ERR) {
    return MUTEN_ERR;
  }
  if (!data) {
    return MUTEN_OK;
  }

  err = Muten_Validate((char *)data, score, txn);
  if (err != MUTEN_OK) {
    return MUTEN_INVALID_ERR;
  }

  return MUTEN_OK;
}

int Muten_Command(RedisModuleCtx *ctx, RedisModuleString **argv, int argc, const char *isuffix, const char *rsuffix) {
  if (argc != 6) {
    return RedisModule_WrongArity(ctx);
  }
  RedisModule_AutoMemory(ctx);

  // Define the keys for both types of sets
  size_t skeyLen;
  const char *skey = RedisModule_StringPtrLen(argv[1], &skeyLen);

  muten_key *imKey = NewMutenKey(ctx, skey, isuffix);
  if (!imKey) {
    return MUTEN_ERR;
  }
  muten_key *dmKey = NewMutenKey(ctx, skey, rsuffix);
  if (!dmKey) {
    return MUTEN_ERR;
  }

  // Get the values.
  RedisModuleString *field = argv[2];

  long long score;
  if ((RedisModule_StringToLongLong(argv[3], &score) != REDISMODULE_OK) || (score < 1)) {
    RedisModule_ReplyWithError(ctx, "ERR invalid score");
    return MUTEN_ERR;
  }

  size_t txnLen;
  const char *txn = RedisModule_StringPtrLen(argv[4], &txnLen);

  size_t dataLen;
  const char *data = RedisModule_StringPtrLen(argv[5], &dataLen);

  int err;

  // Get the value out of the hash for insertions.
  err = Muten_ValidHashFieldData(imKey, field, score, txn);
  if (err != MUTEN_OK) {
    if (err == MUTEN_INVALID_ERR) {
      RM_LOG_DEBUG(ctx, "Field data value was invalid!");
      RedisModule_ReplyWithLongLong(ctx, -1);
      return MUTEN_OK;
    }
    return MUTEN_ERR;
  }

  // Get the value out of the hash for deletions.
  err = Muten_ValidHashFieldData(dmKey, field, score, txn);
  if (err != MUTEN_OK) {
    if (err == MUTEN_INVALID_ERR) {
      RM_LOG_DEBUG(ctx, "Field data value was invalid!");
      RedisModule_ReplyWithLongLong(ctx, -1);
      return MUTEN_OK;
    }
    return MUTEN_ERR;
  }

  // We don't actually care about the result of this!
  RedisModuleCallReply *res = RedisModule_Call(ctx, "hdel", (char *)dmKey->rms, field);
  if (RedisModule_CallReplyType(res) == REDISMODULE_REPLY_INTEGER && RedisModule_CallReplyInteger(res) != 0) {
    RM_LOG_DEBUG(ctx, "Removal of hash field.");
  } else if (RedisModule_CallReplyType(res) == REDISMODULE_REPLY_ERROR) {
    RM_LOG_WARNING(ctx, "Attempting to remove hash key failed.");
    return MUTEN_ERR;
  }

  // Now the actual setting.
  int updated = RedisModule_HashSet(imKey->rmk, REDISMODULE_HASH_NONE, field, data, NULL);
  RedisModule_ReplyWithLongLong(ctx, (long long)updated);

  free(imKey);
  free(dmKey);

  return MUTEN_OK;
}

/* MUTEN.STORE.INSERT key field score txn data
*/
int MutenStoreInsertCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  int res = Muten_Command(ctx, argv, argc, MUTEN_INSERT_SUFFIX, MUTEN_DELETE_SUFFIX);
  if (res != MUTEN_OK) {
    return REDISMODULE_ERR;
  }

  return REDISMODULE_OK;
}

/* MUTEN.STORE.DELETE key field score txn data
*/
int MutenStoreDeleteCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  int res = Muten_Command(ctx, argv, argc, MUTEN_DELETE_SUFFIX, MUTEN_INSERT_SUFFIX);
  if (res != MUTEN_OK) {
    return REDISMODULE_ERR;
  }

  return REDISMODULE_OK;
}

/* MUTEN.STORE.DEBUG key field
*/
int MutenStoreDebugCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  if (argc != 3) {
    return RedisModule_WrongArity(ctx);
  }
  RedisModule_AutoMemory(ctx);

  // Get the keys
  size_t skeyLen;
  const char *skey = RedisModule_StringPtrLen(argv[1], &skeyLen);

  muten_key *imKey = NewMutenKey(ctx, skey, MUTEN_INSERT_SUFFIX);
  if (!imKey) {
    return REDISMODULE_ERR;
  }
  muten_key *dmKey = NewMutenKey(ctx, skey, MUTEN_DELETE_SUFFIX);
  if (!dmKey) {
    return REDISMODULE_ERR;
  }

  // Get the values.
  RedisModuleString *field = argv[2];

  int err;

  // Get the insertion.
  RedisModuleString *idata;
  err = RedisModule_HashGet(imKey->rmk, REDISMODULE_HASH_NONE, field, &idata, NULL);
  if (err == REDISMODULE_ERR) {
    return REDISMODULE_ERR;
  }

  // Get the deletion.
  RedisModuleString *ddata;
  err = RedisModule_HashGet(imKey->rmk, REDISMODULE_HASH_NONE, field, &ddata, NULL);
  if (err == REDISMODULE_ERR) {
    return REDISMODULE_ERR;
  }

  int replyArity = 6;
  RedisModule_ReplyWithArray(ctx, replyArity);
  if (!idata) {
    for (int i = 0; i < replyArity/2; i++) {
      RedisModule_ReplyWithNull(ctx);
    }
  } else {
    muten_data *data = Muten_ParseHashFieldData(ctx, idata);
    RedisModule_ReplyWithLongLong(ctx, data->score);
    RedisModule_ReplyWithSimpleString(ctx, data->txn);
    RedisModule_ReplyWithSimpleString(ctx, data->data);
    free(data);
  }

  if (!ddata) {
    for (int i = 0; i < replyArity/2; i++) {
      RedisModule_ReplyWithNull(ctx);
    }
  } else {
    muten_data *data = Muten_ParseHashFieldData(ctx, ddata);
    RedisModule_ReplyWithLongLong(ctx, data->score);
    RedisModule_ReplyWithSimpleString(ctx, data->txn);
    RedisModule_ReplyWithSimpleString(ctx, data->data);
    free(data);
  }

  return REDISMODULE_OK;
}

/* Redis Module Bootstrap
*/
int RedisModule_OnLoad(RedisModuleCtx *ctx) {
  if (RedisModule_Init(ctx, RM_MODULE_NAME, 1, REDISMODULE_APIVER_1) ==
      REDISMODULE_ERR) {
    return REDISMODULE_ERR;
  }

  if (RedisModule_CreateCommand(ctx, "muten.store.insert", MutenStoreInsertCommand, "write deny-oom",
                                1, 1, 1) == REDISMODULE_ERR) {
    return REDISMODULE_ERR;
  }

  if (RedisModule_CreateCommand(ctx, "muten.store.delete", MutenStoreDeleteCommand, "write deny-oom",
                                1, 1, 1) == REDISMODULE_ERR) {
    return REDISMODULE_ERR;
  }

  if (RedisModule_CreateCommand(ctx, "muten.store.debug", MutenStoreDebugCommand, "readonly",
                                1, 1, 1) == REDISMODULE_ERR) {
    return REDISMODULE_ERR;
  }

  RM_LOG_DEBUG(ctx, "module loaded ok.");

  return REDISMODULE_OK;
}
