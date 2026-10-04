# Eigene Firmware für das Waveshare ESP32-S3-Touch-LCD-1.85C

Bis zum 06.09.2026 lief auf dem Gerät das **fertige Release-Image** von
xiaozhi-esp32 (v2.4.2). Das war der schnelle Weg und hat den Bridge-Bau erst
möglich gemacht, bindet uns aber an fremde Entscheidungen: kein Touch,
chinesische Systemansagen, und nur Espressifs vorgefertigte Wake-Words.

Dieser Ordner enthält, was für den eigenen Bau nötig ist.

## Einrichten (einmalig, ~4 GB Download)

```bash
brew install cmake ninja ccache dfu-util
mkdir -p ~/esp && cd ~/esp
git clone -b v5.5.2 --depth 1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3
cd ~/esp && git clone --depth 1 https://github.com/78/xiaozhi-esp32.git
```

**Die Version ist nicht verhandelbar.** xiaozhi-esp32 verlangt ESP-IDF ≥ 5.5.2;
mit 5.5.1 bricht schon das Konfigurieren ab („no versions of idf match >=5.5.2").

## Bauen

```bash
ELEKTRA=/path/to/this/repo   # Pfad zu diesem Checkout
cd ~/esp/xiaozhi-esp32
git apply $ELEKTRA/firmware/03-elektra-display.patch   # Board: Touch, Akku, Display, Symbol-Tipp
git apply $ELEKTRA/firmware/02-hi-nach-wake-word.patch   # Anwendung: Hi, rundfunk-Nachrichten, SendRundfunk
git apply $ELEKTRA/firmware/04-mikrofon-agc.patch      # Codec + AFE: zwei Mikrofone, BSS, AGC
git apply $ELEKTRA/firmware/05-wake-word-schreibweisen.patch  # Wake-Word: mehrere Schreibweisen, Messlog
cp $ELEKTRA/firmware/hi.ogg main/assets/locales/de-DE/
cp $ELEKTRA/firmware/elektra_display.* main/boards/waveshare/esp32-s3-touch-lcd-1.85c/
cp $ELEKTRA/firmware/sdkconfig.defaults.rundfunk .
source ~/esp/esp-idf/export.sh
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.rundfunk" \
  idf.py set-target esp32s3
python scripts/gen_lang.py --language de-DE --output main/assets/lang_config.h
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash
```

Die `gen_lang.py`-Zeile ist nötig, weil der Sprach-Header nur an
`language.json` hängt, nicht an neuen Sounddateien — ohne sie fehlt
`Lang::Sounds::OGG_HI`. Kommt eine `.ogg` nach dem ersten Bau dazu, zusätzlich
`idf.py reconfigure`, sonst meldet der Linker `_binary_hi_ogg_start`.

## Elektra-Display

Seit 12.09.2026 zeichnet `ElektraDisplay` (firmware/elektra_display.cc) den
Bildschirm: Ein Ring trägt den Zustand (atmet im Schlaf, pulst beim Zuhören,
dreht beim Arbeiten, grün beim Antworten, orange bei Meldungen), im Schlaf
steht der Mond in der Mitte (Phase aus dem Datum, bei Neumond nur die Kontur —
so gewollt), der Akku erscheint erst unter 30 % als Tankanzeige. Entwurf und
Begründungen: <https://claude.ai/code/artifact/14f2daee-7ce8-425f-8f2f-381b8c1bbce9>,
Spec in `docs/superpowers/specs/2026-09-12-elektra-display-design.md`,


Die Klasse erbt von `SpiLcdDisplay` und überschreibt **alle** virtuellen
Methoden, die auf die Statusleiste oder das Emoji-Gesicht zugreifen — auch
`SetTheme` und `SetPreviewImage`. Wer eine vergisst, bekommt beim Laden der
Assets-Schrift einen `LoadProhibited` in `LcdDisplay::SetTheme` und ein Gerät,
das im Sekundentakt neu startet (sah aus wie „kreist im WLAN-Setup").

Neue Dateien im Board-Verzeichnis brauchen einmal `idf.py reconfigure` — der
Glob über die Board-Quellen wird nur beim Konfigurieren ausgewertet, sonst
fehlt die Datei dem Linker.

Der Akku hängt an `BAT_ADC` = GPIO 8 (Schaltplan V2, Teiler 200k/100k über
R14). Der **Akku-Schalter auf dem Board** muss auf ON stehen, sonst misst der
Pin 0 V; unter 2,5 V gilt der Akku als nicht angeschlossen und wird nicht
gezeigt. Laden ist nicht erkennbar (der ETA6098 hat keinen Pin am ESP).

Einstellbar ohne Code: `CONFIG_ELEKTRA_SCHLAF_HELLIGKEIT` in
`sdkconfig.defaults.rundfunk` (Prozent, Stand 35 — mit 12 war das Display
für uns schwarz). Im Code: `RING_BREITE` (5 px), `SCHLAF` (Ringfarbe).
`ElektraDisplay::MondDemo(true)` lässt einen Mondmonat in gut 40 s durchlaufen —
zum Anschauen der Phasen, nicht für den Betrieb.

## Rückrufe und Erinnerungen

Seit 13.09.2026 spricht die Firmware einen eigenen Nachrichtentyp
`{"type":"rundfunk", …}` mit der Bridge (Patch 02, `application.cc`):

| Richtung | Nachricht | Wirkung |
|---|---|---|
| Gerät → Bridge | `hello` beim Start und alle 30 s, solange die Leitung zu ist | das Gerät hält die Leitung **auch im Leerlauf** offen (seit 13.09.); Fehlversuche bleiben stumm (kein Piepen, keine Meldung) |
| Bridge → Gerät | `{"befehl":"schlafen"}` | zurück in den Leerlauf, **Kanal bleibt offen** — Wake-Word und Tippen öffnen dann nur noch die OpenAI-Seite |
| Bridge → Gerät | `{"befehl":"ping"}` | alle 45 s, sonst hält `Protocol::IsTimeout` den Kanal nach 120 s Stille für tot |
| Bridge → Gerät | `{"symbole":[{"art":"rueckruf","anzahl":2,"alter_min":5}, …]}` | Symbolzeile 46 px vom oberen Rand: Kuvert (Rückruf), Glocke (Erinnerung), Zähler ab 2, Farbe nach Alter (Bernstein < 1 h, leise < 24 h, sonst ausgegraut) |
| Gerät → Bridge | `{"tipp":"rueckruf"}` | Tipp auf ein Symbol (`ElektraDisplay::SymbolAn`) — die Bridge liest die älteste Nachricht der Art vor und quittiert sie |

Die Bridge nimmt Nachrichten per `POST /rundfunk/nachricht` an (Vertrag wie
telefon-ai `rueckruf.py`, plus `art`), Token in `RUNDFUNK_AGENT_TOKEN`.

## Was unser Bau anders macht

| Änderung | Warum |
|---|---|
| `01-touch-anmelden.patch` (Teil 1: Touch anmelden) | Der Board-Treiber setzt den CST816 nur über den Portexpander zurück und meldet ihn **nie bei LVGL an** — deshalb reagiert der Bildschirm im Release-Image nicht. Das Muster stammt vom Schwesterboard `esp32-s3-touch-lcd-1.83`, das denselben Controller korrekt anmeldet. Die Pins standen längst in `config.h` (`TP_PIN_NUM_INT` = GPIO 4, Reset über den TCA9554). |
| `02-hi-nach-wake-word.patch` + `hi.ogg` | Nach dem Wake-Word sagt das Gerät sofort „Hi" — lokal, ohne Cloud-Runde, in Elektras Stimme (OpenAI `gpt-4o-mini-tts`, Stimme `marin`, 0,36 s, −9 dB). Die Firmware spielt in allen Zuhör-Pfaden ein Plopp, nur nicht in dem mit `CONFIG_SEND_WAKE_WORD_DATA`, den wir benutzen — das Gespräch begann für den Sprecher unsichtbar. Der Cloud-Gruß der Bridge ist seitdem aus (`begruessung_s=0`). |
| `04-mikrofon-agc.patch` + Mikrofon-Gain 37,5 dB in Patch 03 | Man musste laut sprechen (13.09.2026). Das Board hat zwei MEMS-Mikrofone (MIC1, MIC2 am ES7210) und den Lautsprecher-Rückweg an MIC3; die Firmware nutzte nur MIC1 + Rückweg. Jetzt liest der Codec drei TDM-Slots (Reihenfolge MIC1, MIC3, MIC2 → AFE-Format `MRM`, neues Feld `input_format()` am Codec), und das AFE läuft mit zwei Mikrofonen als `AFE_TYPE_SR` mit Quellentrennung (`AEC → SE(BSS) → VAD`); `AFE_TYPE_VC` kann nur ein Mikrofon. Analoger Gain zunächst 37,5 dB auf beiden, seit dem Abend wieder 30 dB: aus der Nähe übersteuerte das Signal (Pegelspitze 32768 in genau den Sitzungen mit Verhörern wie „Milch“ statt „Mail“); das Wake-Word aus der Ferne bekommt seine Verstärkung digital im Erkenner (Patch 05, `WAKE_GAIN`). **Falle:** schaltet man in dieser Kette die AEC ab (wie die Firmware es im Zuhören tat), liefert das AFE dreifach verschachtelten Müll — Wake-Word ging noch, OpenAI verstand nichts mehr (13.09., 13:00–13:50); mit AEC dauerhaft an ist die Ausgabe sauber. Diagnose über `RUNDFUNK_MITSCHNITT=<Verzeichnis>` in der Bridge (WAV je Sitzung) und Autokorrelation bei Lag 3. Messung aus 1–4 m: vorher 1 von 5 Rufen, mit Schwelle 4 (Patch 05) 4 von 5, mit zwei Mikrofonen **5 von 5**. Die WebRTC-AGC aus dem ersten Wurf ist im SR-Typ nicht in der Kette; die Bridge verstärkt trotzdem nicht mehr (`RUNDFUNK_MIKROFON_GAIN`, Standard 1,0). |
| `05-wake-word-schreibweisen.patch` | `CONFIG_CUSTOM_WAKE_WORD` nimmt mehrere Schreibweisen (`;`), alle wecken; MultiNet meldet ab 3 % ins Log (`unter Schwelle … nicht ausgeloest`), ausgelöst wird ab `CONFIG_CUSTOM_WAKE_WORD_THRESHOLD`. So lässt sich die Erkennung ohne Ratespiel messen. |
| `CONFIG_LANGUAGE_DE_DE` | Deutsche Systemansagen statt chinesischer. |
| `CONFIG_OTA_URL` | Zeigt ab Werk auf die Bridge am Pi. Sonst muss nach jedem Flash das WLAN-Portal bemüht werden — und dort wird der zweite Speicherknopf zuverlässig übersehen (siehe `spike/README.md`). Eine im NVS gesetzte Adresse hat weiterhin Vorrang. |

## Wake-Word: wie der Motorwechsel wirklich funktioniert

Espressif liefert rund 80 fertige WakeNet-Modelle mit festen Namen (Jarvis,
Alexa, Computer …), **kein Elektra**. Ein frei gewähltes Wort geht nur über
MultiNet, den Lauterkenner. Der Weg dorthin ist im Code besser gelöst, als die
Kconfig-Hilfe vermuten lässt:

**Die Erkennungsart bestimmen die Daten, nicht der Bau.** In
`main/audio/engines/afe_audio_engine.cc`:

```cpp
multinet_model_name = esp_srmodel_filter(models_, ESP_MN_PREFIX, nullptr);
if (multinet_model_name != nullptr) {
    wake_detector_ = WakeDetector::kMultiNet;      // MultiNet gewinnt
    custom_wake_word_->Initialize(codec_, models_);  // models_ ist NICHT null
} else if (wakenet_model_name != nullptr) {
    wake_detector_ = WakeDetector::kWakeNet;
}
```

Liegt ein MultiNet-Modell in der Assets-Partition, gewinnt es gegen WakeNet.

**Und die berüchtigte Zeile `language_ = "cn"` ist keine Sackgasse.** In
`main/audio/wake_words/custom_wake_word.cc` steht sie im Zweig
`models_list == nullptr` — der greift nur, wenn gar keine Assets vorhanden
sind. Der Normalfall liest `index.json` aus den Assets:

```json
"multinet_model": {
    "language": "en",
    "duration": 3000,
    "threshold": 0.2,
    "commands": [{"command": "elektra", "text": "Elektra", "action": "wake"}]
}
```

Diesen Block erzeugt `scripts/build_default_assets.py` aus der sdkconfig, und
die Sprache leitet er aus dem **Namen des MultiNet-Modells** ab
(`get_language_from_multinet_models`): `mn7_en` → `"en"`, `mn6_cn` → `"cn"`.

Also genügen fünf Einstellungen (siehe `sdkconfig.defaults.rundfunk`):

```
CONFIG_USE_AFE_WAKE_WORD=n
CONFIG_USE_CUSTOM_WAKE_WORD=y
CONFIG_CUSTOM_WAKE_WORD="elektra"
CONFIG_CUSTOM_WAKE_WORD_DISPLAY="Elektra"
CONFIG_SR_MN_EN_MULTINET7_QUANT=y
```

**Was das kostet:** MultiNet ist eine Lauterkennung, kein trainiertes
Namensmodell. Sie ist flexibel, aber anfälliger für Fehlauslöser, und die
Aussprache wird englisch interpretiert. Der Schwellenwert
(`CONFIG_CUSTOM_WAKE_WORD_THRESHOLD`, Vorgabe 20 %) ist die Stellschraube:
höher heißt weniger Fehlalarme und mehr Nichterkennungen. **Bei 20 % hat
Elektra auf die Stimme des Besitzers nicht reagiert, bei 8 % nur auf laute Rufe**
(12.09.2026). Gemessen am 13.09. mit `05-wake-word-schreibweisen.patch`, der
jede Erkennung ab 3 % ins Log schreibt: fünf Rufe aus 1 m in normaler Stimme
kamen bei 14,7 / 7,7 / 4,3 / <3 / <3 % an — das englische Modell hört das
deutsche Wort nur schwach, Mikrofon-Gain und AGC (Patch 04) ändern daran
wenig. **Mit Schwelle 4 % und sechs Schreibweisen (mit `;` getrennt in
`CONFIG_CUSTOM_WAKE_WORD`) lösten 4 von 5 Rufen aus** (11,7 / 3,9 / 4,1 /
8,7 / 5,4 %); meist trifft die Variante `electra`. Fehlauslöser beobachten.

**Aus der Ferne** (13.09. abends: reagiert nicht auf weite Rufe, aus der Nähe geweckt versteht sie danach alles) hilft zweierlei: Patch 05 verstärkt das Audio nur für den Erkenner ×3 (`WAKE_GAIN`; das Audio zur Cloud bleibt unverändert), und **längere Rufe tragen weiter** — Messung mit der Mac-Stimme bei Lautstärke 50: „Elektra“ 3,8 % (nein), „Hey Elektra“ 6,7 %, „Hallo Elektra“ 17,7 %. Beide Formen stehen in `CONFIG_CUSTOM_WAKE_WORD`.
Der Wert steht in `sdkconfig` *und* in `index.json` der Assets; wer ihn
ändert, muss beide neu bauen.

**Nachträglich änderbar:** Weil das Wort in `index.json` steht und nicht im
Programmcode, lässt sich ein anderes Wort auch ohne Neubau in die
Assets-Partition packen (`spike/assets-tools/`). Das Umschalten zwischen
WakeNet und MultiNet dagegen braucht den Bau.


## Tippen zum Sprechen

Im selben Patch steckt eine zweite Änderung: **Ein Tipp auf den Bildschirm
startet und beendet das Gespräch** — dieselbe Wirkung wie der BOOT-Taster, nur
erreichbar.

Der Grund ist nicht Bequemlichkeit, sondern Verlässlichkeit. Bis hierher war
das Wake-Word die einzige Bedienung, und es ist die unzuverlässigste Stelle der
ganzen Kette: Espressifs trainierte Modelle tragen feste englische Namen, ein
frei gewähltes Wort läuft über den Lauterkenner und ist noch fehleranfälliger.
Wer dreimal rufen muss, will einmal tippen.

Bewusst die **ganze Fläche** und keine Schaltfläche: Auf einem runden 360er
Display ohne Zeigegerät gibt es keinen falschen Ort zum Tippen, und einen Knopf
müsste man erst finden. `LV_EVENT_CLICKED` statt `PRESSED`, damit ein Wischen
über den Bildschirm nichts auslöst.

Der Ereignis-Zeiger hängt am aktiven Bildschirm und überlebt den späteren
Aufbau der Oberfläche, weil `LcdDisplay::SetupUI()` auf demselben Bildschirm
aufbaut statt einen neuen zu laden.

**Ungetestet.** Ich kann das Gerät nicht antippen. Im Protokoll erscheint bei
Erfolg `Bildschirm angetippt — Gespraech umschalten`.


**Am Eingabegerät angemeldet, nicht am Screen.** Der erste Versuch hing den
Handler an `lv_screen_active()` und blieb stumm: Die Anzeige legt einen
bildschirmfüllenden Container über den Screen, Klicks darauf erreichen den
Screen nie (kein `EVENT_BUBBLE`). LVGL schickt `PRESSED`/`CLICKED` aber
zusätzlich an das Eingabegerät selbst (`lv_indev.c`, `send_event`), egal
welches Objekt getroffen wurde — `lv_indev_add_event_cb` auf dem Rückgabewert
von `lvgl_port_add_touch` ist deshalb der robuste Ort. Verifiziert 12.09.2026:
Tippen → `idle -> connecting` in 60 ms → Gespräch.

## Entwurf für die Bildschirmoberfläche

Sechs Zustände, maßstabsgetreu gerendert (360 Pixel, so groß wie das Panel):
<https://claude.ai/code/artifact/14f2daee-7ce8-425f-8f2f-381b8c1bbce9>

**Nichts davon ist auf dem Gerät.** Bildschirmdesign blind zu flashen ist keine
gute Idee — wer es baut, sieht das Ergebnis nicht. Der Entwurf ist zum Ansehen
und Entscheiden da.

Die tragende Idee: **Der Ring trägt den Zustand.** Aus drei Metern liest niemand
eine Statuszeile, aber eine Randfarbe und eine Drehung sieht man über die ganze
Zimmerbreite. Der Text in der Mitte ist für den Meter davor. Praktischer
Nebeneffekt: Ohne Grafikbeschleuniger ist ein `lv_arc` mit animiertem
Startwinkel das Einzige, was auf 360 × 360 flüssig laufen kann — eine
Neuzeichnung des ganzen Bildschirms wäre es nicht.

Vier Festlegungen stecken darin, jede umkehrbar:

1. **Uhr statt Gesicht** im Ruhezustand — eine Uhr wird täglich benutzt, ein
   Emoji-Gesicht nach zehn Minuten nicht mehr.
2. **Zwei Werte in der Fußzeile**, nicht acht. Badtemperatur und Fensterkontakt.
3. **Mitgeschriebene Sätze** — weil die Spracherkennung aus „Jarvis" ein
   „Çavuş" machte und das nur im Protokoll sichtbar war.
4. **Kein Kachel-Bedienfeld** — neun Geräte passen nicht auf einen Kreis, und
   wer davorsteht, kann sprechen.

Aufwand für die Zustände 1 bis 5: eine eigene Display-Klasse neben
`LcdDisplay`, geschätzt 250 Zeilen C++, ohne neue Abhängigkeit.
