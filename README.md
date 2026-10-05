# NVSStorageAB

Libreria Arduino per **ESP32** per salvare strutture C/C++ nella NVS in modo robusto, semplice e compatibile con futuri aggiornamenti firmware.

È pensata in particolare per dispositivi embedded che devono conservare `Settings` e/o `Data` anche in presenza di una perdita improvvisa di alimentazione.

## Caratteristiche

- **API semplice**: normalmente bastano `loadAndUpgrade()` e `save()`.
- **Doppia copia A/B**: la nuova scrittura avviene sempre sullo slot opposto rispetto all'ultima copia valida.
- **CRC32**: ogni copia viene validata prima di essere considerata utilizzabile.
- **Sequence number**: identifica automaticamente la copia A/B più recente, con gestione dell'overflow `uint32_t`.
- **Verifica dopo la scrittura**: lo slot appena scritto viene riletto e ricontrollato.
- **Anti-wear**: se i dati da salvare sono già identici all'ultima copia valida, la NVS non viene scritta e `save()` restituisce `UNCHANGED`.
- **Versioning**: ogni blob contiene la versione della struttura.
- **Dimensione salvata**: permette di caricare una struttura vecchia più piccola in una struttura nuova più grande.
- **Default per i nuovi campi**: i campi aggiunti in coda mantengono automaticamente i valori di default quando viene caricata una versione precedente.
- **Migrazioni opzionali**: per modifiche semantiche tra versioni.
- **Auto-upgrade**: `loadAndUpgrade()` risalva automaticamente nel formato corrente una struttura vecchia caricata correttamente.
- **Recupero automatico**: se uno slot è corrotto/incompleto, viene usato l'altro slot valido.
- **Padding deterministico**: la libreria azzera la struttura prima di applicare i default, rendendo CRC e confronto anti-wear stabili.

## Requisiti

- ESP32
- Arduino framework / Arduino-ESP32
- API NVS di ESP-IDF (`nvs.h`, inclusa nel core ESP32; la NVS viene inizializzata dal core Arduino)
- La struttura `T` deve essere `trivially copyable` e `standard layout`.

## Installazione

### Arduino IDE

1. Scaricare lo ZIP della libreria.
2. In Arduino IDE scegliere **Sketch > Include Library > Add .ZIP Library...**.
3. Selezionare `NVSStorageAB.zip`.
4. Includere la libreria:

```cpp
#include <NVSStorageAB.h>
```

### Installazione manuale

Copiare la cartella `NVSStorageAB` nella directory `libraries` di Arduino.

### PlatformIO / pioarduino (VS Code)

La libreria include un manifest `library.json`, quindi PlatformIO la riconosce direttamente. Nel `platformio.ini` del proprio progetto:

```ini
[env:esp32dev]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
framework = arduino
board = esp32dev
lib_deps =
    ; da repository git
    https://github.com/<utente>/NVSStorageAB.git
    ; oppure da una copia locale (le modifiche sono viste subito)
    ; symlink://D:/percorso/NVSStorageAB
```

In alternativa si può copiare la cartella `NVSStorageAB` dentro la cartella `lib/` del progetto.

> La piattaforma pioarduino `stable` richiede il core pioarduino ≥ 6.2.0: usare l'estensione VS Code **pioarduino IDE** (al posto di "PlatformIO IDE").
> Se la build fallisce con `IncompatiblePlatform: ... depends on PlatformIO Core >=6.2.0`, il core installato è vecchio. Aggiornarlo con (VS Code chiuso):
>
> ```powershell
> & "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m pip install -U https://github.com/pioarduino/platformio-core/archive/refs/tags/v6.2.0.zip
> ```
>
> Non usare `pio upgrade`: installerebbe il core PlatformIO ufficiale da PyPI al posto di quello pioarduino.

#### Compilare gli esempi della libreria

Aprendo la cartella della libreria in VS Code, il `platformio.ini` incluso definisce un ambiente per ogni esempio (`BasicSettings`, `PowerFailData`, `VersionUpgrade`). Selezionare l'ambiente dalla barra di stato di PlatformIO e usare Build/Upload/Monitor, oppure da terminale:

```sh
pio run                      # compila tutti gli esempi
pio run -e BasicSettings -t upload -t monitor
```

Per aggiungere un nuovo esempio basta creare la cartella in `examples/` e un ambiente con `custom_example = <NomeCartella>`.

## Concetto A/B

Per uno storage dichiarato così:

```cpp
NVSStorageAB<MySettings> storage("LCORx", "set", 1, defaults);
```

la libreria usa il namespace NVS `LCORx` e crea internamente:

```text
set_A
set_B
```

Ogni slot contiene:

```text
StorageHeader
  magic
  headerSize
  version
  dataSize
  sequence
  crc32

T data
```

Esempio:

```text
A: sequence 100, CRC OK
B: sequence 101, CRC OK   <- copia corrente
```

Al salvataggio successivo viene scritto A con `sequence = 102`. Se l'alimentazione viene a mancare durante la scrittura di A e A risulta invalida, al boot successivo B rimane disponibile come ultima copia valida.

