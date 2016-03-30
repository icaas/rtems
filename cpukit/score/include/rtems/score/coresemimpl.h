/**
 * @file
 *
 * @brief Inlined Routines Associated with the SuperCore Semaphore
 *
 * This include file contains all of the inlined routines associated
 * with the SuperCore semaphore.
 */

/*
 *  COPYRIGHT (c) 1989-2006.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.org/license/LICENSE.
 */

#ifndef _RTEMS_SCORE_CORESEMIMPL_H
#define _RTEMS_SCORE_CORESEMIMPL_H

#include <rtems/score/coresem.h>
#include <rtems/score/objectimpl.h>
#include <rtems/score/threaddispatch.h>
#include <rtems/score/threadqimpl.h>
#include <rtems/score/statesimpl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup ScoreSemaphore
 */
/**@{**/

/**
 *  Core Semaphore handler return statuses.
 */
typedef enum {
  /** This status indicates that the operation completed successfully. */
  CORE_SEMAPHORE_STATUS_SUCCESSFUL,
  /** This status indicates that the calling task did not want to block
   *  and the operation was unable to complete immediately because the
   *  resource was unavailable.
   */
  CORE_SEMAPHORE_STATUS_UNSATISFIED_NOWAIT,
  /** This status indicates that the thread was blocked waiting for an
   *  operation to complete and the semaphore was deleted.
   */
  CORE_SEMAPHORE_WAS_DELETED,
  /** This status indicates that the calling task was willing to block
   *  but the operation was unable to complete within the time allotted
   *  because the resource never became available.
   */
  CORE_SEMAPHORE_TIMEOUT,
  /** This status indicates that an attempt was made to unlock the semaphore
   *  and this would have made its count greater than that allowed.
   */
  CORE_SEMAPHORE_MAXIMUM_COUNT_EXCEEDED
}   CORE_semaphore_Status;

/**
 *  @brief Core semaphore last status value.
 *
 *  This is the last status value.
 */
#define CORE_SEMAPHORE_STATUS_LAST CORE_SEMAPHORE_MAXIMUM_COUNT_EXCEEDED

/**
 *  The following type defines the callout which the API provides
 *  to support global/multiprocessor operations on semaphores.
 */
typedef void ( *CORE_semaphore_API_mp_support_callout )(
                 Thread_Control *,
                 Objects_Id
             );

/**
 *  @brief Initialize the semaphore based on the parameters passed.
 *
 *  This package is the implementation of the CORE Semaphore Handler.
 *  This core object utilizes standard Dijkstra counting semaphores to provide
 *  synchronization and mutual exclusion capabilities.
 *
 *  This routine initializes the semaphore based on the parameters passed.
 *
 *  @param[in] the_semaphore is the semaphore to initialize
 *  @param[in] discipline the blocking discipline
 *  @param[in] initial_value is the initial count of the semaphore
 */
void _CORE_semaphore_Initialize(
  CORE_semaphore_Control     *the_semaphore,
  CORE_semaphore_Disciplines  discipline,
  uint32_t                    initial_value
);

RTEMS_INLINE_ROUTINE void _CORE_semaphore_Destroy(
  CORE_semaphore_Control *the_semaphore
)
{
  _Thread_queue_Destroy( &the_semaphore->Wait_queue );
}

/**
 *  @brief Surrender a unit to a semaphore.
 *
 *  This routine frees a unit to the semaphore.  If a task was blocked waiting
 *  for a unit from this semaphore, then that task will be readied and the unit
 *  given to that task.  Otherwise, the unit will be returned to the semaphore.
 *
 *  @param[in] the_semaphore is the semaphore to surrender
 *  @param[in] id is the Id of the API level Semaphore object associated
 *         with this instance of a SuperCore Semaphore
 *  @param[in] api_semaphore_mp_support is the routine to invoke if the
 *         thread unblocked is remote
 *  @param[in] lock_context is a temporary variable used to contain the ISR
 *        disable level cookie
 *
 *  @retval an indication of whether the routine succeeded or failed
 */
