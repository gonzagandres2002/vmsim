# Reporte de análisis — Simulador de memoria virtual

Laboratorio de memoria virtual · Sistemas Operativos · Universidad de Antioquia

Este reporte describe las estructuras de datos del simulador, explica las políticas de reemplazo
implementadas (FIFO y LRU), presenta los resultados de los programas de prueba y compara ambas
políticas. Las instrucciones de compilación y uso están en [README.md](README.md).

## 1. Estructuras de datos

### 1.1 La dirección virtual (`AddressLayout`)

Una dirección virtual tiene 32 bits. Con páginas de 4 KB el offset necesita 12 bits
(2^12 = 4096) y los 20 restantes, el número de página virtual (VPN), se reparten en dos índices
de 10 bits:

```
 31           22 21           12 11            0
┌───────────────┬───────────────┬───────────────┐
│  PT1 (10 b)   │  PT2 (10 b)   │ offset (12 b) │
└───────────────┴───────────────┴───────────────┘
        │               │               │
        │               │               └─ desplazamiento dentro de la página (0..4095)
        │               └─ índice en la tabla de nivel 2 (1024 entradas)
        └─ índice en el directorio, nivel 1 (1024 entradas)
```

`AddressLayout` calcula estos anchos a partir del tamaño de página (`offset_bits = log2(página)`,
el resto se divide en dos) y ofrece las operaciones de bits: `vpn(va) = va >> 12`,
`offset(va) = va & 0xFFF`, `level1_index(vpn) = vpn >> 10`, `level2_index(vpn) = vpn & 0x3FF`,
`physical_address(marco, offset) = (marco << 12) | offset`. Con páginas de 8 KB el offset pasa a
13 bits y los índices a 9 y 10.

### 1.2 La tabla de páginas de dos niveles (`PageTable`, `PageTableEntry`)

```
 directorio (nivel 1)           tablas de nivel 2 (se crean bajo demanda)
 1024 unique_ptr                1024 PTE cada una
┌──────┐
│  0   │──────────────────────► ┌───────┬───────┬──────────┬───────┐
├──────┤                        │ frame │ valid │ accessed │ dirty │  PTE 0
│  1   │── nullptr              ├───────┼───────┼──────────┼───────┤
├──────┤                        │ frame │ valid │ accessed │ dirty │  PTE 1
│ ...  │                        │  ...  │       │          │       │
├──────┤                        └───────┴───────┴──────────┴───────┘
│ 1023 │── nullptr
└──────┘
```

- **PTE** (`PageTableEntry`): número de marco físico y tres bits. `valid` dice si la página está
  en memoria; `accessed` se enciende en cada lectura o escritura; `dirty` se enciende en cada
  escritura. Los dos últimos se apagan cuando la página se carga.
- El **directorio** tiene una entrada por cada tabla de nivel 2 posible, pero solo apunta a las
  que se han necesitado. Un programa que usa 8 KB crea una sola tabla de nivel 2 (8 KB de PTE)
  en lugar de las 1024 que ocuparía una tabla lineal completa (8 MB). Esa es la razón de ser de
  los dos niveles (OSTEP cap. 20).
- `entry(vpn)` devuelve la PTE creando la tabla de nivel 2 si falta; `find(vpn)` la consulta sin
  crear nada. Las tablas se guardan en `std::unique_ptr`, así que se liberan solas.

### 1.3 La memoria física (`PhysicalMemory`)

Un `std::vector<uint8_t>` con todos los bytes de la memoria (256 KB por defecto) dividido en
marcos del tamaño de página, más un `std::deque<uint32_t>` con los marcos libres. Los marcos se
entregan en orden (0, 1, 2, ...) para que las trazas sean reproducibles. Ofrece `read`/`write`
de un byte por dirección física y `copy_page`/`load_page`/`zero_page` para mover páginas
completas desde y hacia el disco.

### 1.4 El espacio de direcciones (`VirtualMemory`)

