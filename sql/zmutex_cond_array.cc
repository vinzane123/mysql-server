/* Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

#include "zgtids.h"
#include "my_sys.h"
#include "sql_class.h"


Mutex_cond_array::Mutex_cond_array(Checkable_rwlock *_global_lock)
  : global_lock(_global_lock)
{
  DBUG_ENTER("Mutex_cond_array::Mutex_cond_array");
  my_init_dynamic_array(&array, sizeof(Mutex_cond *), 0, 8);
  DBUG_VOID_RETURN;
}


Mutex_cond_array::~Mutex_cond_array()
{
  DBUG_ENTER("Mutex_cond_array::~Mutex_cond_array");
  // destructor should only be called when no other thread may access object
  //global_lock->assert_no_lock();
  // need to hold lock before calling get_max_sidno
  global_lock->rdlock();
  int max_index= get_max_index();
  for (int i= 0; i <= max_index; i++)
  {
    Mutex_cond *mutex_cond= get_mutex_cond(i);
    if (mutex_cond)
    {
      mysql_mutex_destroy(&mutex_cond->mutex);
      mysql_cond_destroy(&mutex_cond->cond);
      free(mutex_cond);
    }
  }
  delete_dynamic(&array);
  global_lock->unlock();
  DBUG_VOID_RETURN;
}


void Mutex_cond_array::enter_cond(THD *thd, int n, PSI_stage_info *stage,
                                  PSI_stage_info *old_stage) const
{
  DBUG_ENTER("Mutex_cond_array::enter_cond");
  Mutex_cond *mutex_cond= get_mutex_cond(n);
  thd->ENTER_COND(&mutex_cond->cond, &mutex_cond->mutex, stage, old_stage);
  DBUG_VOID_RETURN;
}


enum_return_status Mutex_cond_array::ensure_index(int n)
{
  DBUG_ENTER("Mutex_cond_array::ensure_index");
  global_lock->assert_some_lock();
  int max_index= get_max_index();
  if (n > max_index)
  {
    bool is_wrlock= global_lock->is_wrlock();
    if (!is_wrlock)
    {
      global_lock->unlock();
      global_lock->wrlock();
    }
    if (n > max_index)
    {
      if (allocate_dynamic(&array, n + 1))
        goto error;
      for (int i= max_index + 1; i <= n; i++)
      {
        Mutex_cond *mutex_cond= (Mutex_cond *)malloc(sizeof(Mutex_cond));
        if (mutex_cond == NULL)
          goto error;
        mysql_mutex_init(0, &mutex_cond->mutex, NULL);
        mysql_cond_init(0, &mutex_cond->cond, NULL);
        insert_dynamic(&array, &mutex_cond);
        DBUG_ASSERT(&get_mutex_cond(i)->mutex == &mutex_cond->mutex);
      }
    }
    if (!is_wrlock)
    {
      global_lock->unlock();
      global_lock->rdlock();
    }
  }
  RETURN_OK;
error:
  global_lock->unlock();
  global_lock->rdlock();
  BINLOG_ERROR(("Out of memory."), (ER_OUT_OF_RESOURCES, MYF(0)));
  RETURN_REPORTED_ERROR;
}
