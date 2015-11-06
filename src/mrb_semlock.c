/*
** mrb_semlock.c - Semlock class
**
** Copyright (c) Takanori Suzuki 2015
**
** See Copyright Notice in LICENSE
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <errno.h>
#include "mruby.h"
#include "mruby/data.h"
#include "mrb_semlock.h"

#define DONE mrb_gc_arena_restore(mrb, 0);

#ifndef _SEMUN_H
#define _SEMUN_H
union semun {
	int val;
	struct semid_ds *buf;
	unsigned short *array;
};
#endif

typedef struct {
  char *key_str;
  int key_len;
  int semid;
  int removed;
} mrb_semlock_data;

static const struct mrb_data_type mrb_semlock_data_type = {
  "mrb_semlock_data", mrb_free,
};

int wait_init(mrb_state *mrb, int semid) {
	struct semid_ds seminfo;
	union semun sem_setup;
	sem_setup.buf = &seminfo;
	while(TRUE){
		if (-1 == semctl(semid, 1, IPC_STAT, sem_setup)){
			mrb_raise(mrb, E_RUNTIME_ERROR, "failed to get semaphore info by semctl()");
			break;
		}
		if (0 != sem_setup.buf->sem_otime)
			break;
		sleep(1);
	}
}

int sem_lock(mrb_state *mrb, int semid, int slot_num, int readers_num, int wait) {
	int res = 0;
	struct sembuf sem_binary;
	sem_binary.sem_num = slot_num;
	sem_binary.sem_op = -readers_num;
	if (wait){
		sem_binary.sem_flg = SEM_UNDO;
	}else{
		sem_binary.sem_flg = SEM_UNDO | IPC_NOWAIT;
	}
	res = semop(semid, &sem_binary, 1);
	if (res == 0){
		return TRUE;
	}else{
		if (errno == EAGAIN){
			return FALSE;
		}else{
			mrb_raise(mrb, E_RUNTIME_ERROR, "failed to get lock by semop()");
		}
	}
}

int sem_unlock(mrb_state *mrb, int semid, int slot_num, int readers_num, int wait) {
	int res = 0;
	struct sembuf sem_binary;
	sem_binary.sem_num = slot_num;
	sem_binary.sem_op = readers_num;
	if (wait){
		sem_binary.sem_flg = SEM_UNDO;
	}else{
		sem_binary.sem_flg = SEM_UNDO | IPC_NOWAIT;
	}
	res = semop(semid, &sem_binary, 1);
	if (res == 0){
		return TRUE;
	}else{
		if (errno == EAGAIN){
			return FALSE;
		}else{
			mrb_raise(mrb, E_RUNTIME_ERROR, "failed to get lock by semop()");
		}
	}
}

int sem_create(mrb_state *mrb, char *key_str, int prj_num, int nsems, int mode){
	int i;
	int sem_owner = FALSE;
	int semid;
	union semun sem_setup;
	key_t key;
	key = ftok(key_str, prj_num);
	semid = semget(key, nsems + 1, mode|IPC_CREAT|IPC_EXCL);
	if (semid == -1){
		semid = semget(key, nsems + 1, mode|IPC_CREAT);
	} else {
		sem_owner = TRUE;
	}
	if (sem_owner){
		for(i = 0; i < nsems + 1; i++){
			if (i == 0){
				sem_setup.val = (1<<15)-1;
			}else{
				sem_setup.val = 1;
			}
			if (semctl(semid, i, SETVAL, sem_setup) == -1){
				mrb_raise(mrb, E_RUNTIME_ERROR, "failed to set semaphore by semctl()");
			}
		}
	} else {
		wait_init(mrb, semid);
	}
	/* make shared lock for who should remove semaphore */
	sem_lock(mrb, semid, 0, 1, TRUE);
	return semid;
}

int sem_remove(mrb_state *mrb, int semid){
	struct semid_ds seminfo;
	union semun sem_setup;

	sem_setup.buf = &seminfo;
	sem_unlock(mrb, semid, 0, 1, TRUE);
	/* trying to make exclusive lock for who should remove semaphore */
	if (TRUE == sem_lock(mrb, semid, 0, (1<<15) - 1, FALSE)){
		if (semctl(semid, 0, IPC_RMID, sem_setup) == -1) {
			mrb_raise(mrb, E_RUNTIME_ERROR, "failed to remove semaphore by semctl()");
		}
		return TRUE;
	}else{
		return FALSE;
	}
}

