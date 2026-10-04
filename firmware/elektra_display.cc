// elektra_display.cc — siehe elektra_display.h
#include "elektra_display.h"

#include "assets/lang_config.h"
#include "board.h"

#include <esp_log.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#define TAG "ElektraDisplay"

LV_FONT_DECLARE(font_noto_sans_basic_16_4);
LV_FONT_DECLARE(font_noto_sans_basic_20_4);
LV_FONT_DECLARE(font_noto_sans_basic_30_4);
LV_FONT_DECLARE(font_material_symbols_20_4);
#include "material_symbols.h"

namespace {
// Farben aus dem Entwurf. Kein Reinschwarz: das Panel ist ein LCD.
const lv_color_t PANEL      = lv_color_hex(0x14110E);
const lv_color_t SCHLAF     = lv_color_hex(0x8A7A66);   // 12.09.: 0x5A4E40 war vom Sofa aus schwarz
const lv_color_t BERNSTEIN  = lv_color_hex(0xE8A33D);
const lv_color_t GRUEN      = lv_color_hex(0x86B36B);
const lv_color_t ORANGE     = lv_color_hex(0xE0763C);
const lv_color_t MOND       = lv_color_hex(0xD9CDBB);
const lv_color_t LEISE      = lv_color_hex(0x9A8D80);
const lv_color_t TEXT       = lv_color_hex(0xF2EAE0);
const lv_color_t DUNKEL     = lv_color_hex(0x2A241E);

constexpr int RING_RAND = 8;      // Abstand Ring zum Panelrand, wie im Entwurf
constexpr int RING_BREITE = 5;      // 12.09.: 3 px waren vom Sofa aus zu duenn
constexpr int MOND_DURCHMESSER = 92;
constexpr int SYMBOL_OBEN = 46;       // Abstand der Symbolzeile vom oberen Rand (Entwurf)
constexpr int SYMBOL_KACHEL = 34;
constexpr int SYMBOL_ABSTAND = 14;

void RingOpa(void* obj, int32_t v) { lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), v, 0); }
void RingDrehung(void* obj, int32_t v) { lv_arc_set_rotation(static_cast<lv_obj_t*>(obj), v); }
}  // namespace

ElektraDisplay::ElektraDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                               int width, int height, int offset_x, int offset_y, bool mirror_x,
                               bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {}