## Esempio minimo: Settings

### 1. Definire la struttura

```cpp
struct LCORxSettings {
    uint64_t ChipID;
    uint8_t  ControllerID;
    char     companyID[8];
    uint32_t flags;
    uint16_t timeout;
    uint8_t  brightness;
};
```

### 2. Definire i default

```cpp
void defaultSettings(LCORxSettings &s) {
    s.ChipID = 0;
    s.ControllerID = 1;
    strlcpy(s.companyID, "DEFAULT", sizeof(s.companyID));
    s.flags = 0;
    s.timeout = 1000;
    s.brightness = 80;
}
```

Non è necessario fare `memset()` nella funzione di default: la libreria azzera già l'intera struttura prima di chiamarla.

### 3. Creare lo storage

```cpp
LCORxSettings settings;

NVSStorageAB<LCORxSettings> settingsStorage(
    "LCORx",          // namespace NVS
    "set",            // base key -> set_A / set_B
    1,                // versione corrente
    defaultSettings
);
```

### 4. Caricare

```cpp
void setup() {
    settingsStorage.loadAndUpgrade(settings);
}
```

Al primo avvio, se non esiste nessuna copia valida, vengono caricati i default e salvata automaticamente la prima copia.

### 5. Salvare

```cpp
settings.brightness = 90;
settingsStorage.save(settings);
```

Non è necessario sapere quale slot A/B verrà utilizzato.

## Anti-wear

È automatico.

```cpp
settingsStorage.save(settings);
settingsStorage.save(settings);
```

Se la seconda chiamata contiene esattamente gli stessi dati già memorizzati, restituisce:

```cpp
Result::UNCHANGED
```

e **non effettua alcuna scrittura NVS**.

È possibile ignorare il valore di ritorno oppure controllarlo:

```cpp
auto r = settingsStorage.save(settings);

if (r == NVSStorageAB<LCORxSettings>::Result::UNCHANGED) {
    Serial.println("Nessuna scrittura necessaria");
}
```

## Aggiornare una struttura nel firmware

Regola fondamentale:

> I nuovi campi devono essere aggiunti sempre in fondo alla struttura. Non cambiare ordine, tipo o significato binario dei campi già esistenti se si vuole usare la compatibilità automatica append-only.

Versione 1:

```cpp
struct Settings {
    uint8_t controllerID;
    uint32_t flags;
};
```

Versione 2:

```cpp
struct Settings {
    uint8_t controllerID;
    uint32_t flags;

    // nuovi campi V2, sempre in fondo
    uint16_t timeout;
    uint8_t brightness;
};
```

Aggiornare anche il numero di versione:

```cpp
NVSStorageAB<Settings> storage(
    "LCORx",
    "set",
    2,
    defaults
);
```

Se viene caricata la vecchia struttura più corta:

1. la nuova struttura viene inizializzata con i default;
2. vengono copiati solo i byte presenti nella vecchia struttura;
3. `timeout` e `brightness` rimangono ai nuovi valori di default;
4. `loadAndUpgrade()` risalva il risultato nel formato V2.

## Migrazioni opzionali

Se oltre ad aggiungere campi occorre modificare semanticamente i dati:

```cpp
void migrateSettings(uint16_t oldVersion, Settings &s) {
    if (oldVersion < 2) {
        // conversioni V1 -> V2
    }

    if (oldVersion < 3) {
        // conversioni V2 -> V3
    }
}
```

Passarla al costruttore:

```cpp
NVSStorageAB<Settings> storage(
    "LCORx",
    "set",
    3,
    defaults,
    migrateSettings
);
```

## `load()` o `loadAndUpgrade()`?

### `load(data)`

- carica la copia valida più recente;
- applica eventuale migrazione in RAM;
- se non trova copie valide carica i default in RAM;
- **non scrive automaticamente** la NVS.

### `loadAndUpgrade(data)`

È la scelta consigliata nella maggior parte dei progetti:

- carica la copia valida più recente;
- applica eventuale migrazione;
- se il formato è vecchio lo risalva automaticamente nel formato corrente;
- se non esiste nessuna copia valida, inizializza e salva i default.

Se una copia esiste ma non può essere letta (errore NVS o memoria esaurita), entrambe le funzioni restituiscono l'errore (`READ_ERROR`, `OUT_OF_MEMORY`, ...) con i default in RAM e **non sovrascrivono** la NVS. Per lo stesso motivo `save()` rifiuta di scrivere se uno dei due slot non è leggibile: potrebbe contenere la copia più recente.

Uso tipico:

```cpp
settingsStorage.loadAndUpgrade(settings);
dataStorage.loadAndUpgrade(data);
```

## Power fail

La NVS **non deve essere scritta dentro una ISR**.

L'interrupt deve limitarsi a impostare un flag:

```cpp
volatile bool powerFailIRQ = false;

void IRAM_ATTR onPowerFail() {
    powerFailIRQ = true;
}
```

Il salvataggio viene effettuato dal normale contesto del programma:

```cpp
if (powerFailIRQ) {
    powerFailIRQ = false;
    dataStorage.save(data);
}
```