Reúne las piezas anteriores y añade:

| Estructura | Tipo | Para qué |
|---|---|---|
| `regions` | `std::map<inicio, tamaño>` | las regiones vivas creadas por `alloc`; un acceso fuera de todas es un segmentation fault |
| `frame_owner` | `std::vector<VPN>` (uno por marco) | saber qué página vive en el marco que la política eligió expulsar |
| `swap` | `std::map<VPN, bytes de la página>` | el disco simulado: las páginas sucias expulsadas se guardan aquí y vuelven con su contenido |
| `counters` | `Stats` | accesos, fallos, reemplazos y escrituras a disco |

### 1.5 Las políticas (`Fifo`, `Lru`)

Ambas guardan una `std::list<uint32_t>` de **marcos**. En FIFO la lista es una cola: los marcos
entran al final cuando se cargan y la víctima es el frente. En LRU cada acceso mueve el marco al
final, de modo que el frente es siempre el menos recientemente usado. `on_release` saca de la
lista un marco que `free` dejó libre.

## 2. Flujo de un acceso y política de reemplazo

```
read/write(va)
   │
   ├─ 1. ¿va está dentro de alguna región de alloc?  no → segmentation fault (error)
   ├─ 2. accesos++
   ├─ 3. VPN = va >> 12;  PTE = tabla.entry(VPN)      (crea la tabla de nivel 2 si no existe)
   ├─ 4. ¿PTE.valid?
   │        sí → HIT: política.on_access(marco)
   │        no → FALLO DE PÁGINA: fallos++
   │              ├─ ¿hay marco libre?  sí → tomarlo
   │              │                     no → REEMPLAZO: reemplazos++
   │              │                            marco = política.choose_victim()
   │              │                            víctima = frame_owner[marco]
   │              │                            si víctima.dirty → swap[víctima] = copia; escrituras++
   │              │                            víctima.valid = false
   │              ├─ cargar la página: desde swap si tiene copia, si no en ceros
   │              ├─ PTE = {marco, valid=1, accessed=0, dirty=0};  frame_owner[marco] = VPN
   │              └─ política.on_load(marco)
   ├─ 5. PTE.accessed = 1;  si es write: PTE.dirty = 1
   └─ 6. PA = (marco << 12) | offset;  leer o escribir el byte
```

### 2.1 FIFO

Expulsa la página que lleva **más tiempo en memoria**, sin importar cuánto se use. Es una cola:
la página entra al final al cargarse y sale por el frente al expulsarse. Cuesta O(1) por
operación y no necesita hardware especial. Su debilidad es que ignora la información de uso: una
página que se accede en cada instrucción se expulsa igual cuando le llega el turno. Además sufre
la **anomalía de Belady**: con más memoria puede fallar más (sección 3.3).

### 2.2 LRU

Expulsa la página que lleva **más tiempo sin usarse**. Apuesta a que el pasado predice el futuro
(localidad temporal): si una página no se ha usado en mucho tiempo, probablemente no se use pronto.
Con cargas con localidad se acerca a la política óptima. El costo es que hay que registrar cada
acceso: en el simulador se mueve el marco al final de una lista (O(marcos) por acceso). Un
sistema real no puede pagar eso en cada acceso a memoria y usa aproximaciones como Clock, que
solo consulta el bit `accessed` que el hardware enciende. LRU tiene la **propiedad de pila**: el
conjunto de páginas en memoria con N marcos está contenido en el de N+1, así que más memoria
nunca produce más fallos.

## 3. Resultados de los programas de prueba

Todos los resultados se obtienen con los comandos indicados; `--trace` muestra el detalle.

### 3.1 Ejemplo del enunciado (`data/example.txt`)

```
alloc 8192      write 0 42      write 4096 99      read 0      read 4096
```

`./vmsim data/example.txt --trace`:

