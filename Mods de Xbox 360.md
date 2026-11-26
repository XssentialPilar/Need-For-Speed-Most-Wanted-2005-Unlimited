# Integración de Xbox 360 en Unlimited

## Integrado

- **Luces de vehículos en los reflejos.** Unlimited reactiva los destellos de los vehículos en el mapa cúbico con el renderizador original de PC. La decisión de dibujar luces sigue dependiendo de los ajustes gráficos del juego. La referencia publicada para este efecto es [NFS HD Reflections de Aero_](https://nfsmods.xyz/mod/3363).
- **Retrovisor de alta resolución.** El objetivo de color y profundidad conserva las proporciones del juego y se adapta al tamaño de la interfaz. Unlimited utiliza 256, 512 o 1024 píxeles por lado según la resolución y puede reducir el tamaño si falla la asignación. [Referencia de NFS HD Reflections](https://github.com/AeroWidescreen/NFSHDReflections).
- **Listas de dibujo ampliadas.** Unlimited eleva de 400 a 1000 KiB cada una de las dos listas que el motor reserva al arrancar. El cambio aporta margen para la geometría de los reflejos. Las tablas de coches, personalización y partidas requieren una integración propia. [Código de NFS HD Reflections](https://github.com/AeroWidescreen/NFSHDReflections).
- **XenonEffects 1.02.** Recupera chispas y estelas de aire de Xbox 360. Se controla desde **Opciones → Vídeo → Efectos de Xbox 360** y se aplica al reiniciar. El ajuste comienza desactivado. El cargador comprueba los puntos de parcheo antes de activar el módulo. Las pruebas abarcaron el ejecutable, las instrucciones y el paquete de texturas. Queda pendiente probar los efectos en carrera. [Proyecto y licencia MIT](https://github.com/xan1242/NFSMW_XenonEffects) · [Versión 1.02](https://github.com/xan1242/NFSMW_XenonEffects/releases/tag/1.02).

## Componentes de referencia

| Proyecto | Aporte | Punto de integración |
| --- | --- | --- |
| [Xbox 360 Stuff Pack 4.2](https://nfsmods.xyz/mod/1200) | Materiales de vehículos, exposición, luz por hora, cielo, agua, vegetación y texturas | Verificar cada recurso y su procedencia. El paquete incluye escalado de vistas y XenonEffects, que se solapan con funciones de Unlimited. |
| [Xbox 360 Visual Treatment 1.5 de Kevin4e](https://www.nfsaddons.com/downloads/nfsmw/tools/7927/updated-xbox-360-visual-treatment-v14.html) | Ajuste de color y luz de la versión de consola | Su distribución depende de Extra Options, VltEd y Stuff Pack 2.4 Beta. El autor exige permiso para incluirlo en un modpack. |
| [NFS HD Reflections de Aero_](https://github.com/AeroWidescreen/NFSHDReflections) | Detalle de reflejos, luces del retrovisor, composición y resolución de renderizado | Revisar cada efecto por separado. El código de reflejos se solapa con la actualización de los mapas cúbicos y los reinicios de Direct3D de Unlimited. |
| [NFSMW Unlimiter de nlgxzef](https://nfsmods.xyz/mod/688) | Amplía tablas de coches, pools de memoria y opciones de personalización | Auditar los cambios de base de datos y los menús FNG con perfiles de prueba antes de combinar ambos parches. |

El componente XenonEffects procede del archivo `NFSMW_XenonEffects.zip` publicado por el autor. Su SHA-256 es `1a6d85cfdc274259b0ee1df0e2e51f7e273382e04bfdbe472b4688de53226082`. La licencia está en `Juego/UnlimitedCommunity/NFSMW_XenonEffects-LICENSE.txt`.

## Cinemáticas Full HD

Unlimited busca reemplazos EA VP6 de 1920 × 1080 en `Juego/MOVIES_HD`. Cada video debe conservar el nombre con el que el juego solicita la versión de ese idioma y usar su cadencia compatible. El cargador valida el flujo antes de abrirlo. Si el reemplazo falta o falla la validación, se reproduce la cinemática de la instalación. Las cinemáticas Full HD se suministran por separado.
