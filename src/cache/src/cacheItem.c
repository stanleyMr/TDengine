/*
 * Copyright (c) 2019 TAOS Data, Inc. <cli@taosdata.com>
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

#include <assert.h>
#include <string.h>
#include "cacheTable.h"
#include "cacheint.h"
#include "cacheLru.h"
#include "cacheItem.h"
#include "cacheSlab.h"

static void cacheItemFree(cache_t* pCache, cacheItem* pItem);

static void cacheItemUpdateInColdLruList(cacheItem* pItem, uint64_t now);

cacheItem* cacheAllocItem(cache_t* cache, uint8_t nkey, uint32_t nbytes, uint64_t expireTime) {
  size_t ntotal = cacheItemTotalBytes(nkey, nbytes);
  uint32_t id = cacheSlabId(cache, ntotal);
  cacheItem* pItem = NULL;

  if (ntotal > 10240) { /* chunk pItem */

  } else {
    pItem = cacheSlabAllocItem(cache, ntotal, id);
  }

  if (pItem == NULL) {
    return NULL;
  }

  memset(pItem, 0, sizeof(cacheItem));

  itemIncrRef(pItem);
  item_set_used(pItem);

  pItem->next = pItem->prev = NULL;
  pItem->expireTime = expireTime;
  if (expireTime == 0) {
    /* never expire, add to never expire list */
    pItem->next = cache->neverExpireItemHead;
    if (cache->neverExpireItemHead) cache->neverExpireItemHead->prev = pItem;
    cache->neverExpireItemHead = pItem;
  } else {
    /* add to hot lru slab list */    
    pItem->slabLruId = id | CACHE_LRU_HOT;
    cacheLruLinkItem(cache, pItem, true);
  }

  return pItem;
}

void cacheItemUnlink(cacheTable* pTable, cacheItem* pItem) {
  assert(pItem->pTable == pTable);
  if (item_is_used(pItem)) {
    item_unset_used(pItem);
    cacheTableRemove(pTable, item_key(pItem), pItem->nkey);
    cacheLruUnlinkItem(pTable->pCache, pItem, true);
    cacheItemRemove(pTable, pItem);
  }
}

void cacheItemRemove(cache_t* pCache, cacheItem* pItem) {
  assert(item_is_freed(pItem));
  assert(pItem->refCount > 0);

  if (itemDecrRef(pItem) == 0) {
    cacheItemFree(pCache, pItem);
  }
}

void cacheItemBump(cacheTable* pTable, cacheItem* pItem, uint64_t now) {
  if (item_is_active(pItem)) {
    /* already is active pItem, return */
    itemDecrRef(pItem);
    return;
  }

  if (!item_is_fetched(pItem)) {
    /* access only one time, make it as fetched */
    itemDecrRef(pItem);
    item_set_fetched(pItem);
    return;
  }

  /* already mark as fetched, mark it as active */
  item_set_actived(pItem);

  if (item_slablru_id(pItem) != CACHE_LRU_COLD) {
    pItem->lastTime = now;
    itemDecrRef(pItem);
    return;
  }

  cacheItemUpdateInColdLruList(pItem, now);
}

static void cacheItemUpdateInColdLruList(cacheItem* pItem, uint64_t now) {
  assert(item_is_used(pItem));
  assert(item_slablru_id(pItem) == CACHE_LRU_COLD && item_is_active(pItem));

  cacheTableLockBucket(pItem->pTable, pItem->hash);

  /* update last access time */
  pItem->lastTime = now;

  /* move pItem to warm lru list */
  cacheLruUnlinkItem(pItem->pTable->pCache, pItem, true);
  pItem->slabLruId = item_cls_id(pItem) | CACHE_LRU_WARM;
  cacheLruLinkItem(pItem->pTable->pCache, pItem, true);

  itemDecrRef(pItem);

  cacheTableUnlockBucket(pItem->pTable, pItem->hash);
}

static void cacheItemFree(cache_t* pCache, cacheItem* pItem) {
  assert(pItem->refCount == 0);
  assert(item_is_freed(pItem));

  cacheSlabFreeItem(pCache, pItem, false);
}

/*

void cacheItemMoveToLruHead(cache_t* cache, cacheItem* pItem) {
  cacheSlabLruClass* lru = &(cache->lruArray[item_lru_id(pItem)]);
  cacheMutexLock(&(lru->mutex));

  cacheLruUnlinkItem(cache, pItem, false);
  cacheItemLinkToLru(cache, pItem, false);

  cacheMutexUnlock(&(lru->mutex));
}


void cacheItemUnlinkNolock(cacheTable* pTable, cacheItem* pItem) {
  if (item_is_used(pItem)) {
    item_unset_used(pItem);
    cacheTableRemove(pTable, item_key(pItem), pItem->nkey);
    cacheLruUnlinkItem(pTable->pCache, pItem, false);
    cacheItemRemove(pTable->pCache, pItem);
  }
}

*/