```
[2] alloc 8192 -> region [0x00000000 .. 0x00001fff], 2 page(s)
[3] write 0x00000000 <- 42 | VPN 0 (PT1 0, PT2 0, offset 0) | PAGE FAULT: level-2 table created, free frame 0, new page filled with zeros | PA 0x00000000
[4] write 0x00001000 <- 99 | VPN 1 (PT1 0, PT2 1, offset 0) | PAGE FAULT: free frame 1, new page filled with zeros | PA 0x00001000
[5] read 0x00000000 -> 42 | VPN 0 (PT1 0, PT2 0, offset 0) | HIT: frame 0 | PA 0x00000000
[6] read 0x00001000 -> 99 | VPN 1 (PT1 0, PT2 1, offset 0) | HIT: frame 1 | PA 0x00001000

Total de accesos: 4
Total fallos de página: 2
Hit rate: 50.00%
Total reemplazos: 0
Política: FIFO
```

Dos fallos obligatorios (la primera vez que se toca cada página), dos aciertos, ningún reemplazo
porque sobran marcos. El resultado es el mismo con LRU: la política solo interviene cuando la
memoria está llena.

### 3.2 La cadena de referencia de clase (`data/ostep.txt`)

Páginas `0 1 2 0 1 3 0 3 1 2 1` con 3 marcos (OSTEP cap. 22, figuras 22.2 y 22.5):

| Política | Comando | Accesos | Fallos | Hit rate | Reemplazos | Tiempo estimado |
|---|---|---|---|---|---|---|
| FIFO | `./vmsim data/ostep.txt fifo 12` | 11 | 7 | 36.36 % | 4 | 70.0 ms |
| LRU  | `./vmsim data/ostep.txt lru 12`  | 11 | 5 | 54.55 % | 2 | 50.0 ms |

Coincide con el libro y las diapositivas (FIFO 4 hits = 36.4 %, LRU 6 hits = 54.5 %). El momento
decisivo es el acceso a la página 3: FIFO expulsa la 0 (la más antigua) aunque acaba de usarse y
vuelve a usarse enseguida; LRU expulsa la 2, que es la que lleva más tiempo sin tocarse:

```
FIFO [11] read VPN 3 | PAGE FAULT: evicted VPN 0 from frame 0     → el siguiente read 0 falla
LRU  [11] read VPN 3 | PAGE FAULT: evicted VPN 2 from frame 2     → los siguientes read 0 y read 3 aciertan
```

### 3.3 Anomalía de Belady (`data/belady.txt`)

Cadena `1 2 3 4 1 2 5 1 2 3 4 5` con 3 y 4 marcos:

| Política | 3 marcos (`12` KB) | 4 marcos (`16` KB) |
|---|---|---|
| FIFO | 9 fallos (25.00 %) | **10 fallos (16.67 %)** |
| LRU  | 10 fallos (16.67 %) | 8 fallos (33.33 %) |

Con FIFO, agregar un marco **empeora** el resultado. Con LRU no puede pasar (propiedad de pila).

### 3.4 Carga con localidad (`data/locality.txt`)

500 accesos sobre 140 páginas: 40 páginas "calientes" (VPN 0-39) se leen en cada una de 10
rondas y en cada ronda se leen además 10 páginas "frías" nuevas (VPN 40-139). Es la versión
determinista del workload 80-20 de clase: un conjunto pequeño concentra los accesos.

| Memoria | Marcos | FIFO fallos (hit rate) | FIFO reempl. | LRU fallos (hit rate) | LRU reempl. |
|---|---|---|---|---|---|
| 192 KB | 48 | 500 (0 %) | 452 | 500 (0 %) | 452 |
| **256 KB** | **64** | **260 (48 %)** | **196** | **140 (72 %)** | **76** |
| 320 KB | 80 | 180 (64 %) | 100 | 140 (72 %) | 60 |
| 576 KB | 144 | 140 (72 %) | 0 | 140 (72 %) | 0 |

