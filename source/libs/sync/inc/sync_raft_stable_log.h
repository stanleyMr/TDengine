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

#ifndef _TD_LIBS_SYNC_RAFT_STABLE_LOG_H
#define _TD_LIBS_SYNC_RAFT_STABLE_LOG_H

#include "sync.h"
#include "sync_raft_code.h"
#include "sync_type.h"

SSyncRaftStableLog* syncRaftCreateStableLog(SSyncRaft* pRaft);

// Entries returns a slice of log entries in the range [lo,hi).
// MaxSize limits the total size of the log entries returned, but
// Entries returns at least one entry if any.
int syncRaftStableEntries(SSyncRaftStableLog* storage, SyncIndex lo, SyncIndex hi, 
                          SSyncRaftEntry** ppEntries, int* n);

SyncIndex syncRaftStableLogLastIndex(const SSyncRaftStableLog* storage);

// FirstIndex returns the index of the first log entry that is
// possibly available via Entries (older entries have been incorporated
// into the latest Snapshot; if storage only contains the dummy entry the
// first log entry is not available).
SyncIndex syncRaftStableLogFirstIndex(const SSyncRaftStableLog* storage);

// Term returns the term of entry i, which must be in the range
// [FirstIndex()-1, LastIndex()]. The term of the entry before
// FirstIndex is retained for matching purposes even though the
// rest of that entry may not be available.
SyncTerm syncRaftStableLogTerm(const SSyncRaftStableLog*, SyncIndex, ESyncRaftCode*);

int syncRaftStableAppendEntries(SSyncRaftStableLog* storage, SSyncRaftEntry* entries, int n);

void syncRaftStableLogVisit(const SSyncRaftStableLog* storage, SyncIndex lo, SyncIndex hi, visitEntryFp visit, void* arg);

#endif // _TD_LIBS_SYNC_RAFT_STABLE_LOG_H
