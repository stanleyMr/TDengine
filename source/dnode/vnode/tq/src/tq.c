/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "tqInt.h"
#include "osSocket.h"
#include "tqMetaStore.h"
#include "osAtomic.h"

// static
// read next version data
//
// send to fetch queue
//
// handle management message
//

int tqGroupSSize(const STqGroup* pGroup);
int tqTopicSSize();
int tqItemSSize();

void* tqSerializeListHandle(STqList* listHandle, void* ptr);
void* tqSerializeTopic(STqTopic* pTopic, void* ptr);
void* tqSerializeItem(STqMsgItem* pItem, void* ptr);

const void* tqDeserializeTopic(const void* pBytes, STqTopic* pTopic);
const void* tqDeserializeItem(const void* pBytes, STqMsgItem* pItem);

STQ* tqOpen(const char* path, STqCfg* tqConfig, STqLogReader* tqLogReader, SMemAllocatorFactory* allocFac) {
  STQ* pTq = malloc(sizeof(STQ));
  if (pTq == NULL) {
    terrno = TSDB_CODE_TQ_OUT_OF_MEMORY;
    return NULL;
  }
  pTq->path = strdup(path);
  pTq->tqConfig = tqConfig;
  pTq->tqLogReader = tqLogReader;
  pTq->tqMemRef.pAlloctorFactory = allocFac;
  pTq->tqMemRef.pAllocator = allocFac->create(allocFac);
  if (pTq->tqMemRef.pAllocator == NULL) {
    // TODO: error code of buffer pool
  }
  pTq->tqMeta = tqStoreOpen(path, (FTqSerialize)tqSerializeGroup, (FTqDeserialize)tqDeserializeGroup, free, 0);
  if (pTq->tqMeta == NULL) {
    free(pTq);
    allocFac->destroy(allocFac, pTq->tqMemRef.pAllocator);
    return NULL;
  }

  return pTq;
}

void tqClose(STQ* pTq) {
  // TODO
}

static int tqProtoCheck(STqMsgHead* pMsg) {
  // TODO
  return pMsg->protoVer == 0;
}

static int tqAckOneTopic(STqTopic* pTopic, STqOneAck* pAck, STqQueryMsg** ppQuery) {
  // clean old item and move forward
  int32_t consumeOffset = pAck->consumeOffset;
  int     idx = consumeOffset % TQ_BUFFER_SIZE;
  ASSERT(pTopic->buffer[idx].content && pTopic->buffer[idx].executor);
  tfree(pTopic->buffer[idx].content);
  if (1 /* TODO: need to launch new query */) {
    STqQueryMsg* pNewQuery = malloc(sizeof(STqQueryMsg));
    if (pNewQuery == NULL) {
      terrno = TSDB_CODE_TQ_OUT_OF_MEMORY;
      return -1;
    }
    // TODO: lock executor
    // TODO: read from wal and assign to src
    /*pNewQuery->exec->executor = pTopic->buffer[idx].executor;*/
    /*pNewQuery->exec->src = 0;*/
    /*pNewQuery->exec->dest = &pTopic->buffer[idx];*/
    /*pNewQuery->next = *ppQuery;*/
    /**ppQuery = pNewQuery;*/
  }
  return 0;
}

static int tqAck(STqGroup* pGroup, STqAcks* pAcks) {
  int32_t    ackNum = pAcks->ackNum;
  STqOneAck* acks = pAcks->acks;
  // double ptr for acks and list
  int          i = 0;
  STqList*     node = pGroup->head;
  int          ackCnt = 0;
  STqQueryMsg* pQuery = NULL;
  while (i < ackNum && node->next) {
    if (acks[i].topicId == node->next->topic.topicId) {
      ackCnt++;
      tqAckOneTopic(&node->next->topic, &acks[i], &pQuery);
    } else if (acks[i].topicId < node->next->topic.topicId) {
      i++;
    } else {
      node = node->next;
    }
  }
  if (pQuery) {
    // post message
  }
  return ackCnt;
}

static int tqCommitGroup(STqGroup* pGroup) {
  // persist modification into disk
  return 0;
}

