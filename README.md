# Practica1_SistemasEmbebidosII
Practica 1 Sistemas Embebidos II  Equipo: Isaac Vazquez, Enrique Terán 


Funciones implementadas:

rtos_start_scheduler:
se pone el global tick en 0 y la tarea actual en -1, se crea la tarea idle y se carga el systick
rtos_create_task:

rtos_get_clock:
 regresa el valor del global tick
 
rtos_delay:
pone la tarea actual en espera y pasa el valor del delay a los local ticks y hace la llamada al dispatcher
rtos_suspend_task:
Pone la tarea actual en estado suspend y hace la llamada al dispatcher
rtos_activate_task:
Pone la tarea actual en ready y hace la llamada al dispatcher
dipatcher:

context_switch:

activate_waiting_task

systick:

PendSV:
