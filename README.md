# Practica1_SistemasEmbebidosII
Practica 1 Sistemas Embebidos II  Equipo: Isaac Vazquez, Enrique Terán 


Funciones implementadas:

rtos_start_scheduler:
se pone el global tick en 0 y la tarea actual en -1, se crea la tarea idle y se carga el systick

rtos_create_task:
Se crean las tareas solamente si hay disponibilidad, dentro se define si se pone la tarea en ready o en suspend.
Se inicializa el stack pointer, inicializa el frame inicial con la direccion de retorno y el PSR en el valor por defecto.

rtos_get_clock:
 regresa el valor del global tick
 
rtos_delay:
pone la tarea actual en espera y pasa el valor del delay a los local ticks y hace la llamada al dispatcher

rtos_suspend_task:
Pone la tarea actual en estado suspend y hace la llamada al dispatcher

rtos_activate_task:
Pone la tarea actual en ready y hace la llamada al dispatcher

dipatcher:
Se crea una variable para iterar la lista de tareas y otra variable para llevar la prioridad maxima de la tarea actual, se checa si la tarea esta en estado ready o esta corriendo  y tiene mayor prioridad que la actual prioridad establecida, pone la prioridad de la tarea como la mas alta y guarda la tarea como la proxima. Si la proxima tarea es diferente  a la tarea actual hace el llamado al cambio de contexto.



context_switch:

activate_waiting_task

systick:
Se incrementa el valor del reloj, se activa las tareas que estan listas y se corre el dispatcher.
PendSV:
Carga el stack pointer del procesador con el stack pointer de la tarea acual.

Notas:
