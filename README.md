# Need for Speed Most Wanted 2005 Unlimited

Parche para la edición de PC de 2005. El instalador admite Windows 7 SP1 y versiones posteriores en sistemas capaces de ejecutar aplicaciones x86. Requiere una instalación compatible del juego original con el ejecutable 1.3.

## Instalar

Descarga **NFSMW-Unlimited-Setup.exe** desde [Releases](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases). Elige el idioma del instalador y selecciona la carpeta que contiene `speed.exe`. El instalador valida el ejecutable, instala el parche y las bibliotecas de mandos y conserva los archivos de configuración existentes. Las copias de las DLL reemplazadas quedan en `UnlimitedCommunity/BeforeUnlimited` dentro de la carpeta del juego.

El instalador ofrece español, inglés, francés, alemán, italiano, neerlandés, portugués, polaco, ruso, japonés, sueco y danés.

## Controles y pantalla

- El minimapa y el velocímetro se desplazan junto con sus indicadores hacia las esquinas inferiores. El margen se calcula a partir del lado corto de la imagen y los instrumentos mantienen sus proporciones.
- **Opciones → Gráficos → Modo de pantalla** ofrece pantalla completa, ventana y sin bordes.
- **Opciones → Gráficos → Avanzado → API de salida** ofrece DirectX 9, DirectX 11, DirectX 12 y Vulkan. La API seleccionada se aplica al reiniciar. El juego arranca con DirectX 9 tras una instalación limpia. Las otras API son experimentales y el motor 3D todavía usa Direct3D 9 para buena parte de la escena. [Detalles técnicos](renderer-lab/Presentacion.md).
- El instalador incluye las dependencias de mandos y el complemento XenonEffects. Los archivos comerciales del juego y los paquetes opcionales de cinemáticas e interfaz HD se obtienen por separado. [Créditos de mandos](third-party/controllers/README.md) · [Referencias de Xbox 360](Mods%20de%20Xbox%20360.md).

## Compilar

Usa LLVM-MinGW x86, los SDK de Windows y Vulkan, y el compilador Inno Setup 6. Ejecuta `installer/build.ps1` desde PowerShell. El instalador se genera en `dist/NFSMW-Unlimited-Setup.exe`. Puedes indicar la ruta de Inno mediante `-Compiler` y la de las bibliotecas redistribuibles x86 del SDK mediante `-LegacyUcrt`.

Las bibliotecas incluidas conservan [sus licencias](third-party/controllers/Licenses).
