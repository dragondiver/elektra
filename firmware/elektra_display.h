// elektra_display.h — rundfunk: der Bildschirm des Wohnzimmergeraets.
//
// Entwurf: https://claude.ai/code/artifact/14f2daee-7ce8-425f-8f2f-381b8c1bbce9
// Spec:    docs/superpowers/specs/2026-09-12-elektra-display-design.md (rundfunk)
//
// Ein Ring traegt den Zustand (Farbe und Bewegung reichen ueber die
// Zimmerbreite), im Schlaf steht der Mond in der Mitte, im Gespraech der
// Text. Erbt von SpiLcdDisplay, damit LVGL-Init, Panel und Lock bleiben —
// nur die Oberflaeche ist eine andere.
#ifndef ELEKTRA_DISPLAY_H
#define ELEKTRA_DISPLAY_H

#include "display/lcd_display.h"

#include <string>
#include <vector>

class ElektraDisplay : public SpiLcdDisplay {
public:
    enum class Zustand { Schlaeft, Verbindet, Hoert, Arbeitet, Antwortet, Meldung };

    // Haken fuer die Symbole — heute zeichnet SetzeSymbole nichts.
    struct Symbol {
        std::string art;      // "rueckruf" | "erinnerung"
        int anzahl = 0;
        int alter_min = 0;    // fuer die drei Formen: frisch, alt, ausgegraut
    };

    ElektraDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                   int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                   bool swap_xy);

    void SetupUI() override;
    void SetStatus(const char* status) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void ShowNotification(const std::string& notification, int duration_ms = 3000) override;
    void SetEmotion(const char* emotion) override;
    void SetChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void SetPowerSaveMode(bool on) override;
    void UpdateStatusBar(bool update_all = false) override;
    // LcdDisplay::SetTheme und SetPreviewImage greifen auf Objekte zu, die es
    // hier nicht gibt (container_, emoji_label_ ...). Wer sie nicht ueberschreibt,
    // stuerzt beim Laden der Assets ab (LoadProhibited, 12.09.2026).
    void SetTheme(Theme* theme) override;
    void SetPreviewImage(std::unique_ptr<LvglImage> image) override;

    void SetzeZustand(Zustand zustand);
    const char* SymbolAn(int x, int y) const;          // Art des getroffenen Symbols oder nullptr
    void SetzeSymbole(const std::vector<Symbol>& symbole);
    void MondDemo(bool an);      // Mondmonat im Zeitraffer durchlaufen (Abnahme)

private:
    void ZeichneRing();          // Farbe/Animation nach zustand_
    void ZeichneMitte();         // Mond oder Text nach zustand_
    void ZeichneMond();          // Phase aus dem Datum
    void ZeichneAkku();          // Tankanzeige unter 30 %

    Zustand zustand_ = Zustand::Schlaeft;
    lv_obj_t* ring_ = nullptr;           // lv_arc, voller Kreis
    lv_obj_t* akku_ring_ = nullptr;      // lv_arc, Tankanzeige
    lv_obj_t* mond_ = nullptr;           // lv_canvas, pixelweise gezeichnet
    lv_draw_buf_t* mond_buf_ = nullptr;
    int mond_tag_ = -1;                  // Kalendertag des letzten Mondbilds
    double mond_demo_alter_ = -1;        // >= 0: Demo laeuft, Mondalter in Tagen
    lv_timer_t* mond_demo_timer_ = nullptr;
    lv_timer_t* meldung_timer_ = nullptr;    // Rueckweg in den Schlaf nach einer Meldung
    lv_obj_t* zustand_label_ = nullptr;  // "HÖRT ZU" etc.
    lv_obj_t* satz_label_ = nullptr;     // Gehoertes / Gesagtes
    lv_obj_t* fuss_label_ = nullptr;     // "Elektra · oder tippen", "22 %"
    lv_anim_t ring_anim_;
    bool ring_anim_laeuft_ = false;
    int akku_prozent_ = 100;
    std::vector<Symbol> symbole_;
    lv_obj_t* symbol_zeile_ = nullptr;   // Kacheln 46 px vom oberen Rand
    void ZeichneSymbole();
};

#endif
