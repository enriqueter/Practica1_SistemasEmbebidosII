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
	SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk
	        | SysTick_CTRL_ENABLE_Msk;
	reload_systick();
	for (;;)
		;
}

rtos_task_handle_t rtos_create_task(void (*task_body)(), uint8_t priority,
		rtos_autostart_e autostart)
{
	/*Output variable*/
	rtos_task_handle_t task_handle;

	/*Check if there's space in the Task List*/
	if(task_list.nTasks > RTOS_MAX_NUMBER_OF_TASKS -1)
	{
		/*List is full invalid task return value -1*/
		task_handle = -1;

	}
	else
	{
		if(autostart == kAutoStart)
		{
			/*Set task state to ready */
			task_list.tasks[task_list.nTasks].state = S_READY;
		}
		else
		{
			/*Set task state to suspend */
			task_list.tasks[task_list.nTasks].state = S_SUSPENDED;
		}

		/*****************************/
		/* TASK STACK INITIALIZATION */
		/*****************************/
		/*Set pointer to the task */
		task_list.tasks[task_list.nTasks].task_body = task_body;
		/*Create space in stack for context change */
		task_list.tasks[task_list.nTasks].sp = &(task_list.tasks[task_list.nTasks].stack[RTOS_STACK_SIZE - 1]) - STACK_FRAME_SIZE;
		/*Set stack frame with  the return address bye StackSize - offset */
		task_list.tasks[task_list.nTasks].stack[RTOS_STACK_SIZE - STACK_LR_OFFSET] = (uint32_t)task_body;
		/* Initialize the stack frame in default PSR value */
		task_list.tasks[task_list.nTasks].stack[RTOS_STACK_SIZE - STACK_PSR_OFFSET] = STACK_PSR_DEFAULT;
		/*Increase task list index value*/
		task_list.nTasks++;
		/*Initialize local clock*/
		task_list.tasks[task_list.nTasks].local_tick = 0;
		/*****************************/
		/*****************************/


		/*Set input priority to the task */
		task_list.tasks[task_list.nTasks].priority = priority;
		/*Set task handle to return variable*/
		task_handle = task_list.nTasks - 1;


	}

	/* Return the value of task_handle */
	return task_handle;



}

rtos_tick_t rtos_get_clock(void)
{
	/* Return the value of the system clock */
	return task_list.global_tick;
}

void rtos_delay(rtos_tick_t ticks)
{


}

void rtos_suspend_task(void)
{

}

void rtos_activate_task(rtos_task_handle_t task)
{

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
	/* Current task in the dispatcher */
	uint8_t task_i;
	/* Next idle task */
	uint8_t next_task_handle =  task_list.nTasks;
	/* Highest priority possible */
	int8_t highest = -1;


	for(task_i = 0; task_i < task_list.nTasks; task_i++)
	{
		if(task_list.tasks[task_i].priority > highest && ((S_READY == task_list.tasks[task_i].state) || (S_RUNNING == task_list.tasks[task_i].state)))
		{
			/* Set task priority as Highest priority */
			highest = task_list.tasks[task_i].priority;
			/*  */
			next_task_handle = task_i;
		}
	}

	task_list.next_task = next_task_handle;
	if(next_task_handle != task_list.current_task)
	{
		/* Change task to next task */
		context_switch(type);
	}

}

FORCE_INLINE static void context_switch(task_switch_type_e type)
{

	 /* Get current stack  pointer */
	register uint32_t sp asm("r0");

	/* Value for first task */
	static uint8_t first = 1;
	/* For the current task if not the first task */
	if(!first)
	{
		asm("mov r0, r7");
		/* Save the stack pointer in the current task */
		if(kFromISR == type)
		{
			task_list.tasks[task_list.current_task].sp = (uint32_t*)sp + 9;
		}
		else
		{
			task_list.tasks[task_list.current_task].sp = (uint32_t*)sp -9;
		}
	}
	else
	{
		first = 0;
	}

	/* Moves to next */
	task_list.current_task = task_list.next_task;
	/* Puts next task in running state */
	task_list.tasks[task_list.next_task].state = S_RUNNING;
	/* Call the context Switch */
	SCB->ICSR  |= SCB_ICSR_PENDSVSET_Msk;
}

static void activate_waiting_tasks()
{
	/* Counter for current  task */
	uint8_t task_i;
	/* Each task in the list */
	for(task_i = 0; task_i < task_list.nTasks; task_i++)
	{
		 if(S_WAITING == task_list.tasks[task_i].state)
		 {
			 /* Decrease local clock -1 */
			 task_list.tasks[task_i].local_tick--;
			 /* If local tick is 0 */
			 if(0 == task_list.tasks[task_i].local_tick)
			 {

				 /*Current task is set to ready*/

				 task_list.tasks[task_i].state = S_READY;
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
	activate_waiting_tasks();
	reload_systick();
}

void PendSV_Handler(void)
{

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