int tqCreateGroup(STQ* pTq, int64_t topicId, int64_t cgId, int64_t cId, STqGroup** ppGroup) {
  // create in disk
  STqGroup* pGroup = (STqGroup*)malloc(sizeof(STqGroup));
  if (pGroup == NULL) {
    // TODO
    return -1;
  }
  memset(pGroup, 0, sizeof(STqGroup));

  pGroup->topicList = tdListNew(sizeof(STqTopic));
  if(pGroup->topicList == NULL) {
    free(pGroup);
    return -1;
  }
  *ppGroup = pGroup;

  return 0;
}

STqGroup* tqOpenGroup(STQ* pTq, int64_t topicId, int64_t cgId, int64_t cId) {
  STqGroup* pGroup = tqHandleGet(pTq->tqMeta, cId);
  if (pGroup == NULL) {
    int code = tqCreateGroup(pTq, topicId, cgId, cId, &pGroup);
    if (code < 0) {
      // TODO
      return NULL;
    }
    tqHandleMovePut(pTq->tqMeta, cId, pGroup);
  }
  ASSERT(pGroup);

  return pGroup;
}

int tqCloseGroup(STQ* pTq, int64_t topicId, int64_t cgId, int64_t cId) {
  // TODO
  return 0;
}

int tqDropGroup(STQ* pTq, int64_t topicId, int64_t cgId, int64_t cId) {
  // delete from disk
  return 0;
}

static int tqFetch(STqGroup* pGroup, STqConsumeRsp** pRsp) {
  STqList* pHead = pGroup->head;
  STqList* pNode = pHead;
  int      totSize = 0;
  int      numOfMsgs = 0;
  // TODO: make it a macro
  int   sizeLimit = 4 * 1024;

  void* ptr = realloc(*pRsp, sizeof(STqConsumeRsp) + sizeLimit);
  if (ptr == NULL) {
    terrno = TSDB_CODE_TQ_OUT_OF_MEMORY;
    return -1;
  }
  *pRsp = ptr;
  STqMsgContent* buffer = (*pRsp)->msgs;

  // iterate the list to get msgs of all topics
  // until all topic iterated or msgs over sizeLimit
  while (pNode->next) {
    pNode = pNode->next;
    STqTopic* pTopic = &pNode->topic;
    int       idx = pTopic->nextConsumeOffset % TQ_BUFFER_SIZE;
    if (pTopic->buffer[idx].content != NULL && pTopic->buffer[idx].offset == pTopic->nextConsumeOffset) {
      totSize += pTopic->buffer[idx].size;
      if (totSize > sizeLimit) {
        void* ptr = realloc(*pRsp, sizeof(STqConsumeRsp) + totSize);
        if (ptr == NULL) {
          totSize -= pTopic->buffer[idx].size;
          terrno = TSDB_CODE_TQ_OUT_OF_MEMORY;
          // return msgs already copied
          break;
        }
        *pRsp = ptr;
        break;
      }
      *((int64_t*)buffer) = pTopic->topicId;
      buffer = POINTER_SHIFT(buffer, sizeof(int64_t));
      *((int64_t*)buffer) = pTopic->buffer[idx].size;
      buffer = POINTER_SHIFT(buffer, sizeof(int64_t));
      memcpy(buffer, pTopic->buffer[idx].content, pTopic->buffer[idx].size);
      buffer = POINTER_SHIFT(buffer, pTopic->buffer[idx].size);
      numOfMsgs++;
      if (totSize > sizeLimit) {
        break;
      }
    }
  }
  (*pRsp)->bodySize = totSize;
  return numOfMsgs;
}

STqGroup* tqGetGroup(STQ* pTq, int64_t clientId) { return tqHandleGet(pTq->tqMeta, clientId); }

int tqSendLaunchQuery(STqMsgItem* bufItem, int64_t offset) {
  if (tqQueryExecuting(bufItem->status)) {
    return 0;
  }
  bufItem->status = 1;
  // load data from wal or buffer pool
  // put into exec
  // send exec into non blocking queue
  // when query finished, put into buffer pool
  return 0;
}

/*int tqMoveOffsetToNext(TqGroupHandle* gHandle) {*/
/*return 0;*/
/*}*/

int tqPushMsg(STQ* pTq, void* p, int64_t version) {
  // add reference
  // judge and launch new query
  return 0;
}