RTEMS_INLINE_ROUTINE CORE_semaphore_Status _CORE_semaphore_Surrender(
  CORE_semaphore_Control                *the_semaphore,
  Objects_Id                             id,
  CORE_semaphore_API_mp_support_callout  api_semaphore_mp_support,
  ISR_lock_Context                      *lock_context
)
{
  Thread_Control *the_thread;
  CORE_semaphore_Status status;

  status = CORE_SEMAPHORE_STATUS_SUCCESSFUL;

  _Thread_queue_Acquire_critical( &the_semaphore->Wait_queue, lock_context );

  the_thread = _Thread_queue_First_locked(
    &the_semaphore->Wait_queue,
    the_semaphore->operations
  );
  if ( the_thread != NULL ) {
#if defined(RTEMS_MULTIPROCESSING)
    _Thread_Dispatch_disable();
#endif

    _Thread_queue_Extract_critical(
      &the_semaphore->Wait_queue.Queue,
      the_semaphore->operations,
      the_thread,
      lock_context
    );

#if defined(RTEMS_MULTIPROCESSING)
    if ( !_Objects_Is_local_id( the_thread->Object.id ) )
      (*api_semaphore_mp_support) ( the_thread, id );

    _Thread_Dispatch_enable( _Per_CPU_Get() );
#endif
  } else {
    if ( the_semaphore->count < UINT32_MAX )
      the_semaphore->count += 1;
    else
      status = CORE_SEMAPHORE_MAXIMUM_COUNT_EXCEEDED;

    _Thread_queue_Release( &the_semaphore->Wait_queue, lock_context );
  }

  return status;
}

/* Must be a macro due to the multiprocessing dependent parameters */
#define _CORE_semaphore_Flush( \
  the_semaphore, \
  status, \
  mp_callout, \
  mp_id \
) \
  _Thread_queue_Flush( \
    &( the_semaphore )->Wait_queue, \
    ( the_semaphore )->operations, \
    status, \
    mp_callout, \
    mp_id \
  )

/**
 * This routine returns the current count associated with the semaphore.
 *
 * @param[in] the_semaphore is the semaphore to obtain the count of
 *
 * @return the current count of this semaphore
 */
RTEMS_INLINE_ROUTINE uint32_t  _CORE_semaphore_Get_count(
  CORE_semaphore_Control  *the_semaphore
)
{
  return the_semaphore->count;
}

/**
 * This routine attempts to receive a unit from the_semaphore.
 * If a unit is available or if the wait flag is false, then the routine
 * returns.  Otherwise, the calling task is blocked until a unit becomes
 * available.
 *
 * @param[in] the_semaphore is the semaphore to obtain
 * @param[in,out] executing The currently executing thread.
 * @param[in] id is the Id of the owning API level Semaphore object
 * @param[in] wait is true if the thread is willing to wait
 * @param[in] timeout is the maximum number of ticks to block
 * @param[in] lock_context is a temporary variable used to contain the ISR
 *        disable level cookie
 *
 * @note There is currently no MACRO version of this routine.
 */
RTEMS_INLINE_ROUTINE void _CORE_semaphore_Seize(
  CORE_semaphore_Control  *the_semaphore,
  Thread_Control          *executing,
  Objects_Id               id,
  bool                     wait,
  Watchdog_Interval        timeout,
  ISR_lock_Context        *lock_context
)
{
  /* disabled when you get here */

  executing->Wait.return_code = CORE_SEMAPHORE_STATUS_SUCCESSFUL;
  _Thread_queue_Acquire_critical( &the_semaphore->Wait_queue, lock_context );
  if ( the_semaphore->count != 0 ) {
    the_semaphore->count -= 1;
    _Thread_queue_Release( &the_semaphore->Wait_queue, lock_context );
    return;
  }

  if ( !wait ) {
    _Thread_queue_Release( &the_semaphore->Wait_queue, lock_context );
    executing->Wait.return_code = CORE_SEMAPHORE_STATUS_UNSATISFIED_NOWAIT;
    return;
  }

  _Thread_queue_Enqueue_critical(
    &the_semaphore->Wait_queue.Queue,
    the_semaphore->operations,
    executing,
    STATES_WAITING_FOR_SEMAPHORE,
    timeout,
    CORE_SEMAPHORE_TIMEOUT,
    lock_context
  );
}

/** @} */

#ifdef __cplusplus
}
#endif

#endif
/* end of include file */
