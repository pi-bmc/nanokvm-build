/*
 * Copyright (C) Cvitek Co., Ltd. 2019-2020. All rights reserved.
 *
 * File Name: include/module_common.h
 * Description: Common video definitions.
 */

#ifndef __MODULE_COMMON_H__
#define __MODULE_COMMON_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define MUTEX_INIT(MUTEX_HANDLE, ATTR) mutex_init(MUTEX_HANDLE)
#define MUTEX_DESTROY(MUTEX_HANDLE) mutex_destroy(MUTEX_HANDLE)
/*
 * Was mutex_lock_interruptible(), whose __must_check return every caller
 * discarded -- on a signal that means proceeding *without the lock*. Callers
 * are void and have no way to report failure, so take it uninterruptibly.
 *
 * A few callers (e.g. vdi.c) still assign the result, so it stays an
 * expression and evaluates to 0 -- the success value they already expect.
 */
#define MUTEX_LOCK(MUTEX_HANDLE) ({ mutex_lock(MUTEX_HANDLE); 0; })
#define MUTEX_UNLOCK(MUTEX_HANDLE) mutex_unlock(MUTEX_HANDLE)
#define MEM_MALLOC(SIZE) vmalloc(SIZE)
#define MEM_CALLOC(NUMBER, SIZE) vzalloc(SIZE * NUMBER)
#define MEM_FREE(PTR) vfree(PTR)
#define SEMA_INIT(P_SEM, SHARED, VALUE) sema_init(P_SEM, VALUE)
#define SEMA_DESTROY(P_SEM)
#define SEMA_WAIT(P_SEM) down_interruptible(P_SEM)
#define SEMA_TRYWAIT(P_SEM) down_trylock(P_SEM)
#define SEMA_TIMEWAIT(P_SEM, P_TS) down_timeout(P_SEM, P_TS)
#define SEMA_POST(P_SEM) up(P_SEM)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
