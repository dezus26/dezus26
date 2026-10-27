# dezuClip

Plugin clipper (VST3 + Standalone) hecho con JUCE.

## Funciones
- Modos: Soft Clip, Soft Clip Pro, Hard Clip, Ratio 2:1
- Input, Clip Level, Output y Ceiling
- Sobremuestreo: Off, 2x, 4x, 8x, 16x, 32x
- Filtro de fase mínima (sin latencia extra) o fase lineal
- Medidores IN / Peak / RMS y osciloscopio pre-clip vs salida
- Latencia reportada automáticamente al DAW

## Compilar en Windows 11 (local)
Requisitos: Visual Studio 2022 (carga de trabajo "Desarrollo para el escritorio con C++"), CMake y Git.

```
cmake -B build
cmake --build build --config Release
```

El VST3 queda en `build/dezuClip_artefacts/Release/VST3/dezuClip.vst3`.
Copia esa carpeta a `C:\Program Files\Common Files\VST3\` y reescanea plugins en tu DAW.

## Compilar sin instalar nada (GitHub Actions)
1. Sube esta carpeta a un repositorio de GitHub.
2. Ve a la pestaña **Actions** y ejecuta "Build dezuClip (Windows VST3)".
3. Descarga el artefacto `dezuClip-VST3-windows`.

## Notas
- Pon dezuClip al final de la cadena. El nivel de clip efectivo debe quedar en 0 dBFS o menos.
- El Ceiling se aplica después del sobremuestreo.
