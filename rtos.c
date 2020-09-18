/**
 * @file rtos.c
 * @author ITESO
 * @date Feb 2018
 * @brief Implementation of rtos API
 *
 * This is the implementation of the rtos module for the
 * embedded systems II course at ITESO
 */

#include "rtos.h"
#include "rtos_config.h"
#include "clock_config.h"

#ifdef RTOS_ENABLE_IS_ALIVE
#include "fsl_gpio.h"
#include "fsl_port.h"
#endif
/**********************************************************************************/
// Module defines
/**********************************************************************************/

#define FORCE_INLINE 	__attribute__((always_inline)) inline

#define STACK_FRAME_SIZE			8
#define STACK_LR_OFFSET				2
#define STACK_PSR_OFFSET			1
#define STACK_PSR_DEFAULT			0x01000000

/**********************************************************************************/
// IS ALIVE definitions
/**********************************************************************************/

#ifdef RTOS_ENABLE_IS_ALIVE
#define CAT_STRING(x,y)  		x##y
#define alive_GPIO(x)			CAT_STRING(GPIO,x)
#define alive_PORT(x)			CAT_STRING(PORT,x)
#define alive_CLOCK(x)			CAT_STRING(kCLOCK_Port,x)
static void init_is_alive(void);
static void refresh_is_alive(void);
#endif

/**********************************************************************************/
// Type definitions
/**********************************************************************************/

typedef enum
{
	S_READY = 0, S_RUNNING, S_WAITING, S_SUSPENDED
} task_state_e;
typedef enum
{
	kFromISR = 0, kFromNormalExec
} task_switch_type_e;

typedef struct
{
	uint8_t priority;
	task_state_e state;
	uint32_t *sp;
	void (*task_body)();
	rtos_tick_t local_tick;
	uint32_t reserved[10];
	uint32_t stack[RTOS_STACK_SIZE];
} rtos_tcb_t;

/**********************************************************************************/
// Global (static) task list
/**********************************************************************************/

struct
{
	uint8_t nTasks;
	rtos_task_handle_t current_task;
	rtos_task_handle_t next_task;
	rtos_tcb_t tasks[RTOS_MAX_NUMBER_OF_TASKS + 1];
	rtos_tick_t global_tick;
} task_list =
{ 0 };

/**********************************************************************************/
// Local methods prototypes
/**********************************************************************************/

static void reload_systick(void);
static void dispatcher(task_switch_type_e type);
static void activate_waiting_tasks();
FORCE_INLINE static void context_switch(task_switch_type_e type);
static void idle_task(void);

/**********************************************************************************/
// API implementation
/**********************************************************************************/

void rtos_start_scheduler(void)
{
#ifdef RTOS_ENABLE_IS_ALIVE
	init_is_alive();
#endif
	task_list.global_tick = 0;
	task_list.current_task = -1;
	rtos_create_task(idle_task, 0, kAutoStart);
	SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk
	        | SysTick_CTRL_ENABLE_Msk;
	reload_systick();
	for (;;)
		;
}

rtos_task_handle_t rtos_create_task(void (*task_body)(), uint8_t priority,
		rtos_autostart_e autostart)
{
	rtos_task_handle_t retval = -1; /*Default value for retval*/

	if(task_list.nTasks < RTOS_MAX_NUMBER_OF_TASKS)	/*Create a new task only of there is availability */
	{
		if(kAutoStart == autostart) /*Define state depending on the rtos_autostart_e value*/
		{
			task_list.tasks[task_list.nTasks].state = S_READY;
		}else
		{
			task_list.tasks[task_list.nTasks].state = S_SUSPENDED;
		}

		task_list.tasks[task_list.nTasks].sp = &(task_list.tasks[task_list.nTasks].stack[RTOS_STACK_SIZE-1]) - STACK_FRAME_SIZE; /*Save sp value in the task variable*/
		task_list.tasks[task_list.nTasks].stack[RTOS_STACK_SIZE - STACK_PSR_OFFSET] = STACK_PSR_DEFAULT;
		task_list.tasks[task_list.nTasks].stack[RTOS_STACK_SIZE - STACK_LR_OFFSET ] = (uint32_t)task_body;
		task_list.tasks[task_list.nTasks].local_tick = 0; /*Initialize the tick variable*/
		task_list.tasks[task_list.nTasks].priority = priority; /*Establish the priority*/
		retval = task_list.nTasks; /*Save the actual number of tasks*/
		task_list.nTasks++; /*Increase the number of tasks*/
	}
	return retval;
}

rtos_tick_t rtos_get_clock(void)
{
	return (task_list.global_tick); /*Return the glocal tick value*/
}

void rtos_delay(rtos_tick_t ticks)
{
	task_list.tasks[task_list.current_task].state = S_WAITING; /*Change the state to waiting*/
	task_list.tasks[task_list.current_task].local_tick = ticks; /*Save the tick value*/
	dispatcher(kFromNormalExec); /*Call dispatcher to decide which task will be executed next*/
}

void rtos_suspend_task(void)
{
	task_list.tasks[task_list.current_task].state = S_SUSPENDED; /*Change the state to waiting*/
	dispatcher(kFromNormalExec); /*Call dispatcher to decide which task will be executed next*/
}

void rtos_activate_task(rtos_task_handle_t task)
{
	task_list.tasks[task].state = S_READY; /*Change the state to ready so it will execute*/
	dispatcher(kFromNormalExec); /*Call dispatcher to decide which task will be executed next*/
}

