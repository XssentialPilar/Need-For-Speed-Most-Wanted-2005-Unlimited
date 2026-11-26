# Mandos de Unlimited

El juego lee mandos por DirectInput 8. Las DLL de esta carpeta usan SDL3 para leer los mandos y presentar al juego un dispositivo DirectInput con dirección analógica, cruceta y gatillos independientes. El módulo `nfs_controller.hpp` integra las asignaciones predeterminadas en el juego 1.3. El teclado y las asignaciones guardadas mantienen su comportamiento.

- `dinput8.dll` y `Xidi.32.dll` provienen de la edición Win32 de Xidi-SDL 1.0.1. [Versión publicada y código fuente](https://github.com/ProjectXsent/Xidi-SDL/releases/tag/Xidi-SDL3-v1.0.1)
- `SDL3.dll` corresponde a SDL 3.4.20. [Código fuente](https://github.com/libsdl-org/SDL/tree/release-3.4.20)
- `Xidi.ini` mantiene un eje para cada gatillo y sitúa el reposo en el centro que espera Most Wanted.

Windows debe reconocer el mando. Un DualShock 3 original necesita un controlador de dispositivo compatible, como DsHidMini en Windows 10 u 11. Unlimited usa la entrada que el dispositivo expone a Windows. [Documentación de DsHidMini](https://docs.nefarius.at/projects/DsHidMini/v3/)

La prueba local de consola recorrió dispositivos SDL virtuales de tipo DualShock 3, DualShock 4, DualSense y Xbox y comprobó dirección, gatillos, cruceta, botones y desconexión en DirectInput. La comprobación con mandos físicos queda pendiente.
