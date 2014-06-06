/*
 * Copyright 2014 Stefano Ceccherini <stefano.ceccherini@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _H_LOCK
#define _H_LOCK

#include <OS.h>
#include <KernelExport.h>
#include <SupportDefs.h>

typedef sem_id mutex;
#define mutex_init(id, name) *id = create_sem(1, name)
#define mutex_lock(id) acquire_sem(*id)
#define mutex_unlock(id) release_sem(*id)
#define mutex_destroy(id) delete_sem(*id)


class MutexLocker {
public:
	MutexLocker(mutex m) { fMutex = m; mutex_lock(&m); };
	~MutexLocker() { mutex_unlock(&fMutex); };
private:
	mutex fMutex;
};

#endif // _H_LOCK