Se l'hardware dispone di condensatori/supercapacitori per mantenere alimentato l'ESP32 dopo la perdita dell'alimentazione principale, è comunque consigliato effettuare `save()` **subito dopo il rilevamento del power fail**, quando la tensione è ancora alta e stabile.

L'esempio `PowerFailData` mostra anche come evitare salvataggi multipli durante lo stesso evento di mancanza alimentazione.

## Settings e Data: strategia consigliata

### Settings

Salvare quando la configurazione viene modificata:

```cpp
settings.ControllerID = 12;
settingsStorage.save(settings);
```

L'anti-wear evita comunque una scrittura se il contenuto non è realmente cambiato.

### Data

Tenere i contatori in RAM durante il normale funzionamento:

```cpp
data.totalRides++;
data.totalHits += hits;
```

Al power fail:

```cpp
dataStorage.save(data);
```

## Valori restituiti

`NVSStorageAB<T>::Result` può restituire:

| Risultato | Significato |
|---|---|
| `OK` | Operazione completata |
| `UNCHANGED` | Dati identici; nessuna scrittura effettuata |
| `DEFAULTS_LOADED` | Nessuna copia valida; sono stati usati i default |
| `NVS_OPEN_ERROR` | Impossibile aprire namespace NVS |
| `READ_ERROR` | Errore di lettura (nessuna scrittura effettuata) |
| `WRITE_ERROR` | Errore di scrittura |
| `VERIFY_ERROR` | La verifica post-scrittura è fallita |
| `INVALID_HEADER` | Header non valido |
| `INVALID_SIZE` | Dimensione blob incoerente |
| `CRC_ERROR` | CRC errato |
| `OUT_OF_MEMORY` | Allocazione buffer fallita |
| `INVALID_ARGUMENT` | Namespace/base key non validi |
| `FUTURE_VERSION` | NVS scritta da una versione firmware più nuova |

Per stampare il risultato:

```cpp
Serial.println(
    NVSStorageAB<LCORxSettings>::resultToString(result)
);
```

## Limiti e regole importanti

### 1. Append-only

Per la compatibilità automatica, aggiungere nuovi membri **solo alla fine**.

### 2. Non usare oggetti dinamici nella struttura

La struttura deve essere binariamente copiabile. Sono adatti:

```cpp
uint8_t
uint16_t
uint32_t
uint64_t
int32_t
float
bool
char array[...]
struct semplici annidate
array di tipi semplici
```

Evitare membri come:

```cpp
String
std::string
std::vector
puntatori a memoria dinamica
```

### 3. Padding e ABI

La persistenza è basata sulla rappresentazione binaria della struct. È pensata per evoluzioni firmware sullo stesso ambiente ESP32/toolchain compatibile. Non è un formato di serializzazione portabile tra architetture diverse.

### 4. Namespace e key

ESP32 NVS limita i nomi. La libreria richiede:

- namespace: massimo 15 caratteri;
- `baseKey`: massimo 13 caratteri perché vengono aggiunti `_A` e `_B`.

### 5. Versioni future

Se viene trovata una copia valida con `version > currentVersion`, la libreria restituisce `FUTURE_VERSION` invece di interpretarla come una struttura vecchia. Questo evita downgrade firmware potenzialmente distruttivi.

### 6. Più task

Un oggetto `NVSStorageAB` non è protetto da mutex: se `load()`/`save()` dello stesso storage possono essere chiamati da task FreeRTOS diversi, serializzare le chiamate (ad esempio con un `SemaphoreHandle_t`). Storage diversi (namespace/baseKey diversi) possono essere usati in parallelo.

## Esempi inclusi

### `BasicSettings`

Mostra:

- struttura Settings;
- default;
- primo caricamento;
- modifica;
- salvataggio;
- anti-wear `UNCHANGED`.

### `PowerFailData`

Mostra:

- struttura Data;
- contatori mantenuti in RAM;
- interrupt di power fail;
- scrittura fuori dalla ISR;
- protezione da salvataggi ripetuti durante lo stesso evento.

### `VersionUpgrade`

Mostra:

- incremento della versione;
- aggiunta di campi in fondo;
- default dei campi nuovi;
- callback di migrazione;
- `loadAndUpgrade()`.

## API rapida

```cpp
NVSStorageAB<T> storage(
    namespaceName,
    baseKey,
    version,
    defaults,
    migrationOptional
);
```

Caricamento consigliato:

```cpp
storage.loadAndUpgrade(data);
```

Salvataggio:

```cpp
storage.save(data);
```

Cancellazione di entrambe le copie:

```cpp
storage.erase();
```

Versione corrente:

```cpp
storage.currentVersion();
```

## Filosofia della libreria

Il codice applicativo dovrebbe restare semplice:

```cpp
settingsStorage.loadAndUpgrade(settings);

settings.brightness = 90;
settingsStorage.save(settings);
```

La libreria si occupa internamente di:

```text
A/B
CRC32
sequence
validazione
recovery
versione
dimensione
migrazione
auto-upgrade
anti-wear
verifica post-scrittura
```