- 140 fallos es el mínimo posible: cada página se toca al menos una vez (fallos obligatorios).
  **LRU lo alcanza con 64 marcos**: las 40 páginas calientes se usan en cada ronda, nunca son las
  menos recientes y nunca salen; solo se expulsan páginas frías viejas.
- **FIFO con 64 marcos falla 260 veces**, casi el doble. A partir de la tercera ronda la cola
  llega a las páginas calientes y las expulsa por antigüedad; en la ronda siguiente vuelven a
  fallar, expulsan a otras calientes, y el ciclo se repite (196 reemplazos contra 76).
- Con 48 marcos las dos fallan siempre: en cada ronda se recorren 50 páginas distintas
  (40 calientes + 10 frías) y no caben. El conjunto de trabajo es mayor que la memoria y la
  política no puede arreglarlo.
- Con 144 marcos cabe todo y las políticas dan igual, como en las diapositivas: "cuando la caché
  es lo suficientemente grande, no tiene sentido cuál política usar".

En tiempo estimado (T_M = 100 ns, T_D = 10 ms): FIFO 2600 ms frente a LRU 1400 ms con 256 KB.
El costo del disco domina tanto que 120 fallos de diferencia son 1.2 segundos.

### 3.5 Ciclo secuencial (`data/loop.txt`)

80 páginas leídas en orden, tres veces (240 accesos): el *looping sequential workload* de clase.

| Memoria | Marcos | FIFO | LRU |
|---|---|---|---|
| 256 KB | 64 | 240 fallos (0 %), 176 reemplazos | 240 fallos (0 %), 176 reemplazos |
| 320 KB | 80 | 80 fallos (66.67 %), 0 reemplazos | 80 fallos (66.67 %), 0 reemplazos |

Con 64 marcos las dos políticas fallan en **todos** los accesos: cuando se vuelve a la página 0,
es justamente la más antigua (FIFO) y la menos recientemente usada (LRU), así que se expulsó un
instante antes de necesitarse. Es el peor caso de ambas; aquí una política aleatoria lo haría
mejor. Basta un marco más por página (80) para que solo fallen los 80 primeros accesos.

### 3.6 Bit dirty y swap (`data/dirty.txt`)

`./vmsim data/dirty.txt fifo 8 --trace` (2 marcos):

```
[3] write 0x00000000 <- 7 | ... | PAGE FAULT: level-2 table created, free frame 0, new page filled with zeros
[4] write 0x00001000 <- 8 | ... | PAGE FAULT: free frame 1, new page filled with zeros
[5] read 0x00002000 -> 0  | ... | PAGE FAULT: evicted VPN 0 from frame 0 (dirty: written to disk), new page filled with zeros
[6] read 0x00000000 -> 7  | ... | PAGE FAULT: evicted VPN 1 from frame 1 (dirty: written to disk), page loaded from disk
[7] read 0x00001000 -> 8  | ... | PAGE FAULT: evicted VPN 2 from frame 0 (clean: discarded), page loaded from disk
[8] read 0x00000000 -> 7  | ... | HIT: frame 1
[9] free 0x00000000 -> 3 page(s), 2 frame(s) released
[11] write 0x00003000 <- 9 | ... | PAGE FAULT: free frame 1, new page filled with zeros
```

La página 0 se escribió (7), se expulsó estando sucia y volvió del disco con su 7. La página 2
solo se leyó, así que al expulsarla no se escribió nada (`clean: discarded`): el bit `dirty`
ahorra una operación de disco. Después de `free` los marcos vuelven a la lista de libres y el
siguiente fallo no necesita reemplazo. Resultado: 7 accesos, 6 fallos, 3 reemplazos, 2 escrituras
a disco.

## 4. Análisis