static mrb_value mrb_semlock_init(mrb_state *mrb, mrb_value self)
{
  mrb_semlock_data *data;
  char *key_str;
  int key_len;
  int nsems;
  int mode;
  int semid;
  int prj_num;

  data = (mrb_semlock_data *)DATA_PTR(self);
  if (data) {
    mrb_free(mrb, data);
  }
  DATA_TYPE(self) = &mrb_semlock_data_type;
  DATA_PTR(self) = NULL;

  mrb_get_args(mrb, "siii", &key_str, &key_len, &prj_num, &nsems, &mode);

  semid = sem_create(mrb, key_str, prj_num, nsems, mode);

  data = (mrb_semlock_data *)mrb_malloc(mrb, sizeof(mrb_semlock_data));
  data->key_str = key_str;
  data->key_len = key_len;
  data->semid = semid;
  data->removed = FALSE;
  DATA_PTR(self) = data;

  return self;
}

static mrb_value mrb_semlock_lock(mrb_state *mrb, mrb_value self)
{
  mrb_value ret;
  int slot_num;
  int readers_num = 1;
  mrb_bool wait = TRUE;

  mrb_semlock_data *data = DATA_PTR(self);
  mrb_get_args(mrb, "i", &slot_num);
  if (slot_num < 0)
 	 mrb_raise(mrb, E_RANGE_ERROR, "slot_num must be positive number");
  ret = mrb_bool_value(sem_lock(mrb, data->semid, slot_num + 1, readers_num, wait));

  return ret;
}

static mrb_value mrb_semlock_trylock(mrb_state *mrb, mrb_value self)
{
  mrb_value ret;
  int slot_num;
  int readers_num = 1;
  mrb_bool wait = FALSE;

  mrb_semlock_data *data = DATA_PTR(self);
  mrb_get_args(mrb, "i", &slot_num);
  if (slot_num < 0)
 	 mrb_raise(mrb, E_RANGE_ERROR, "slot_num must be positive number");
  ret = mrb_bool_value(sem_lock(mrb, data->semid, slot_num + 1, readers_num, wait));

  return ret;
}

static mrb_value mrb_semlock_unlock(mrb_state *mrb, mrb_value self)
{
  mrb_value ret;
  int slot_num;
  int readers_num = 1;
  mrb_bool wait = TRUE;

  mrb_semlock_data *data = DATA_PTR(self);
  mrb_get_args(mrb, "i", &slot_num);
  if (slot_num < 0)
	mrb_raise(mrb, E_RANGE_ERROR, "slot_num must be positive number");
  ret = mrb_bool_value(sem_unlock(mrb, data->semid, slot_num + 1, readers_num, wait));

  return ret;
}

static mrb_value mrb_semlock_remove(mrb_state *mrb, mrb_value self)
{
  mrb_value ret;
  mrb_semlock_data *data = DATA_PTR(self);
  if (data->removed){
	mrb_raise(mrb, E_RUNTIME_ERROR, "cannot remove more than once");
	ret = mrb_bool_value(FALSE);
  }else{
  	ret = mrb_bool_value(sem_remove(mrb, data->semid));
  	data->removed = TRUE;
  }
  DATA_PTR(self) = data;
  return ret;
}

void mrb_mruby_semlock_gem_init(mrb_state *mrb)
{
    struct RClass *semlock;
    semlock = mrb_define_class(mrb, "Semlock", mrb->object_class);
    mrb_define_method(mrb, semlock, "initialize", mrb_semlock_init, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, semlock, "lock", mrb_semlock_lock, MRB_ARGS_NONE());
    mrb_define_method(mrb, semlock, "trylock", mrb_semlock_trylock, MRB_ARGS_NONE());
    mrb_define_method(mrb, semlock, "unlock", mrb_semlock_unlock, MRB_ARGS_NONE());
    mrb_define_method(mrb, semlock, "remove", mrb_semlock_remove, MRB_ARGS_NONE());
    DONE;
}

void mrb_mruby_semlock_gem_final(mrb_state *mrb)
{
}