void ElektraDisplay::SetupUI() {
    if (setup_ui_called_) return;
    Display::SetupUI();
    DisplayLockGuard lock(this);

    auto screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, PANEL, 0);
    lv_obj_set_style_text_color(screen, TEXT, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // Ring: ein lv_arc ohne Knopf, voller Kreis, Hintergrundbogen traegt die Farbe.
    ring_ = lv_arc_create(screen);
    lv_obj_set_size(ring_, width_ - 2 * RING_RAND, height_ - 2 * RING_RAND);
    lv_obj_center(ring_);
    lv_arc_set_bg_angles(ring_, 0, 360);
    lv_arc_set_value(ring_, 0);
    lv_obj_remove_style(ring_, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(ring_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(ring_, RING_BREITE, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring_, RING_BREITE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(ring_, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ring_, true, LV_PART_MAIN);

    // Tankanzeige: derselbe Kreis, Bogen von unten im Uhrzeigersinn. Unsichtbar,
    // solange der Akku ueber 30 % hat — dann gibt es nichts zu wissen.
    akku_ring_ = lv_arc_create(screen);
    lv_obj_set_size(akku_ring_, width_ - 2 * RING_RAND, height_ - 2 * RING_RAND);
    lv_obj_center(akku_ring_);
    lv_arc_set_bg_angles(akku_ring_, 0, 360);
    lv_arc_set_rotation(akku_ring_, 90);                 // 0° = unten
    lv_arc_set_range(akku_ring_, 0, 100);
    lv_arc_set_value(akku_ring_, 0);
    lv_obj_remove_style(akku_ring_, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(akku_ring_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(akku_ring_, RING_BREITE, LV_PART_MAIN);
    lv_obj_set_style_arc_width(akku_ring_, RING_BREITE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(akku_ring_, DUNKEL, LV_PART_MAIN);
    lv_obj_set_style_arc_color(akku_ring_, BERNSTEIN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(akku_ring_, true, LV_PART_INDICATOR);
    lv_obj_add_flag(akku_ring_, LV_OBJ_FLAG_HIDDEN);

    // Mond: eine kleine Leinwand, pixelweise gezeichnet. Zwei uebereinander
    // geschobene Kreise (erster Versuch) springen bei Halbmond, weil der
    // Terminator dort von hohl auf bauchig kippt — in Wirklichkeit ist er
    // eine Ellipse, die bei Halbmond zur Geraden wird (12.09.2026, Video).
    mond_ = lv_canvas_create(screen);
    mond_buf_ = lv_draw_buf_create(MOND_DURCHMESSER, MOND_DURCHMESSER, LV_COLOR_FORMAT_RGB565,
                                   LV_STRIDE_AUTO);
    lv_canvas_set_draw_buf(mond_, mond_buf_);
    lv_obj_center(mond_);
    lv_obj_remove_flag(mond_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(mond_, LV_OBJ_FLAG_HIDDEN);

    // Mitte: Zustandswort klein oben, Satz gross darunter.
    zustand_label_ = lv_label_create(screen);
    lv_obj_set_style_text_font(zustand_label_, &font_noto_sans_basic_16_4, 0);
    lv_obj_set_style_text_color(zustand_label_, BERNSTEIN, 0);
    lv_obj_set_style_text_letter_space(zustand_label_, 2, 0);
    lv_label_set_text(zustand_label_, "");
    lv_obj_align(zustand_label_, LV_ALIGN_CENTER, 0, -58);

    satz_label_ = lv_label_create(screen);
    lv_obj_set_width(satz_label_, width_ - 2 * 54);
    lv_label_set_long_mode(satz_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(satz_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(satz_label_, &font_noto_sans_basic_20_4, 0);
    lv_obj_set_style_text_color(satz_label_, TEXT, 0);
    lv_label_set_text(satz_label_, "");
    lv_obj_align(satz_label_, LV_ALIGN_CENTER, 0, 0);

    fuss_label_ = lv_label_create(screen);
    lv_obj_set_style_text_font(fuss_label_, &font_noto_sans_basic_16_4, 0);
    lv_obj_set_style_text_color(fuss_label_, LEISE, 0);
    lv_label_set_text(fuss_label_, "");
    lv_obj_align(fuss_label_, LV_ALIGN_BOTTOM_MID, 0, -44);

    // Symbolzeile: Kuvert fuer Rueckrufe, Glocke fuer Erinnerungen.
    // Ein Flex-Container, der seine Kacheln mittig setzt; leer ist er unsichtbar.
    symbol_zeile_ = lv_obj_create(screen);
    lv_obj_set_size(symbol_zeile_, width_, SYMBOL_KACHEL + 8);
    lv_obj_align(symbol_zeile_, LV_ALIGN_TOP_MID, 0, SYMBOL_OBEN - 4);
    lv_obj_set_style_bg_opa(symbol_zeile_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(symbol_zeile_, 0, 0);
    lv_obj_set_style_pad_all(symbol_zeile_, 4, 0);
    lv_obj_set_style_pad_column(symbol_zeile_, SYMBOL_ABSTAND, 0);
    lv_obj_set_flex_flow(symbol_zeile_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(symbol_zeile_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(symbol_zeile_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(symbol_zeile_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(symbol_zeile_, LV_OBJ_FLAG_HIDDEN);

    SetzeZustand(Zustand::Schlaeft);
#ifdef ELEKTRA_MOND_DEMO
    MondDemo(true);
#endif
}

void ElektraDisplay::SetzeZustand(Zustand zustand) {
    DisplayLockGuard lock(this);
    if (ring_ == nullptr) return;
    zustand_ = zustand;
    ZeichneRing();
    ZeichneMitte();
    ZeichneAkku();

    auto backlight = Board::GetInstance().GetBacklight();
    if (backlight != nullptr) {
        if (zustand == Zustand::Schlaeft) {
            backlight->SetBrightness(CONFIG_ELEKTRA_SCHLAF_HELLIGKEIT);
        } else {
            backlight->RestoreBrightness();
        }
    }
}

void ElektraDisplay::ZeichneRing() {
    if (ring_anim_laeuft_) {
        lv_anim_delete(ring_, nullptr);
        ring_anim_laeuft_ = false;
    }
    lv_arc_set_bg_angles(ring_, 0, 360);
    lv_arc_set_rotation(ring_, 0);
    lv_obj_set_style_opa(ring_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(ring_, LV_OBJ_FLAG_HIDDEN);

    lv_anim_init(&ring_anim_);
    lv_anim_set_var(&ring_anim_, ring_);
    lv_anim_set_repeat_count(&ring_anim_, LV_ANIM_REPEAT_INFINITE);

    switch (zustand_) {
        case Zustand::Schlaeft:
            lv_obj_set_style_arc_color(ring_, SCHLAF, LV_PART_MAIN);
            lv_anim_set_exec_cb(&ring_anim_, RingOpa);
            lv_anim_set_values(&ring_anim_, LV_OPA_COVER * 45 / 100, LV_OPA_COVER);
            lv_anim_set_duration(&ring_anim_, 3000);
            lv_anim_set_reverse_duration(&ring_anim_, 3000);
            lv_anim_set_path_cb(&ring_anim_, lv_anim_path_ease_in_out);
            lv_anim_start(&ring_anim_);
            ring_anim_laeuft_ = true;
            break;
        case Zustand::Verbindet:
            lv_obj_set_style_arc_color(ring_, BERNSTEIN, LV_PART_MAIN);
            break;
        case Zustand::Hoert:
            lv_obj_set_style_arc_color(ring_, BERNSTEIN, LV_PART_MAIN);
            lv_anim_set_exec_cb(&ring_anim_, RingOpa);
            lv_anim_set_values(&ring_anim_, LV_OPA_COVER, LV_OPA_COVER * 38 / 100);
            lv_anim_set_duration(&ring_anim_, 950);
            lv_anim_set_reverse_duration(&ring_anim_, 950);
            lv_anim_set_path_cb(&ring_anim_, lv_anim_path_ease_in_out);
            lv_anim_start(&ring_anim_);
            ring_anim_laeuft_ = true;
            break;
        case Zustand::Arbeitet:
            lv_obj_set_style_arc_color(ring_, BERNSTEIN, LV_PART_MAIN);
            lv_arc_set_bg_angles(ring_, 0, 60);          // ein Bogen, der kreist
            lv_anim_set_exec_cb(&ring_anim_, RingDrehung);
            lv_anim_set_values(&ring_anim_, 0, 360);
            lv_anim_set_duration(&ring_anim_, 1150);
            lv_anim_set_path_cb(&ring_anim_, lv_anim_path_linear);
            lv_anim_start(&ring_anim_);
            ring_anim_laeuft_ = true;
            break;
        case Zustand::Antwortet:
            lv_obj_set_style_arc_color(ring_, GRUEN, LV_PART_MAIN);
            break;
        case Zustand::Meldung:
            lv_obj_set_style_arc_color(ring_, ORANGE, LV_PART_MAIN);
            break;
    }
}

void ElektraDisplay::ZeichneMitte() {
    const bool schlaeft = zustand_ == Zustand::Schlaeft;
    if (mond_ != nullptr && !schlaeft) {
        lv_obj_add_flag(mond_, LV_OBJ_FLAG_HIDDEN);
    }
    if (schlaeft) {
        lv_label_set_text(zustand_label_, "");
        lv_label_set_text(satz_label_, "");
        lv_obj_set_style_text_color(fuss_label_, LEISE, 0);
        lv_label_set_text(fuss_label_, "");
        ZeichneMond();
        return;
    }
    switch (zustand_) {
        case Zustand::Verbindet:
            lv_obj_set_style_text_color(zustand_label_, BERNSTEIN, 0);
            lv_label_set_text(zustand_label_, "VERBINDET");
            lv_label_set_text(fuss_label_, "");
            break;
        case Zustand::Hoert:
            lv_obj_set_style_text_color(zustand_label_, BERNSTEIN, 0);
            lv_label_set_text(zustand_label_, "HÖRT ZU");
            lv_obj_set_style_text_font(satz_label_, &font_noto_sans_basic_30_4, 0);
            lv_label_set_text(satz_label_, "Sprich einfach.");
            lv_label_set_text(fuss_label_, "Elektra · oder tippen");
            break;
        case Zustand::Arbeitet:
            lv_obj_set_style_text_color(zustand_label_, BERNSTEIN, 0);
            lv_label_set_text(zustand_label_, "ARBEITET");
            lv_obj_set_style_text_font(satz_label_, &font_noto_sans_basic_20_4, 0);
            break;   // satz_label_ behaelt das zuletzt Gehoerte
        case Zustand::Antwortet:
            lv_obj_set_style_text_color(zustand_label_, GRUEN, 0);
            lv_label_set_text(zustand_label_, "ANTWORTET");
            lv_obj_set_style_text_font(satz_label_, &font_noto_sans_basic_20_4, 0);
            lv_label_set_text(fuss_label_, "");
            break;
        case Zustand::Meldung:
            lv_obj_set_style_text_color(zustand_label_, ORANGE, 0);
            lv_label_set_text(zustand_label_, "MELDUNG");
            lv_obj_set_style_text_font(satz_label_, &font_noto_sans_basic_20_4, 0);
            lv_label_set_text(fuss_label_, "");
            break;
        case Zustand::Schlaeft:
            break;
    }
    lv_obj_align(satz_label_, LV_ALIGN_CENTER, 0, 0);
}

void ElektraDisplay::ZeichneMond() {
    if (mond_ == nullptr || mond_buf_ == nullptr) return;
    const double periode = 29.530588853;
    double alter;
    if (mond_demo_alter_ >= 0) {
        alter = fmod(mond_demo_alter_, periode);
    } else {
        time_t jetzt = time(nullptr);
        if (jetzt < 1700000000) {             // keine Serverzeit: kein Mond, nur Ring
            lv_obj_add_flag(mond_, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        // Mondalter in Tagen seit dem Neumond vom 6. Januar 2000, 18:14 UTC.
        const double referenz = 947182440.0;
        alter = fmod((double)jetzt - referenz, periode * 86400.0) / 86400.0;
        if (alter < 0) alter += periode;
        mond_tag_ = (int)(jetzt / 86400);
    }
    const float phi = (float)(2.0 * M_PI * alter / periode);   // 0 Neumond, pi Vollmond
    const float beleuchtet = (1.0f - cosf(phi)) / 2.0f;
    const bool zunehmend = alter < periode / 2.0;

    // Terminator: in jeder Zeile mit Halbbreite w liegt die Licht-Schatten-
    // Grenze bei x = w*cos(phi) (zunehmend, Licht rechts) bzw. x = -w*cos(phi)
    // (abnehmend, Licht links). Das ergibt die Ellipse, die bei Halbmond zur
    // Geraden wird. 4x4 Unterabtastung je Pixel glaettet die Kanten.
    // Float statt double: der ESP32-S3 rechnet double in Software, das
    // kostete 600 ms je Bild und liess den Ring stottern.
    const float r = MOND_DURCHMESSER / 2.0f;
    const float c = cosf(phi) * (zunehmend ? 1.0f : -1.0f);
    constexpr int N = 4;
    const uint8_t licht[3] = {0xD9, 0xCD, 0xBB};     // MOND
    const uint8_t erde[3]  = {0x24, 0x1F, 0x1A};     // Erdlicht: die dunkle Seite bleibt als Kontur sichtbar
    const uint8_t panel[3] = {0x14, 0x11, 0x0E};     // PANEL
    auto* px = reinterpret_cast<uint16_t*>(mond_buf_->data);
    const int stride = mond_buf_->header.stride / 2;
    for (int y = 0; y < MOND_DURCHMESSER; y++) {
        float w2[N], w[N];
        for (int sy = 0; sy < N; sy++) {
            const float dy = y + (sy + 0.5f) / N - r;
            w2[sy] = r * r - dy * dy;
            w[sy] = w2[sy] > 0 ? sqrtf(w2[sy]) : 0;
        }
        for (int x = 0; x < MOND_DURCHMESSER; x++) {
            int n_licht = 0, n_erde = 0;
            for (int sy = 0; sy < N; sy++) {
                if (w2[sy] <= 0) continue;
                for (int sx = 0; sx < N; sx++) {
                    const float dx = x + (sx + 0.5f) / N - r;
                    if (dx * dx > w2[sy]) continue;                 // ausserhalb der Scheibe
                    // zunehmend: hell rechts vom Terminator; abnehmend gespiegelt (c negiert, dx negiert)
                    const bool hell = zunehmend ? (dx >= w[sy] * c) : (dx <= w[sy] * c);
                    if (hell) n_licht++; else n_erde++;
                }
            }
            const int n_panel = N * N - n_licht - n_erde;
            const int rr = (licht[0] * n_licht + erde[0] * n_erde + panel[0] * n_panel) / (N * N);
            const int gg = (licht[1] * n_licht + erde[1] * n_erde + panel[1] * n_panel) / (N * N);
            const int bb = (licht[2] * n_licht + erde[2] * n_erde + panel[2] * n_panel) / (N * N);
            px[y * stride + x] = lv_color_to_u16(lv_color_make(rr, gg, bb));
        }
    }
    lv_obj_invalidate(mond_);
    if (zustand_ == Zustand::Schlaeft) lv_obj_remove_flag(mond_, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "Mond: Alter %.1f d, %.0f %% beleuchtet, %s%s", alter, beleuchtet * 100,
             zunehmend ? "zunehmend" : "abnehmend", mond_demo_alter_ >= 0 ? " (Demo)" : "");
}

void ElektraDisplay::MondDemo(bool an) {
    DisplayLockGuard lock(this);
    if (!an) {
        if (mond_demo_timer_) { lv_timer_delete(mond_demo_timer_); mond_demo_timer_ = nullptr; }
        mond_demo_alter_ = -1;
        ZeichneMond();
        return;
    }
    mond_demo_alter_ = 0;
    // Ein Mondmonat in gut 40 Sekunden: alle 700 ms einen halben Tag weiter.
    mond_demo_timer_ = lv_timer_create([](lv_timer_t* t) {
        auto* self = static_cast<ElektraDisplay*>(lv_timer_get_user_data(t));
        self->mond_demo_alter_ += 0.5;
        self->ZeichneMond();
    }, 700, this);
}

void ElektraDisplay::ZeichneAkku() {
    if (akku_ring_ == nullptr) return;
    const bool zeigen = zustand_ == Zustand::Schlaeft && akku_prozent_ < 30;
    if (!zeigen) {
        lv_obj_add_flag(akku_ring_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    // Der Akku-Ring ersetzt den atmenden Ring: Atmen aus, Ring weg.
    if (ring_anim_laeuft_) {
        lv_anim_delete(ring_, nullptr);
        ring_anim_laeuft_ = false;
    }
    lv_obj_add_flag(ring_, LV_OBJ_FLAG_HIDDEN);
    lv_arc_set_value(akku_ring_, akku_prozent_);
    lv_obj_remove_flag(akku_ring_, LV_OBJ_FLAG_HIDDEN);
    char text[8];
    snprintf(text, sizeof(text), "%d %%", akku_prozent_);
    lv_obj_set_style_text_color(fuss_label_, SCHLAF, 0);
    lv_label_set_text(fuss_label_, text);
}

// --- Was die Anwendung aufruft ------------------------------------------------

void ElektraDisplay::SetStatus(const char* status) {
    // application.cc meldet den Geraetezustand nur ueber diese Texte.
    if (strcmp(status, Lang::Strings::STANDBY) == 0) {
        SetzeZustand(Zustand::Schlaeft);
    } else if (strcmp(status, Lang::Strings::CONNECTING) == 0) {
        SetzeZustand(Zustand::Verbindet);
    } else if (strcmp(status, Lang::Strings::LISTENING) == 0) {
        SetzeZustand(Zustand::Hoert);
    } else if (strcmp(status, Lang::Strings::SPEAKING) == 0) {
        if (zustand_ != Zustand::Arbeitet) SetzeZustand(Zustand::Antwortet);
    } else {
        // Alles andere (WLAN einrichten, Aktivierung, Update, Fehler) ist eine Meldung.
        SetzeZustand(Zustand::Meldung);
        DisplayLockGuard lock(this);
        lv_label_set_text(satz_label_, status);
        lv_obj_align(satz_label_, LV_ALIGN_CENTER, 0, 0);
    }
}

void ElektraDisplay::ShowNotification(const std::string& notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void ElektraDisplay::ShowNotification(const char* notification, int duration_ms) {
    // Hinweise (WLAN gefunden, Version, Fehler) stehen in der Mitte. Kam der
    // Hinweis im Schlaf, geht das Geraet nach duration_ms wieder schlafen —
    // sonst bliebe der orange Ring stehen, bis jemand spricht.
    if (satz_label_ == nullptr) return;
    const bool aus_dem_schlaf = zustand_ == Zustand::Schlaeft;
    if (aus_dem_schlaf) SetzeZustand(Zustand::Meldung);
    DisplayLockGuard lock(this);
    lv_obj_set_style_text_font(satz_label_, &font_noto_sans_basic_20_4, 0);
    lv_label_set_text(satz_label_, notification);
    lv_obj_align(satz_label_, LV_ALIGN_CENTER, 0, 0);
    if (meldung_timer_) { lv_timer_delete(meldung_timer_); meldung_timer_ = nullptr; }
    if (aus_dem_schlaf && duration_ms > 0) {
        meldung_timer_ = lv_timer_create([](lv_timer_t* t) {
            auto* self = static_cast<ElektraDisplay*>(lv_timer_get_user_data(t));
            self->meldung_timer_ = nullptr;
            if (self->zustand_ == Zustand::Meldung) self->SetzeZustand(Zustand::Schlaeft);
        }, duration_ms, this);
        lv_timer_set_repeat_count(meldung_timer_, 1);
    }
}

void ElektraDisplay::SetEmotion(const char* emotion) {
    // Kein Gesicht mehr. Die Bridge schickt "thinking", wenn ein Werkzeug laeuft.
    if (strcmp(emotion, "thinking") == 0) {
        if (zustand_ != Zustand::Schlaeft) SetzeZustand(Zustand::Arbeitet);
    } else if (zustand_ == Zustand::Arbeitet) {
        SetzeZustand(Zustand::Antwortet);
    }
}

void ElektraDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (satz_label_ == nullptr || zustand_ == Zustand::Schlaeft) return;
    if (strcmp(role, "system") == 0 && content[0] == '\0') return;   // Aufraeumen, nichts zeigen
    if (strncmp(content, "Radio: ", 7) == 0) {
        // die Bridge spielt einen Sender. Das Wort oben sagt es,
        // der Sendername steht gross in der Mitte, der Ring bleibt gruen.
        lv_obj_set_style_text_color(zustand_label_, GRUEN, 0);
        lv_label_set_text(zustand_label_, "RADIO");
        lv_obj_set_style_text_font(satz_label_, &font_noto_sans_basic_30_4, 0);
        lv_label_set_text(satz_label_, content + 7);
        lv_label_set_text(fuss_label_, "Tippen stoppt");
        lv_obj_align(satz_label_, LV_ALIGN_CENTER, 0, 0);
        return;
    }
    lv_obj_set_style_text_font(satz_label_, &font_noto_sans_basic_20_4, 0);
    lv_label_set_text(satz_label_, content);
    lv_obj_align(satz_label_, LV_ALIGN_CENTER, 0, 0);
}

void ElektraDisplay::ClearChatMessages() {
    DisplayLockGuard lock(this);
    if (satz_label_ != nullptr && zustand_ != Zustand::Hoert) lv_label_set_text(satz_label_, "");
}

void ElektraDisplay::SetPowerSaveMode(bool on) { /* der Schlaf ist schon leise */ }

void ElektraDisplay::SetTheme(Theme* theme) {
    // Die Assets-Partition liefert beim Start eine Schrift und ein Thema
    // (Assets::Apply -> SetTextFont -> SetTheme). Wir zeichnen mit den
    // eingebauten Noto-Schnitten und festen Farben — nur merken, nicht anwenden.
    DisplayLockGuard lock(this);
    current_theme_ = theme;
}

void ElektraDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    ESP_LOGW(TAG, "Bildvorschau wird auf diesem Bildschirm nicht gezeigt");
}

void ElektraDisplay::UpdateStatusBar(bool update_all) {
    // Jede Sekunde aus der Hauptschleife.
    // Ein neuer Kalendertag im Schlaf: Mond neu zeichnen, sonst zeigt ein
    // Geraet, das drei Tage nicht angesprochen wurde, drei Tage alten Mond.
    if (zustand_ == Zustand::Schlaeft && mond_demo_alter_ < 0) {
        time_t jetzt = time(nullptr);
        if (jetzt >= 1700000000 && (int)(jetzt / 86400) != mond_tag_) {
            DisplayLockGuard lock(this);
            ZeichneMond();
        }
    }
    // Der Board-Code misst den Akku nur alle 30 s.
    int prozent = 100;
    bool laedt = false, entlaedt = false;
    if (!Board::GetInstance().GetBatteryLevel(prozent, laedt, entlaedt)) return;
    if (prozent == akku_prozent_ && !update_all) return;
    akku_prozent_ = prozent;
    DisplayLockGuard lock(this);
    ZeichneAkku();
}

// --- Symbole -------------------------------------------------------

const char* ElektraDisplay::SymbolAn(int x, int y) const {
    if (symbol_zeile_ == nullptr || symbole_.empty()) return nullptr;
    if (lv_obj_has_flag(symbol_zeile_, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    // Grosszuegig: die ganze Zeile zaehlt als Treffer, aufgeteilt auf die Kacheln.
    // Auf 360 px trifft niemand eine 34-px-Kachel genau.
    lv_area_t z;
    lv_obj_get_coords(symbol_zeile_, &z);
    if (y < z.y1 - 12 || y > z.y2 + 12) return nullptr;
    const int n = symbole_.size();
    const int breite = n * SYMBOL_KACHEL + (n - 1) * SYMBOL_ABSTAND;
    const int links = (z.x1 + z.x2) / 2 - breite / 2;
    if (x < links - 12 || x > links + breite + 12) return nullptr;
    int i = (x - links) / (SYMBOL_KACHEL + SYMBOL_ABSTAND);
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;
    return symbole_[i].art.c_str();
}

void ElektraDisplay::SetzeSymbole(const std::vector<Symbol>& symbole) {
    DisplayLockGuard lock(this);
    symbole_ = symbole;
    ZeichneSymbole();
}

void ElektraDisplay::ZeichneSymbole() {
    if (symbol_zeile_ == nullptr) return;
    lv_obj_clean(symbol_zeile_);
    if (symbole_.empty()) {
        lv_obj_add_flag(symbol_zeile_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    for (const auto& sym : symbole_) {
        // Drei Formen: frisch (Bernstein), alt (leise), ausgegraut (ueber 24 h).
        lv_color_t farbe = sym.alter_min < 60 ? BERNSTEIN : sym.alter_min < 24 * 60 ? LEISE : DUNKEL;
        auto* kachel = lv_obj_create(symbol_zeile_);
        lv_obj_set_size(kachel, SYMBOL_KACHEL, SYMBOL_KACHEL);
        lv_obj_set_style_radius(kachel, 9, 0);
        lv_obj_set_style_bg_color(kachel, lv_color_hex(0x201B16), 0);
        lv_obj_set_style_border_width(kachel, 1, 0);
        lv_obj_set_style_border_color(kachel, lv_color_hex(0x3A322A), 0);
        lv_obj_set_style_pad_all(kachel, 0, 0);
        lv_obj_remove_flag(kachel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(kachel, LV_OBJ_FLAG_CLICKABLE);

        auto* glyph = lv_label_create(kachel);
        lv_obj_set_style_text_font(glyph, &font_material_symbols_20_4, 0);
        lv_obj_set_style_text_color(glyph, farbe, 0);
        lv_label_set_text(glyph, sym.art == "erinnerung" ? MATERIAL_SYMBOLS_NOTIFICATIONS
                                                          : MATERIAL_SYMBOLS_MAIL);
        lv_obj_center(glyph);

        if (sym.anzahl > 1) {
            auto* zahl = lv_label_create(kachel);
            lv_obj_set_style_text_font(zahl, &font_noto_sans_basic_16_4, 0);
            lv_obj_set_style_text_color(zahl, PANEL, 0);
            lv_obj_set_style_bg_color(zahl, BERNSTEIN, 0);
            lv_obj_set_style_bg_opa(zahl, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(zahl, 8, 0);
            lv_obj_set_style_pad_hor(zahl, 4, 0);
            lv_label_set_text_fmt(zahl, "%d", sym.anzahl);
            lv_obj_align(zahl, LV_ALIGN_TOP_RIGHT, 8, -8);
        }
    }
    lv_obj_remove_flag(symbol_zeile_, LV_OBJ_FLAG_HIDDEN);
}