/**********************************************************************************/
// Local methods implementation
/**********************************************************************************/

static void reload_systick(void)
{
	SysTick->LOAD = USEC_TO_COUNT(RTOS_TIC_PERIOD_IN_US,
	        CLOCK_GetCoreSysClkFreq());
	SysTick->VAL = 0;
}

static void dispatcher(task_switch_type_e type)
{
	uint8_t task_counter; /*Create counter for task_list.tasks iteration*/
	int8_t max = -1; /*Initialize value to determine the greatest priority among the tasks*/

	for(task_counter = 0 ; task_counter < task_list.nTasks ; task_counter++)
	{
		/*If the task is currently available and has a greatest priority, it will be executed next */
		if(((task_list.tasks[task_counter].priority) > max) && (S_READY == (task_list.tasks[task_counter].state)|| S_RUNNING == (task_list.tasks[task_counter].state)))
		{
			max = task_list.tasks[task_counter].priority; /*Update the maximum priority found*/
			task_list.next_task = task_counter; /*Save the next task with greatest priority*/
		}
	}
	if(task_list.next_task != task_list.current_task) /*Change context only when needed*/
	{
		context_switch(type);
	}
}

FORCE_INLINE static void context_switch(task_switch_type_e type)
{
	register uint32_t sp asm("r0");	/*Retrieve sp value*/
	static uint8_t first = 1; /*Initialize flag 'first'*/
	if(!first)
	{
		asm("mov r0, r7");
		if(kFromISR == type)	/*Condition to distinguish from normal exevution and ISR*/
		{
			task_list.tasks[task_list.current_task].sp = (uint32_t*)sp + 9; /*Adjust the sp value*/
		}else
		{
			task_list.tasks[task_list.current_task].sp = (uint32_t*)sp - 9;	/*Adjust the sp value*/
		}
	}else
	{
		first = 0;
	}

	task_list.current_task = task_list.next_task; /*Update the task to be executed next*/
	task_list.tasks[task_list.next_task].state = S_RUNNING;
	SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk; /*Flag to start PendSV_Handler*/

}

static void activate_waiting_tasks()
{
	uint8_t task_counter; /*Create counter for task_list.tasks iteration*/

	for(task_counter = 0 ; task_counter < task_list.nTasks ; task_counter++) /*Activate tasks when needed*/
	{
		if(S_WAITING == task_list.tasks[task_counter].state)
		{
			task_list.tasks[task_counter].local_tick--;
			if(0 == task_list.tasks[task_counter].local_tick) /*Determine if a task's deadline has passed or not*/
			{
				task_list.tasks[task_counter].state = S_READY; /*Wake task when local tick equals 0*/
			}
		}
	}
}

/**********************************************************************************/
// IDLE TASK
/**********************************************************************************/

static void idle_task(void)
{
	for (;;)
	{

	}
}

/****************************************************/
// ISR implementation
/****************************************************/

void SysTick_Handler(void)
{
#ifdef RTOS_ENABLE_IS_ALIVE
	refresh_is_alive();
#endif
	task_list.global_tick++; /*Increase the counter of ticks since reset*/
	activate_waiting_tasks(); /*Activate tasks that are ready to be executed*/
	dispatcher(kFromISR);
	reload_systick();
}

void PendSV_Handler(void)
{
	register int32_t r0 asm("r0");
	SCB->ICSR |= SCB_ICSR_PENDSVCLR_Msk; /*Clean SCB_ICSR_PENDSVCLR_Msk flag*/
	r0 = (uint32_t)task_list.tasks[task_list.current_task].sp; /*Retrieve the sp*/
	asm("mov r7, r0");
}

/**********************************************************************************/
// IS ALIVE SIGNAL IMPLEMENTATION
/**********************************************************************************/

#ifdef RTOS_ENABLE_IS_ALIVE
static void init_is_alive(void)
{
	gpio_pin_config_t gpio_config =
	{ kGPIO_DigitalOutput, 1, };

	port_pin_config_t port_config =
	{ kPORT_PullDisable, kPORT_FastSlewRate, kPORT_PassiveFilterDisable,
	        kPORT_OpenDrainDisable, kPORT_LowDriveStrength, kPORT_MuxAsGpio,
	        kPORT_UnlockRegister, };
	CLOCK_EnableClock(alive_CLOCK(RTOS_IS_ALIVE_PORT));
	PORT_SetPinConfig(alive_PORT(RTOS_IS_ALIVE_PORT), RTOS_IS_ALIVE_PIN,
	        &port_config);
	GPIO_PinInit(alive_GPIO(RTOS_IS_ALIVE_PORT), RTOS_IS_ALIVE_PIN,
	        &gpio_config);
}

static void refresh_is_alive(void)
{
	static uint8_t state = 0;
	static uint32_t count = 0;
	SysTick->LOAD = USEC_TO_COUNT(RTOS_TIC_PERIOD_IN_US,
	        CLOCK_GetCoreSysClkFreq());
	SysTick->VAL = 0;
	if (RTOS_IS_ALIVE_PERIOD_IN_US / RTOS_TIC_PERIOD_IN_US - 1 == count)
	{
		GPIO_PinWrite(alive_GPIO(RTOS_IS_ALIVE_PORT), RTOS_IS_ALIVE_PIN,
		        state);
		state = state == 0 ? 1 : 0;
		count = 0;
	} else //
	{
		count++;
	}
}
#endif
///