int tqCommit(STQ* pTq) {
  // do nothing
  return 0;
}

int tqBufferSetOffset(STqTopic* pTopic, int64_t offset) {
  int code;
  memset(pTopic->buffer, 0, sizeof(pTopic->buffer));
  // launch query
  for (int i = offset; i < offset + TQ_BUFFER_SIZE; i++) {
    int pos = i % TQ_BUFFER_SIZE;
    code = tqSendLaunchQuery(&pTopic->buffer[pos], offset);
    if (code < 0) {
      // TODO: error handling
    }
  }
  // set offset
  pTopic->nextConsumeOffset = offset;
  pTopic->floatingCursor = offset;
  return 0;
}

STqTopic* tqFindTopic(STqGroup* pGroup, int64_t topicId) {
  // TODO
  return NULL;
}

int tqSetCursor(STQ* pTq, STqSetCurReq* pMsg) {
  int       code;
  int64_t   clientId = pMsg->head.clientId;
  int64_t   topicId = pMsg->topicId;
  int64_t   offset = pMsg->offset;
  STqGroup* gHandle = tqGetGroup(pTq, clientId);
  if (gHandle == NULL) {
    // client not connect
    return -1;
  }
  STqTopic* topicHandle = tqFindTopic(gHandle, topicId);
  if (topicHandle == NULL) {
    return -1;
  }
  if (pMsg->offset == topicHandle->nextConsumeOffset) {
    return 0;
  }
  // TODO: check log last version

  code = tqBufferSetOffset(topicHandle, offset);
  if (code < 0) {
    // set error code
    return -1;
  }

  return 0;
}

// temporary
int tqProcessCMsg(STQ* pTq, STqConsumeReq* pMsg, STqRspHandle* pRsp) {
  int64_t   clientId = pMsg->head.clientId;
  STqGroup* pGroup = tqGetGroup(pTq, clientId);
  if (pGroup == NULL) {
    terrno = TSDB_CODE_TQ_GROUP_NOT_SET;
    return -1;
  }
  pGroup->rspHandle.handle = pRsp->handle;
  pGroup->rspHandle.ahandle = pRsp->ahandle;

  return 0;
}