**Hit rate.** Depende de dos cosas: cuánta memoria hay respecto al conjunto de trabajo del
programa y qué tan bien la política predice el futuro. Cuando la memoria es más pequeña que el
conjunto de trabajo (`locality` con 48 marcos, `loop` con 64) ninguna política salva la situación:
0 % con las dos. Cuando la memoria sobra (`locality` con 144 marcos, `loop` con 80) tampoco importa
la política: solo quedan los fallos obligatorios. La política decide en la zona intermedia, y ahí
LRU gana siempre que haya localidad: 72 % frente a 48 % en `locality` con 64 marcos, 54.5 % frente
a 36.4 % en la cadena de clase.

**Número de reemplazos.** Es el indicador más directo del trabajo de la política: en `locality`
con 64 marcos LRU hizo 76 reemplazos y FIFO 196, porque FIFO expulsó repetidamente páginas
calientes que tuvo que volver a traer. Los reemplazos también cuestan escrituras a disco cuando la
víctima está sucia; en cargas con escrituras la diferencia en tiempo es aún mayor que en fallos.

**Tiempo.** Con el modelo AMAT de clase, T_D es 100 000 veces T_M, así que el tiempo total es
prácticamente `fallos × 10 ms`. Mejorar el hit rate del 48 % al 72 % ahorra 1.2 s en un programa
de 500 accesos. Por eso las políticas de reemplazo importan tanto y por eso ninguna técnica
razonable (TLB, tablas multinivel) se mide en el mismo orden de magnitud que un fallo de página.

## 5. Comparación teórica FIFO vs. LRU

| Criterio | FIFO | LRU |
|---|---|---|
| Qué expulsa | la página cargada hace más tiempo | la página usada hace más tiempo |
| Información que usa | orden de llegada | historial de accesos |
| Costo por acceso | ninguno (solo actúa al cargar) | actualizar la posición del marco |
| Costo por reemplazo | O(1): sacar el frente | O(1): sacar el frente |
| Hardware necesario | ninguno | seguir cada acceso (en real: bit `accessed` + aproximación Clock) |
| Anomalía de Belady | sí (9 → 10 fallos en `belady.txt`) | no (propiedad de pila) |
| Con localidad | expulsa páginas calientes por antigüedad | conserva el conjunto de trabajo |
| Ciclo secuencial mayor que la memoria | 0 % de hits | 0 % de hits |
| Respecto a OPT | lejos | cerca cuando hay localidad |

FIFO es la política "simple y determinista" del enunciado: trivial de implementar y de razonar,
pero ciega al uso. LRU es la "compleja, con mejor hit rate": necesita registrar cada acceso, lo
cual en un simulador es una lista y en un sistema real obliga a aproximar (Clock recorre los
marcos como un reloj y expulsa el primero cuyo bit `accessed` esté apagado, apagando los que
encuentra encendidos). Ambas comparten el peor caso del ciclo secuencial. Ninguna alcanza a la
política óptima (OPT: expulsar la página que se usará más tarde), que en la cadena de clase logra
6 hits igual que LRU pero en general es inalcanzable porque requiere conocer el futuro.

En este simulador la política se elige en tiempo de ejecución (`fifo` o `lru`) y ambas
implementan la misma interfaz de cuatro avisos y una decisión, así que agregar Clock, Random u
OPT (para un programa conocido de antemano) es escribir una clase más sin tocar la traducción ni
el manejo de fallos.

## 6. Conclusiones

- La tabla de dos niveles funciona como un directorio perezoso: el programa del enunciado crea
  una sola tabla de nivel 2 y el resto del directorio queda en `nullptr`.
- La traducción se resuelve con operaciones de bits sobre la dirección; el fallo de página es el
  camino lento que hace todo lo demás (marco, disco, PTE) y la política es una decisión aislada
  dentro de ese camino.
- FIFO y LRU se comportan igual mientras sobre memoria y mientras falte demasiada; en la zona
  intermedia, con localidad, LRU reduce fallos y reemplazos de forma notable (48 % → 72 % de hit
  rate en la carga con localidad) y nunca sufre la anomalía de Belady.
