  Puerta con sistema de control de acceso: quién entra, a qué fecha y hora, identificado o no. Tiene 2 modos, "Seguro" y "Permitir a todos", donde "Seguro" deja pasar a aquellos registrados previamente, y "Permitir a todos" deja que cualquiera que coloque su huella pueda pasar.
  Los registros de acciones e interacciones son enviados via bluetooth a una terminal serial en formato de logs. El proyecto requiere WiFi para escribir la fecha y hora actual. 

   Utilizamos: ESP32 DevKitC, Sensor de huella dactilar R307, 2 servos SG80 y 2 pushbuttons.
  Está diseñado para en primer lugar registrar una huella MASTER o administradora (o user ID#1, en logs), quién debe primero permitir la ejecucíon de las siguientes acciones enlistadas cuando sean invocadas:
* 1er botón: pulso corto - Registrar huella
* 1er botón: pulso largo - Cambiar de modo, entre modo Seguro y modo Permitir a todos.
* 2do botón: limpiar todas las huellas - Se eliminan todas las huellas, y se vuelve a registrar la huella MASTER.
  
Luego, como Modo de recuperación al tener una falla con la huella MASTER, se deben presionar ambos botones al mismo tiempo y sin ningún requirimiento de permiso se eliminará solo la huella administradora, para registrarla de nuevo inmediatamente. Cualquiera de estas acciones tiene un tiempo de expiración, lo que vuelve el sistema al estado próximamente anterior sin modificar nada.

(Este código fue generado con IA, supervisado y testeado meticulosamente para lograr este sistema que creemos carente de fallas lógicas.)