int tqConsume(STQ* pTq, SRpcMsg* pReq, SRpcMsg** pRsp) {
  STqConsumeReq *pMsg = pReq->pCont;
  int64_t   clientId = pMsg->head.clientId;
  STqGroup* pGroup = tqGetGroup(pTq, clientId);
  if (pGroup == NULL) {
    terrno = TSDB_CODE_TQ_GROUP_NOT_SET;
    return -1;
  }

  SList* topicList = pGroup->topicList;

  int totSize = 0;
  int numOfMsgs = 0;
  int sizeLimit = 4096;


  STqConsumeRsp *pCsmRsp = (*pRsp)->pCont;
  void* ptr = realloc((*pRsp)->pCont, sizeof(STqConsumeRsp) + sizeLimit);
  if (ptr == NULL) {
    terrno = TSDB_CODE_TQ_OUT_OF_MEMORY;
    return -1;
  }
  (*pRsp)->pCont = ptr;

  SListIter iter;
  tdListInitIter(topicList, &iter, TD_LIST_FORWARD);

  STqMsgContent* buffer = NULL;
  SArray* pArray = taosArrayInit(0, sizeof(void*));

  SListNode *pn;
  while((pn = tdListNext(&iter)) != NULL) {
    STqTopic* pTopic = *(STqTopic**)pn->data;
    int idx = pTopic->floatingCursor % TQ_BUFFER_SIZE;
    STqMsgItem* pItem = &pTopic->buffer[idx];
    if (pItem->content != NULL && pItem->offset == pTopic->floatingCursor) {
      if(pItem->status == TQ_ITEM_READY) {
        //if has data
        totSize += pTopic->buffer[idx].size;
        if (totSize > sizeLimit) {
          void* ptr = realloc((*pRsp)->pCont, sizeof(STqConsumeRsp) + totSize);
          if (ptr == NULL) {
            totSize -= pTopic->buffer[idx].size;
            terrno = TSDB_CODE_TQ_OUT_OF_MEMORY;
            // return msgs already copied
            break;
          }
          (*pRsp)->pCont = ptr;
          break;
        }
        *((int64_t*)buffer) = htonll(pTopic->topicId);
        buffer = POINTER_SHIFT(buffer, sizeof(int64_t));
        *((int64_t*)buffer) = htonll(pTopic->buffer[idx].size);
        buffer = POINTER_SHIFT(buffer, sizeof(int64_t));
        memcpy(buffer, pTopic->buffer[idx].content, pTopic->buffer[idx].size);
        buffer = POINTER_SHIFT(buffer, pTopic->buffer[idx].size);
        numOfMsgs++;
        if (totSize > sizeLimit) {
          break;
        }
      } else if(pItem->status == TQ_ITEM_PROCESS) {
        //if not have data but in process

      } else if(pItem->status == TQ_ITEM_EMPTY){
        //if not have data and not in process
        int32_t old = atomic_val_compare_exchange_32(&pItem->status, TQ_ITEM_EMPTY, TQ_ITEM_PROCESS);
        if(old != TQ_ITEM_EMPTY) {
          continue;
        }
        pItem->offset = pTopic->floatingCursor;
        taosArrayPush(pArray, &pItem);
      } else {
        ASSERT(0);
      }

    }
  }

  for(int i = 0; i < pArray->size; i++) {
    STqMsgItem* pItem = taosArrayGet(pArray, i);

    void* raw;
    //read from wal
    //get msgType
    //if submitblk
    pItem->executor->assign(pItem->executor->runtimeEnv, raw);
    SSDataBlock* content = pItem->executor->exec(pItem->executor->runtimeEnv);
    pItem->content = content;
    //if other type, send just put into buffer
    pItem->content = raw;

    int32_t old = atomic_val_compare_exchange_32(&pItem->status, TQ_ITEM_PROCESS, TQ_ITEM_READY);
    ASSERT(old == TQ_ITEM_PROCESS);
    
  }

  if (numOfMsgs < 0) {
    return -1;
  }

  if (numOfMsgs == 0) {
    // most recent data has been fetched

    // enable timer for blocking wait
    // once new data written when waiting, launch query and rsp
    return -1;
  }

  // fetched a num of msgs, rpc response

  return 0;
}

#if 0
int tqConsume(STQ* pTq, STqConsumeReq* pMsg) {
  if (!tqProtoCheck((STqMsgHead*)pMsg)) {
    // proto version invalid
    return -1;
  }
  int64_t   clientId = pMsg->head.clientId;
  STqGroup* pGroup = tqGetGroup(pTq, clientId);
  if (pGroup == NULL) {
    // client not connect
    return -1;
  }
  if (pMsg->acks.ackNum != 0) {
    if (tqAck(pGroup, &pMsg->acks) != 0) {
      // ack not success
      return -1;
    }
  }

  STqConsumeRsp* pRsp = (STqConsumeRsp*)pMsg;

  if (tqFetch(pGroup, (void**)&pRsp->msgs) <= 0) {
    // fetch error
    return -1;
  }

  // judge and launch new query
  /*if (tqSendLaunchQuery(gHandle)) {*/
  // launch query error
  /*return -1;*/
  /*}*/
  return 0;
}
#endif

int tqSerializeGroup(const STqGroup* pGroup, STqSerializedHead** ppHead) {
  // calculate size
  int sz = tqGroupSSize(pGroup) + sizeof(STqSerializedHead);
  if (sz > (*ppHead)->ssize) {
    void* tmpPtr = realloc(*ppHead, sz);
    if (tmpPtr == NULL) {
      free(*ppHead);
      // TODO: memory err
      return -1;
    }
    *ppHead = tmpPtr;
    (*ppHead)->ssize = sz;
  }
  void* ptr = (*ppHead)->content;
  // do serialization
  *(int64_t*)ptr = pGroup->clientId;
  ptr = POINTER_SHIFT(ptr, sizeof(int64_t));
  *(int64_t*)ptr = pGroup->cgId;
  ptr = POINTER_SHIFT(ptr, sizeof(int64_t));
  *(int32_t*)ptr = pGroup->topicNum;
  ptr = POINTER_SHIFT(ptr, sizeof(int32_t));
  if (pGroup->topicNum > 0) {
    tqSerializeListHandle(pGroup->head, ptr);
  }
  return 0;
}

void* tqSerializeListHandle(STqList* listHandle, void* ptr) {
  STqList* node = listHandle;
  ASSERT(node != NULL);
  while (node) {
    ptr = tqSerializeTopic(&node->topic, ptr);
    node = node->next;
  }
  return ptr;
}

void* tqSerializeTopic(STqTopic* pTopic, void* ptr) {
  *(int64_t*)ptr = pTopic->nextConsumeOffset;
  ptr = POINTER_SHIFT(ptr, sizeof(int64_t));
  *(int64_t*)ptr = pTopic->topicId;
  ptr = POINTER_SHIFT(ptr, sizeof(int64_t));
  *(int32_t*)ptr = pTopic->head;
  ptr = POINTER_SHIFT(ptr, sizeof(int32_t));
  *(int32_t*)ptr = pTopic->tail;
  ptr = POINTER_SHIFT(ptr, sizeof(int32_t));
  for (int i = 0; i < TQ_BUFFER_SIZE; i++) {
    ptr = tqSerializeItem(&pTopic->buffer[i], ptr);
  }
  return ptr;
}

void* tqSerializeItem(STqMsgItem* bufItem, void* ptr) {
  // TODO: do we need serialize this?
  // mainly for executor
  return ptr;
}

const void* tqDeserializeGroup(const STqSerializedHead* pHead, STqGroup** ppGroup) {
  STqGroup*   gHandle = *ppGroup;
  const void* ptr = pHead->content;
  gHandle->clientId = *(int64_t*)ptr;
  ptr = POINTER_SHIFT(ptr, sizeof(int64_t));
  gHandle->cgId = *(int64_t*)ptr;
  ptr = POINTER_SHIFT(ptr, sizeof(int64_t));
  gHandle->ahandle = NULL;
  gHandle->topicNum = *(int32_t*)ptr;
  ptr = POINTER_SHIFT(ptr, sizeof(int32_t));
  gHandle->head = NULL;
  STqList* node = gHandle->head;
  for (int i = 0; i < gHandle->topicNum; i++) {
    if (gHandle->head == NULL) {
      if ((node = malloc(sizeof(STqList))) == NULL) {
        // TODO: error
        return NULL;
      }
      node->next = NULL;
      ptr = tqDeserializeTopic(ptr, &node->topic);
      gHandle->head = node;
    } else {
      node->next = malloc(sizeof(STqList));
      if (node->next == NULL) {
        // TODO: error
        return NULL;
      }
      node->next->next = NULL;
      ptr = tqDeserializeTopic(ptr, &node->next->topic);
      node = node->next;
    }
  }
  return ptr;
}

const void* tqDeserializeTopic(const void* pBytes, STqTopic* topic) {
  const void* ptr = pBytes;
  topic->nextConsumeOffset = *(int64_t*)ptr;
  ptr = POINTER_SHIFT(ptr, sizeof(int64_t));
  topic->topicId = *(int64_t*)ptr;
  ptr = POINTER_SHIFT(ptr, sizeof(int64_t));
  topic->head = *(int32_t*)ptr;
  ptr = POINTER_SHIFT(ptr, sizeof(int32_t));
  topic->tail = *(int32_t*)ptr;
  ptr = POINTER_SHIFT(ptr, sizeof(int32_t));
  for (int i = 0; i < TQ_BUFFER_SIZE; i++) {
    ptr = tqDeserializeItem(ptr, &topic->buffer[i]);
  }
  return ptr;
}

const void* tqDeserializeItem(const void* pBytes, STqMsgItem* bufItem) { return pBytes; }

// TODO: make this a macro
int tqGroupSSize(const STqGroup* gHandle) {
  return sizeof(int64_t) * 2  // cId + cgId
         + sizeof(int32_t)    // topicNum
         + gHandle->topicNum * tqTopicSSize();
}

// TODO: make this a macro
int tqTopicSSize() {
  return sizeof(int64_t) * 2    // nextConsumeOffset + topicId
         + sizeof(int32_t) * 2  // head + tail
         + TQ_BUFFER_SIZE * tqItemSSize();
}

int tqItemSSize() {
  // TODO: do this need serialization?
  // mainly for executor
  return 0;
}
