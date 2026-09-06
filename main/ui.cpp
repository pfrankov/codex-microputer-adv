#include "ui.h"

#include <M5Unified.hpp>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "audio.h"
#include "display_fade.h"
#include "firmware.h"
#include "lamp.h"
#include "model.h"
#include "motion.h"
#include "status_animation.h"
#include "status_timing.h"
#include "theme.h"
#include "micro5_digits.h"

using namespace theme;

namespace ui {
namespace {

M5Canvas canvas(&M5.Display);
bool canvas_ready = false;

// ---------------------------------------------------------------- frame clock
constexpr uint32_t kFrameBudgetMs = 28;   // ~35 fps ceiling
uint32_t last_frame_ms = 0;
bool     dirty         = true;

// ------------------------------------------------------------- screen machine
Screen current  = Screen::Boot;
Screen previous = Screen::Deck;
motion::Ramp transition;   // 0 -> 1 while the new screen slides in
int  transition_dir = 1;   // +1 push (new from right), -1 pop (new from left)

// Layer offset applied by every primitive, so a whole screen can be translated
// during a transition without each draw call knowing about it.
int layer_dx = 0;

// ---------------------------------------------------------------- gesture clock
// Seconds since the selection last changed. Every moving part on the deck reads
// its own progress off this one value, which is what keeps them a single
// gesture instead of four independent animations.
float since_select = 99.f;   // cursor + title
float since_status = 99.f;   // status band only
int   select_dir   = 1;
int   prev_selected = -1;   // column the cursor is travelling from
char  prev_index[4]  = "01";
char  curr_index[4]  = "01";
char  prev_title[model::kTitleMax] = {};
model::Status prev_status = model::Status::Idle;
model::Status curr_status = model::Status::Idle;

// ------------------------------------------------------------------ deck anim
motion::Spring scroll;      // pixels of list content scrolled off the top
motion::Spring highlight;   // y of the selection pill in content space
motion::Spring marquee;     // horizontal offset of an overlong selected title
motion::Spring selection_amount[kCellCount]; // 0 resting, 1 selected
motion::Spring voice_amount;                 // selected cell -> full panel
bool voice_target = false;
int  voice_slot = 0;
motion::Spring composer_control_amount;      // selected cell -> host control surface
bool composer_control_target = false;
float composer_control_idle = 99.f;
// Set once the session has become something: a click was made, or the host lit
// a lamp in answer. Until then the page is only the evidence that a dial moved.
bool composer_control_engaged = false;
float composer_control_step_age = 99.f;
int composer_control_step_dir = 0;
uint32_t composer_control_closed_at_ms = 0;
uint32_t composer_control_suppressed_until_ms = 0;
bool composer_control_open_sound_pending = false;
// What the host is actually showing on Micro's six lamps while its surface is
// open. This frame used to be parsed, recognised as a picker preview and then
// dropped on the floor: the device knew the host had repainted its lights and
// showed a generic panel anyway. It is the only truthful thing the device has
// about a surface whose semantics it deliberately does not guess, so it is now
// mirrored -- colour, level and which lamp is lit.
constexpr int kLampCount = 6;
uint16_t composer_lamp_colour[kLampCount] = {};
motion::Spring composer_lamp_level[kLampCount];
motion::Spring composer_lamp_marker;
bool  composer_lamp_valid = false;
int   composer_lamp_lit = -1;
float composer_lamp_age = 99.f;
// The dial itself. Detents accumulate into an angle the knob springs towards,
// so a fast series of clicks is one continuous rotation rather than a queue of
// jumps -- the same reason every marker on the device is a spring.
motion::Spring composer_knob_angle;
constexpr float kDetentRadians = 0.5236f;   // 30 degrees: twelve flutes, one per detent
// ------------------------------------------------------------ the stick page
// Same instrument page as the dial, for the four arrow keys. It fixes on
// nothing -- an impulse is sent and forgotten, the host reports no state back
// -- so unlike the dial page it always closes itself three seconds after the
// last press. There is no confirm and nothing to engage.
motion::Spring stick_control_amount;
bool  stick_control_target = false;
float stick_control_idle = 99.f;
float stick_step_age = 99.f;
int   stick_step_dir = -1;          // 0 right, 1 down, 2 left, 3 up
motion::Spring stick_lean_x;
motion::Spring stick_lean_y;
float stick_throw_hold = -1.f;   // >=0 while the cap is still on its way out
bool  stick_open_sound_pending = false;
constexpr float kStickIdleClose = 3.f;

float marquee_hold = 0.f;
int   marquee_dir  = 1;

// ---------------------------------------------------------------------- toast
char  toast_title[32]  = {};
char  toast_detail[40] = {};
uint16_t toast_accent  = kAccent;
motion::Ramp toast_in;
float toast_life = 0.f;     // seconds remaining before it retracts
bool  toast_out  = false;
bool pairing_pin_active = false;
uint32_t pairing_pin = 123456;

// --------------------------------------------------------------------- boot
motion::Ramp boot_ramp;

// -------------------------------------------------------------------- misc
float clock_phase = 0.f;    // free-running, drives breathing indicators
// Freeze underlying pulses while a status owns the panel. An uncovered edge
// changing beneath the expanding surface reads as display flicker.
bool status_animation_active = false;
bool release_status_freeze_after_frame = false;
int   settings_row = 0;
constexpr int kSettingsRows  = static_cast<int>(SettingsRow::Count);
constexpr int kSettingsPitch = 22;
constexpr int kSettingsRowH  = 20;
// Every local list moves the same way: one plate travels, the rows stay put.
// Sharing one spring shape across the menus is what makes them read as pages of
// one instrument rather than as three separately built screens.
motion::Spring settings_marker;
motion::Spring settings_level;   // volume meter position, in segments
int debug_settings_row = 0;
constexpr int kDebugSettingsRows = static_cast<int>(DebugSettingsRow::Count);
constexpr int kPreviewsRows = static_cast<int>(PreviewsRow::Count);
int previews_row = 0;
motion::Spring previews_marker;
constexpr int kDebugSettingsPitch = 15;
constexpr int kDebugSettingsRowH  = 14;
motion::Spring debug_settings_marker;
DeveloperPreview developer_preview = DeveloperPreview::None;
int debug_row = 0;
constexpr int kDebugRows = 7;
constexpr int kDebugPitch = 12;
constexpr int kDebugRowH  = 11;
motion::Spring debug_marker;
constexpr int kChimeCols  = 5;
constexpr int kChimeGap   = 2;
constexpr int kChimeLeft  = 6;
constexpr int kChimeTop   = 28;
constexpr int kChimeCellW = 44;
constexpr int kChimeCellH = 36;
motion::Spring chime_marker_x;
motion::Spring chime_marker_y;
// Per-chip flap timers for the bottom rail, so a setting change is visible
// where the setting actually lives instead of only as a toast.
float setting_flash[3] = {9.f, 9.f, 9.f};

// ------------------------------------------------------------- display power
uint32_t last_activity_ms = 0;
uint8_t  power_state = 0;   // 0 awake, 1 dimmed, 2 backlight off
// Two idle marks, each reached by a short ramp rather than a jump: the levels
// are what the user asked for, the ramp is what stops them reading as a fault.
constexpr uint32_t kDimAfterMs   = 15000;
constexpr uint32_t kDimHoldMs    = 180000;
constexpr uint32_t kDarkAfterMs  = kDimAfterMs + kDimHoldMs;
constexpr uint8_t  kBrightFull   = 110;
constexpr uint8_t  kBrightDim    = 11;    // ~10 percent
constexpr float    kBrightSlewPerSecond = 260.f;
constexpr uint32_t kHostOffDebounceMs = 750;
float   brightness_now    = kBrightFull;  // animated value
uint8_t applied_brightness = kBrightFull;
uint8_t colour_level = 255;

// ============================================================ draw primitives
inline void fill_rect(int x, int y, int w, int h, uint16_t c)
{
    canvas.fillRect(x + layer_dx, y, w, h, c);
}
inline void fill_round(int x, int y, int w, int h, int r, uint16_t c)
{
    canvas.fillRoundRect(x + layer_dx, y, w, h, r, c);
}
inline void draw_round(int x, int y, int w, int h, int r, uint16_t c)
{
    canvas.drawRoundRect(x + layer_dx, y, w, h, r, c);
}
inline void hline(int x, int y, int w, uint16_t c) { canvas.drawFastHLine(x + layer_dx, y, w, c); }
inline void vline(int x, int y, int h, uint16_t c) { canvas.drawFastVLine(x + layer_dx, y, h, c); }
inline void fill_circle(int x, int y, int r, uint16_t c) { canvas.fillCircle(x + layer_dx, y, r, c); }
inline void fill_tri(int x1, int y1, int x2, int y2, int x3, int y3, uint16_t c)
{
    canvas.fillTriangle(x1 + layer_dx, y1, x2 + layer_dx, y2, x3 + layer_dx, y3, c);
}

inline void text_at(const char* s, int x, int y) { canvas.drawString(s, x + layer_dx, y); }
inline void clip(int x, int y, int w, int h) { canvas.setClipRect(x + layer_dx, y, w, h); }
inline void unclip() { canvas.clearClipRect(); }

// Compact system faces handle labels; the six primary numerals use the
// generated neutral Inter atlas.
inline void font_system(int size = 1)
{
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(size);
}
// Font2 is a drawn 16 px face. AsciiFont8x16 carries serifs on 1, I and M, and
// Font0 doubled is just a magnified 6x8 grid -- both read as terminal type
// rather than as designed numerals.
inline void font_display()
{
    canvas.setFont(&fonts::Font2);
    canvas.setTextSize(1);
}
// UTF-8 face for host-supplied content (task titles are frequently Cyrillic).
inline void font_body(bool large = false)
{
    canvas.setFont(large ? &fonts::efontCN_14 : &fonts::efontCN_12);
    canvas.setTextSize(1);
}

// Letter-spaced caps. Font0 has no tracking of its own, and set solid it reads
// like a service screen; opening it up is what makes a label look designed.
int tracked_width(const char* text, int spacing, int size = 1)
{
    const int count = static_cast<int>(std::strlen(text));
    if (count == 0) return 0;
    return count * 6 * size + (count - 1) * spacing;
}

void draw_tracked(const char* text, int x, int y, int spacing, uint16_t color,
                  uint16_t background, int size = 1)
{
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(size);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(color, background);
    char glyph[2] = {0, 0};
    for (const char* p = text; *p; ++p) {
        glyph[0] = *p;
        canvas.drawString(glyph, x + layer_dx, y);
        x += 6 * size + spacing;
    }
}

void draw_tracked_transparent(const char* text, int x, int y, int spacing,
                              uint16_t color, int size = 1)
{
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(size);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(color);
    char glyph[2] = {0, 0};
    for (const char* p = text; *p; ++p) {
        glyph[0] = *p;
        canvas.drawString(glyph, x + layer_dx, y);
        x += 6 * size + spacing;
    }
}

// Right-aligned variant, so status words can hang off the right margin.
void draw_tracked_right(const char* text, int right, int y, int spacing,
                        uint16_t color, uint16_t background, int size = 1)
{
    draw_tracked(text, right - tracked_width(text, spacing, size), y, spacing,
                 color, background, size);
}

// One chip of the bottom rail, with the split-flap used elsewhere for values
// that change under you.
void draw_chip(const char* text, int x, int y, uint16_t colour, float flash)
{
    const float p = motion::clamp01(flash / motion::kFast);
    if (p >= 1.f) { draw_tracked(text, x, y, 2, colour, kPaper); return; }
    const bool opening = p >= 0.5f;
    const float half = opening ? (p - 0.5f) / 0.5f : 1.f - p / 0.5f;
    const int band = std::max(1, static_cast<int>(4 * half));
    clip(x, y + 4 - band, tracked_width(text, 2) + 2, band * 2);
    draw_tracked(text, x, y, 2, colour, kPaper);
    unclip();
}

// A level on this device is counted, not swept. Discrete blocks keep the same
// square vocabulary as the six task towers, stay readable at the 10 % dim
// level, and say how many steps the control actually has.
constexpr int kSegW   = 5;
constexpr int kSegGap = 2;
constexpr int kSegH   = 9;

int segment_width(int count)
{
    return count <= 0 ? 0 : count * kSegW + (count - 1) * kSegGap;
}

// `level` is in segments and is deliberately fractional: the block currently
// arriving mixes in instead of popping, which is what makes a stepped meter
// read as one travelling value rather than ten independent lamps.
void draw_segment_meter(int right, int y, int count, float level,
                        uint16_t on, uint16_t seat)
{
    int x = right - segment_width(count);
    for (int i = 0; i < count; ++i, x += kSegW + kSegGap) {
        const float fill = motion::clamp01(level - static_cast<float>(i));
        if (fill <= 0.f) {
            // An empty step keeps a seat rather than disappearing, so the
            // meter's length always states the full range.
            fill_rect(x, y + kSegH - 2, kSegW, 2, seat);
            continue;
        }
        fill_rect(x, y, kSegW, kSegH,
                  mix(seat, on, static_cast<uint8_t>(fill * 255.f)));
    }
}

// One of N rather than N of N: the unselected positions stay as seats so the
// control shows how many channels exist while only one is live.
void draw_segment_selector(int right, int y, int count, int active,
                           uint16_t on, uint16_t seat)
{
    int x = right - segment_width(count);
    for (int i = 0; i < count; ++i, x += kSegW + kSegGap) {
        if (i == active) fill_rect(x, y, kSegW, kSegH, on);
        else             fill_rect(x, y + kSegH - 2, kSegW, 2, seat);
    }
}

// The focused row is not a differently coloured row: it is the same row seen
// through a plate. Callers draw their content twice -- once on paper, once in
// the plate's ink clipped to the plate -- so the inversion travels with the
// spring instead of jumping a whole row ahead of it.
template <typename Rows>
void draw_selection_plate(int x, int y, int w, int h, const Rows& rows)
{
    rows(false);
    fill_rect(x, y, w, h, kInk);
    clip(x, y, w, h);
    rows(true);
    unclip();
}

void draw_bottom_rail(const char* left, const char* mid, const char* right, const char* hint)
{
    const int y = kScreenH - kBotRailH;
    fill_rect(0, y, kScreenW, kBotRailH, kPaper);
    hline(kGutter, y, kScreenW - 2 * kGutter, kInk);

    int x = kGutter + 1;
    if (left)  { draw_chip(left, x, y + 6, kInk, setting_flash[0]);      x += tracked_width(left, 2) + 10; }
    if (mid)   { draw_chip(mid, x, y + 6, kInkSoft, setting_flash[1]);   x += tracked_width(mid, 2) + 10; }
    if (right) { draw_chip(right, x, y + 6, kInkSoft, setting_flash[2]); }
    if (hint)  { draw_tracked_right(hint, kScreenW - kGutter - 1, y + 6, 1, kInkSoft, kPaper); }
    canvas.setTextDatum(textdatum_t::top_left);
}

// ==================================================================== screens
// ============================================================== deck screen
// Two channels, never mixed: colour and motion belong to the host, the cursor
// bar belongs to the user. Everything drawn here is derivable from the six
// fields v.oai.thstatus actually sends, plus our own clock.
// The focus frame is sprung on both axes, because in a grid the selection can
// move diagonally and two independent springs are what make that read as one
// object travelling rather than two edges sliding.
// Per-cell press feedback and entrance stagger. Both are plain seconds-since
// counters: one value per cell, read by the draw pass, advanced by service().
float cell_press[kCellCount] = {9.f, 9.f, 9.f, 9.f, 9.f, 9.f};
float deck_entrance = 9.f;

constexpr float kPressTime    = 0.22f;
constexpr float kStaggerStep  = 0.04f;   // 40 ms per cell
constexpr float kEntranceTime = 0.34f;

inline int cell_x(int index) { return kGridX + (index % kCols) * kCellPitchX; }
inline int cell_w(int index) { return index == kCellCount - 1 ? 40 : kCellW; }
inline int cell_y(int index) { return kGridY + (index / kCols) * kCellPitchY; }
inline bool link_down() { return model::state.link == model::Link::Offline; }

uint16_t cell_fill(const model::Task& t)
{
    switch (t.status) {
        case model::Status::NeedsInput: return kVerm;
        case model::Status::Error: return kInk;   // inversion, not a sixth hue
        case model::Status::Done:  return t.unseen_done ? kGreen : kViewed;
        case model::Status::Running: return kBlue;
        default: return kPale;
    }
}

uint16_t cell_digit_colour(const model::Task& t)
{
    if (t.status == model::Status::Error) return kVerm;
    if (t.status == model::Status::Done)  return t.unseen_done ? kInk : kIdleDigit;
    if (t.status == model::Status::Idle)  return kIdleDigit;
    return kPaper;
}

bool has_inactive_digit(const model::Task& t)
{
    return t.status == model::Status::Idle
        || (t.status == model::Status::Done && !t.unseen_done);
}

bool has_status_gradient(const model::Task& t)
{
    return t.status == model::Status::NeedsInput
        || (t.status == model::Status::Done && t.unseen_done);
}

// Active colours have a restrained vertical lift: the top catches a little
// light while the bottom keeps the semantic base colour. The contrast is low
// enough to read as material rather than decoration.
void fill_status_surface(int x, int y, int w, int h, uint16_t base,
                         uint8_t opacity, uint8_t press_glow, uint8_t gradient_tint)
{
    if (gradient_tint == 0) {
        uint16_t colour = mix(kPaper, base, opacity);
        if (press_glow) colour = mix(colour, kPaper, press_glow);
        fill_rect(x, y, w, h, colour);
        return;
    }
    for (int row = 0; row < h; ++row) {
        // 15 % tint at the top, almost none at the foot.
        int tint = gradient_tint
                 - ((gradient_tint - gradient_tint / 10) * row)
                   / std::max(1, h - 1);
        uint16_t colour = mix(base, kPaper, tint);
        colour = mix(kPaper, colour, opacity);
        if (press_glow) colour = mix(colour, kPaper, press_glow);
        hline(x, y + row, w, colour);
    }
}

// Micro's keys are towers: the number anchors the bottom instead of floating
// in the middle. The source mask is intentionally rendered below 1:1 here:
// at its native 32 px it leaves only two pixels beside a digit in a 36 px key,
// so neither the numeral nor its selection affordance has room to breathe.
// Half scale maps every 2x2 source block onto one display pixel. The previous
// 0.60 scale rounded alternating rows and columns differently, leaving small
// protrusions along diagonal and curved edges.
constexpr int kDigitRestBottom = 127;
constexpr int kDigitSelectedBottom = 114;
constexpr int kSelectionW = 30;
constexpr int kSelectionH = 8;
constexpr int kSelectionY = 122;

void draw_micro5_digit(int index, float x, float y, float scale, uint16_t fg)
{
    if (index < 0 || index >= 10) return;
    for (int row = 0; row < micro5_digits::kHeight; ++row) {
        int run = -1;
        for (int col = 0; col <= micro5_digits::kWidth; ++col) {
            bool on = false;
            if (col < micro5_digits::kWidth) {
                const uint8_t byte = micro5_digits::kGlyphs[0][index]
                    [row * micro5_digits::kStride + col / 8];
                on = (byte & (1 << (7 - col % 8))) != 0;
            }
            if (on && run < 0) run = col;
            if (!on && run >= 0) {
                const int px = static_cast<int>(x + run * scale + 0.5f);
                const int py = static_cast<int>(y + row * scale + 0.5f);
                const int pw = std::max(1, static_cast<int>((col - run) * scale + 0.5f));
                const int ph = std::max(1, static_cast<int>(scale + 0.5f));
                fill_rect(px, py, pw, ph, fg);
                run = -1;
            }
        }
    }
}

int micro5_pin_glyph(char digit)
{
    if (digit >= '1' && digit <= '6') return digit - '1';
    if (digit == '0') return 6;
    if (digit >= '7' && digit <= '9') return digit - '0';
    return -1;
}

void draw_cell_digit(int index, int x, int y, int w, uint16_t fg, float selected)
{
    const float eased = motion::ease_out_cubic(motion::clamp01(selected));
    // Only the selected numeral makes room for the plate. Every other numeral
    // stays on the lower baseline; reversing the spring returns it there.
    const float digit_bottom = motion::lerp(static_cast<float>(kDigitRestBottom),
                                            static_cast<float>(kDigitSelectedBottom), eased);

    const int visual_w = micro5_digits::kVisualWidth[0][index];
    const int visual_h = micro5_digits::kVisualHeight[0][index];
    const int visual_left = x + (w - visual_w) / 2;
    const int visual_top = static_cast<int>(digit_bottom + 0.5f) - visual_h;
    const int source_left = visual_left - micro5_digits::kLeft[0][index];
    const int source_top = visual_top - micro5_digits::kTop[0][index];
    draw_micro5_digit(index, source_left, source_top, 1.f, fg);
}

float announcement_selection_visibility(int index);

void draw_selection_block(int index, int x, int w, uint16_t colour, float selected)
{
    const float eased = motion::ease_out_cubic(motion::clamp01(
        selected * announcement_selection_visibility(index)));
    if (eased <= 0.f) return;

    // The active index remains below the numeral. The whole square-edged plate
    // rises from beyond the physical bottom edge and returns there on deselect;
    // it never inflates in place or turns into a rounded control.
    const int block_x = x + (w - kSelectionW) / 2;
    const int sy = static_cast<int>(motion::lerp(static_cast<float>(kScreenH),
                                                static_cast<float>(kSelectionY), eased) + 0.5f);
    fill_rect(block_x, sy, kSelectionW, kSelectionH, colour);
}

void draw_cells()
{
    auto& s = model::state;
    const bool entering = deck_entrance < kEntranceTime + kStaggerStep * kCellCount;
    for (int i = 0; i < kCellCount; ++i) {
        // Entrance is opacity only. Geometry never jumps: these are controls,
        // not cards being dealt onto a table.
        float in = 1.f;
        if (entering) {
            in = motion::ease_out_cubic(
                motion::clamp01((deck_entrance - kStaggerStep * i) / kEntranceTime));
            if (in <= 0.f) continue;
        }
        const uint8_t opacity = static_cast<uint8_t>(in * 255.f);
        const int x = cell_x(i);
        const int y = cell_y(i);
        const int w = cell_w(i);
        const int h = kCellH;

        const bool bound = !link_down() && i < s.task_count && s.tasks[i].present;
        if (!bound) {
            // An unbound slot is an outline, an idle agent is a filled cell.
            // Drawing them the same would lie about whether the key does anything.
            const uint16_t edge = mix(kPaper, kPale, opacity);
            canvas.drawRect(x + layer_dx, y, w, h, edge);
            const float selected = selection_amount[i].x;
            const uint16_t idle_digit = mix(kIdleDigit, kIdleDigitSelected,
                static_cast<uint8_t>(motion::clamp01(selected) * 255.f));
            draw_selection_block(i, x, w, idle_digit, selected);
            draw_cell_digit(i, x, y, w, idle_digit, selected);
            continue;
        }
        const model::Task& t = s.tasks[i];
        uint16_t base = cell_fill(t);
        if (t.status == model::Status::Running && !status_animation_active) {
            // Three incommensurate rhythms make the flat colour feel active
            // and slightly twitchy without repeating as an obvious sine wave.
            // Slot-specific rates and offsets keep neighbouring agents apart.
            const float rate = 1.62f + 0.113f * i;
            const float phase = clock_phase * rate + i * 1.731f;
            const float slow = std::sin(phase);
            const float mid = std::sin(phase * 2.37f + i * 0.83f);
            const float quick = std::sin(phase * 5.11f + i * 2.19f);
            const float irregular = std::clamp(
                0.50f + slow * 0.27f + mid * 0.16f + quick * 0.07f,
                0.f, 1.f);
            const float pulse = motion::ease_in_out_cubic(irregular);
            base = mix(kBlue, kBlueLift,
                       static_cast<uint8_t>(28.f + pulse * 158.f));
        }
        uint8_t press_glow = 0;
        // Press feedback is a restrained material flash, never a size change.
        // A key answers on contact and then lets go: full value on the first
        // frame with an eased decay reads as a mechanical switch, where the
        // previous symmetric ramp read as a slow glow arriving late.
        if (cell_press[i] < kPressTime) {
            const float glow = 1.f - motion::ease_out_cubic(cell_press[i] / kPressTime);
            press_glow = static_cast<uint8_t>(glow * 54.f);
        }
        fill_status_surface(x, y, w, h, base, opacity, press_glow,
                            has_status_gradient(t) ? 38 : 0);
        if (t.status == model::Status::Error) {
            fill_rect(x + 8, y + 14, w - 16, 3, kVerm);
        }
        const uint16_t resting_fill = mix(kPaper, base, opacity);
        const float selected = selection_amount[i].x;
        const uint16_t target_digit = has_inactive_digit(t)
            ? mix(kIdleDigit, kIdleDigitSelected,
                  static_cast<uint8_t>(motion::clamp01(selected) * 255.f))
            : cell_digit_colour(t);
        const uint16_t digit = mix(resting_fill, target_digit, opacity);
        draw_selection_block(i, x, w, digit, selected);
        draw_cell_digit(i, x, y, w, digit, selected);
    }
}

void draw_deck()
{
    fill_rect(0, 0, kScreenW, kScreenH, kPaper);

    draw_cells();

    // Small paper-backed overlays stay readable over every task colour without
    // taking a rail away from the six full-height towers. Sound is silent when
    // enabled; only the exceptional muted state earns a label.
    if (model::state.sound_volume == 0) {
        fill_rect(2, 2, 39, 13, kPaper);
        draw_tracked("MUTE", 7, 5, 1, kVerm, kPaper);
    }

    // Normal connectivity stays silent. Weak BLE is an annunciator, not a
    // sticker: it is flush with the top and right edges and spans exactly two
    // task columns, so it lands on the same grid as the towers instead of
    // floating across the middle of two of them at an arbitrary offset. RSSI
    // hysteresis prevents flicker.
    //
    // It is built as an instrument panel, not as a warning label. A dark plate
    // with a word set in reverse is a sticker applied on top of the deck; an
    // inset of the same paper the interface is printed on, delimited by a
    // hairline and holding a short technical code and a level readout, is part
    // of the panel. So: bone field, one-pixel rule all the way round, the code
    // for what is weak at the left, and the level itself at the right, with
    // air between them rather than a filled bar.
    //
    // The readout is three equal dots, not an ascending staircase: equal marks
    // are a scale being read, a staircase is a picture of a signal. One dot
    // lit warm and two left in the tone of spent chrome says one of three
    // without a word for it, and the spent dots stay drawn -- a scale missing
    // two positions reads as a two-position scale.
    const bool weak_link = model::state.link == model::Link::Ble
                        && model::state.ble_signal_weak;
    if (weak_link) {
        const int x = kScreenW - kSignalStripW;
        fill_rect(x, 0, kSignalStripW, kSignalStripH, kPaper);
        fill_rect(x, 0, kSignalStripW, 1, kInk);
        fill_rect(x, kSignalStripH - 1, kSignalStripW, 1, kInk);
        fill_rect(x, 0, 1, kSignalStripH, kInk);
        draw_tracked("BLE", x + 8, 3, 1, kInk, kPaper);
        const uint16_t spent = mix(kPaper, kInk, 58);
        for (int dot = 0; dot < 3; ++dot) {
            fill_rect(x + 53 + dot * 7, 5, 4, 4, dot == 0 ? kVerm : spent);
        }
    }

    // Battery is exceptional chrome: stay silent during normal operation and
    // surface the exact value only when the user needs to act (< 15%). It steps
    // aside for the annunciator rather than overprinting it, and takes its
    // contrast from whichever tower it actually lands on, so it remains part of
    // the deck instead of becoming a floating card.
    if (model::state.battery >= 0 && model::state.battery < 15) {
        const int right = weak_link ? kScreenW - kSignalStripW - 5 : 225;
        model::Task indicator_fallback;
        const int under = std::clamp(right / kCellPitchX, 0, kCellCount - 1);
        const model::Task& indicator_task = model::state.task_count > under
            ? model::state.tasks[under] : indicator_fallback;
        char battery[6];
        std::snprintf(battery, sizeof(battery), "%d%%", model::state.battery);
        const uint16_t battery_colour = mix(cell_fill(indicator_task),
                                            cell_digit_colour(indicator_task), 144);
        canvas.setFont(&fonts::Font0);
        canvas.setTextSize(1);
        canvas.setTextDatum(textdatum_t::top_right);
        canvas.setTextColor(battery_colour);
        canvas.drawString(battery, right + layer_dx, 3);
    }

    canvas.setTextDatum(textdatum_t::top_left);
}

// ------------------------------------------------------------- voice takeover
// Voice is captured by Codex, not by this firmware. The selected agent tower
// therefore expands into the recording surface: the spatial continuity makes
// it unambiguous which chat owns the dictated text.
void draw_voice_takeover()
{
    const float amount = motion::ease_in_out_cubic(
        motion::clamp01(voice_amount.x));
    if (amount <= 0.f) return;

    const int slot = std::clamp(voice_slot, 0, kCellCount - 1);
    const model::Task* task = slot < model::state.task_count
        ? &model::state.tasks[slot] : nullptr;
    model::Task fallback;
    fallback.present = true;
    const model::Task& visual = task ? *task : fallback;
    const uint16_t source_colour = cell_fill(visual);
    const uint16_t base = mix(source_colour, kVoice,
        static_cast<uint8_t>(amount * 255.f));
    const uint16_t fg = kPaper;

    const int source_x = cell_x(slot);
    const int source_w = cell_w(slot);
    const int x0 = static_cast<int>(motion::lerp(source_x, 0.f, amount));
    const int x1 = static_cast<int>(motion::lerp(source_x + source_w,
                                                 static_cast<float>(kScreenW), amount));
    fill_status_surface(x0, 0, x1 - x0, kScreenH, base, 255, 0, 0);

    const float resting_scale = 1.f;
    const float resting_w = micro5_digits::kVisualWidth[0][slot];
    const float resting_h = micro5_digits::kVisualHeight[0][slot];
    const float resting_visual_x = source_x + (source_w - resting_w) / 2.f;
    const float resting_visual_y = kDigitSelectedBottom - resting_h;
    const float resting_source_x = resting_visual_x - micro5_digits::kLeft[0][slot];
    const float resting_source_y = resting_visual_y - micro5_digits::kTop[0][slot];

    const float voice_scale = 2.f;
    const float voice_visual_x = (kScreenW - micro5_digits::kVisualWidth[0][slot] * voice_scale) / 2.f;
    const float voice_visual_y = 14.f;
    const float voice_source_x = voice_visual_x - micro5_digits::kLeft[0][slot] * voice_scale;
    const float voice_source_y = voice_visual_y - micro5_digits::kTop[0][slot] * voice_scale;
    draw_micro5_digit(slot,
        motion::lerp(resting_source_x, voice_source_x, amount),
        motion::lerp(resting_source_y, voice_source_y, amount),
        motion::lerp(resting_scale, voice_scale, amount), fg);

    // Copy arrives only after the tower has clearly become the whole panel.
    const float copy = motion::ease_out_cubic((amount - 0.55f) / 0.45f);
    if (copy > 0.f) {
        const uint16_t text = mix(base, fg, static_cast<uint8_t>(copy * 255.f));
        const int recording_w = tracked_width("RECORDING", 3, 2);
        draw_tracked_transparent("RECORDING", (kScreenW - recording_w) / 2 + 8,
                                 78, 3, text, 2);

        // A filled core and breathing ring read as a live capture indicator,
        // but never fully disappear and therefore never resemble disconnect.
        const float live = motion::pulse(clock_phase * 1.15f);
        const int dot_x = (kScreenW - recording_w) / 2 - 3;
        const int dot_y = 86;
        fill_circle(dot_x, dot_y, 4, text);
        const uint16_t ring = mix(base, kPaper,
            static_cast<uint8_t>((0.25f + live * 0.55f) * copy * 255.f));
        canvas.drawCircle(dot_x + layer_dx, dot_y, 7 + static_cast<int>(live * 2.f), ring);

        const char* hold = "HOLD G0";
        draw_tracked_transparent(hold, (kScreenW - tracked_width(hold, 2)) / 2,
                                 105, 2, text);
        const char* release = "RELEASE TO FINISH";
        draw_tracked_transparent(release,
            (kScreenW - tracked_width(release, 1)) / 2, 122, 1, text);
    }
    canvas.setTextDatum(textdatum_t::top_left);
}

// A filled ellipse, used only by the instrument on the control page.
// LovyanGFX has fillEllipse but not through the layer offset, and the knob
// needs the same scanline shape for its cap, its base and its light ring.
void fill_ellipse(int cx, int cy, int rx, int ry, uint16_t c)
{
    for (int dy = -ry; dy <= ry; ++dy) {
        const float t = static_cast<float>(dy) / static_cast<float>(ry);
        const int half = static_cast<int>(rx * std::sqrt(std::max(0.f, 1.f - t * t)));
        if (half <= 0) continue;
        hline(cx - half, cy + dy, half * 2 + 1, c);
    }
}

// The dial, drawn as the object that is actually under the user's fingers.
// Codex Micro is Work Louder hardware: the encoder is a straight machined
// cylinder with a flat top, fine knurling around the wall and a light ring at
// its base. So the knurling belongs on the wall, not radiating across the cap,
// the silhouette has no taper, and the ring is not decoration -- it carries the
// host's own lamp colour, which is the only thing the device truthfully knows
// about the surface being adjusted.
constexpr int kKnobRx   = 25;
constexpr int kKnobRy   = 10;
constexpr int kKnobWall = 27;
constexpr int kOutline  = 2;   // the instrument is drawn on the surface, and
                               // reads as applied to it only if the keyline is
                               // heavy enough to survive the pixel grid

void draw_knob(int cx, int cy, float angle, uint16_t base, uint16_t glow,
               float glow_level, float copy)
{
    const uint8_t ink = static_cast<uint8_t>(copy * 255.f);
    const uint16_t wall = mix(base, kInk,  static_cast<uint8_t>(copy * 46.f));
    const uint16_t cap  = mix(base, kPaper, static_cast<uint8_t>(ink * 0.22f));
    const uint16_t knur = mix(base, kPaper, static_cast<uint8_t>(ink * 0.44f));
    const uint16_t edge = mix(base, kPaper, static_cast<uint8_t>(ink * 0.58f));
    const uint16_t mark = mix(base, kPaper, ink);

    const int ring_y = cy + kKnobWall;

    // Light ring first, then the keyline, then the body. What survives is a
    // halo of the host's own lamp colour bleeding out from under the base,
    // which is how it behaves in aluminium.
    const uint16_t lit = mix(base, glow,
        static_cast<uint8_t>(copy * (0.30f + 0.70f * motion::clamp01(glow_level)) * 255.f));
    fill_ellipse(cx, ring_y + 2, kKnobRx + kOutline + 3, kKnobRy + 2,
                 mix(base, lit, 170));
    fill_ellipse(cx, ring_y + 1, kKnobRx + kOutline + 1, kKnobRy + 1, lit);

    // The keyline, drawn as an oversized silhouette the body is then sunk into.
    fill_ellipse(cx, cy, kKnobRx + kOutline, kKnobRy + kOutline, mark);
    fill_rect(cx - kKnobRx - kOutline, cy, (kKnobRx + kOutline) * 2 + 1,
              kKnobWall, mark);
    fill_ellipse(cx, ring_y, kKnobRx + kOutline, kKnobRy + kOutline, mark);

    // Body: bottom cap, straight wall, flat top. No taper, no shading ramp.
    fill_ellipse(cx, ring_y, kKnobRx, kKnobRy, wall);
    fill_rect(cx - kKnobRx, cy, kKnobRx * 2 + 1, kKnobWall, wall);
    // The rim is chamfered, not cut square: a mid band between the wall and the
    // flat of the cap. Machined aluminium always breaks that edge, and without
    // it the top reads as a lid dropped onto a tube.
    fill_ellipse(cx, cy, kKnobRx, kKnobRy, edge);
    fill_ellipse(cx, cy - 1, kKnobRx - 3, kKnobRy - 2, cap);

    // Knurling: twenty-four flutes around the wall, so one 30-degree detent
    // walks the pattern by exactly two -- unmistakable, and never a blur.
    for (int k = 0; k < 24; ++k) {
        const float a = angle + static_cast<float>(k) * (kDetentRadians * 0.5f);
        const float sa = std::sin(a);
        if (sa <= 0.08f) continue;          // only the near half of the wall
        const int x = cx + static_cast<int>(kKnobRx * std::cos(a));
        const int y = cy + static_cast<int>(kKnobRy * sa);
        vline(x, y, kKnobWall - static_cast<int>((1.f - sa) * 4.f), knur);
    }

    // No index mark. The knurling already carries the angle -- it walks two
    // flutes per detent -- and a notch through the wall plus a bar on the cap
    // only added two marks competing to be read as the value, which is a thing
    // this page deliberately never claims to know.
    (void)mark;
}

// ------------------------------------------------------------- the stick
// Micro's stick is planar: a low rubber cap that slides across a recess in the
// plate, not a ball on a shaft that pivots. Drawn as a tilting lever it was a
// different object entirely -- too tall, and moving the wrong way. It is drawn
// in the dial's family -- same white keyline, same tone ladder -- because it is
// the same kind of page about a different control, and two dialects of one
// idea would just look like two unrelated screens.
constexpr int kGateRx  = 37;   // the recess milled into the plate: wide
constexpr int kGateRy  = 14;   // enough that a full throw stays inside it
constexpr int kCapRx   = 18;   // the rubber cap that slides across it
constexpr int kCapRy   = 8;
constexpr int kCapWall = 9;

void draw_stick(int cx, int cy, float lx, float ly, uint16_t base, float copy)
{
    const uint8_t ink = static_cast<uint8_t>(copy * 255.f);
    const uint16_t line  = mix(base, kPaper, ink);
    const uint16_t plate = mix(base, kPaper, static_cast<uint8_t>(ink * 0.26f));
    const uint16_t well  = mix(base, kPaper, static_cast<uint8_t>(ink * 0.10f));
    const uint16_t wall  = mix(base, kPaper, static_cast<uint8_t>(ink * 0.22f));
    const uint16_t face  = mix(base, kPaper, static_cast<uint8_t>(ink * 0.60f));
    const uint16_t dish  = mix(base, kPaper, static_cast<uint8_t>(ink * 0.50f));

    // The recess. Keyline, plate lip, then the shadowed floor the cap rides on.
    fill_ellipse(cx, cy + 2, kGateRx + kOutline, kGateRy + kOutline, line);
    fill_ellipse(cx, cy + 2, kGateRx, kGateRy, plate);
    fill_ellipse(cx, cy + 1, kGateRx - 4, kGateRy - 3, well);

    // The cap slides; it does not lean. Screen x and y travel in the ratio of
    // the ellipse, so the motion stays in the plane of the plate.
    // Its base rides on the plate, so the cap stands proud of the recess
    // rather than down inside it -- a rubber nub you can get a thumb on.
    const int px = cx + static_cast<int>(std::lround(lx * 16.f));
    const int py = cy + 1 - kCapWall + static_cast<int>(std::lround(ly * 8.f));

    for (int t = kCapWall; t >= 0; --t)
        fill_ellipse(px, py + t, kCapRx + kOutline, kCapRy + kOutline, line);
    for (int t = kCapWall; t >= 1; --t)
        fill_ellipse(px, py + t, kCapRx, kCapRy, wall);
    fill_ellipse(px, py, kCapRx, kCapRy, face);
    // Rubber, so the top is dished rather than flat, and the dish is a tone
    // with a lit far rim -- the same way the keycap read as scooped.
    fill_ellipse(px, py + 1, kCapRx - 5, kCapRy - 3, dish);
    fill_ellipse(px, py - 1, kCapRx - 7, kCapRy - 4, mix(dish, kPaper,
                 static_cast<uint8_t>(copy * 40.f)));
}

// A chevron pointing away from the instrument, on any of the four sides.
void draw_chevron(int ax, int ay, int dx, int dy, uint16_t c)
{
    for (int k = 0; k < 6; ++k) {
        if (dx != 0) vline(ax + dx * k, ay - 5 + k / 2, 11 - k, c);
        else         hline(ax - 5 + k / 2, ay + dy * k, 11 - k, c);
    }
}

void draw_stick_control_takeover()
{
    const float amount = motion::ease_in_out_cubic(
        motion::clamp01(stick_control_amount.x));
    if (amount <= 0.f) return;

    const int slot = std::clamp(model::state.selected, 0, kCellCount - 1);
    const model::Task* task = slot < model::state.task_count
        ? &model::state.tasks[slot] : nullptr;
    model::Task fallback;
    fallback.present = true;
    const uint16_t source = cell_fill(task ? *task : fallback);
    const uint16_t base = mix(source, kBlue, static_cast<uint8_t>(amount * 255.f));
    const int source_x = cell_x(slot);
    const int source_w = cell_w(slot);
    const int x0 = static_cast<int>(motion::lerp(source_x, 0.f, amount));
    const int x1 = static_cast<int>(motion::lerp(source_x + source_w,
                                                 static_cast<float>(kScreenW), amount));
    fill_status_surface(x0, 0, x1 - x0, kScreenH, base, 255, 0, 30);

    const float resting_w = micro5_digits::kVisualWidth[0][slot];
    const float resting_h = micro5_digits::kVisualHeight[0][slot];
    const float resting_visual_x = source_x + (source_w - resting_w) / 2.f;
    const float resting_visual_y = kDigitSelectedBottom - resting_h;
    draw_micro5_digit(slot,
        motion::lerp(resting_visual_x - micro5_digits::kLeft[0][slot],
                     10.f - micro5_digits::kLeft[0][slot], amount),
        motion::lerp(resting_visual_y - micro5_digits::kTop[0][slot],
                     7.f - micro5_digits::kTop[0][slot], amount),
        1.f, kPaper);

    const float copy = motion::ease_out_cubic((amount - 0.55f) / 0.45f);
    if (copy <= 0.f) return;
    const uint16_t text  = mix(base, kPaper, static_cast<uint8_t>(copy * 255.f));
    const uint16_t faint = mix(base, kPaper, static_cast<uint8_t>(copy * 78.f));

    const int cx = kScreenW / 2;
    const int cy = 70;
    draw_stick(cx, cy, stick_lean_x.x, stick_lean_y.x, base, copy);

    const float step = motion::clamp01(1.f - stick_step_age / 0.45f);
    static const int8_t dirs[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    for (int d = 0; d < 4; ++d) {
        const bool hot = stick_step_dir == d && step > 0.f;
        const uint16_t c = hot
            ? mix(base, kPaper, static_cast<uint8_t>(
                  copy * std::min(255.f, 70.f + step * 185.f)))
            : faint;
        const int dx = dirs[d][0], dy = dirs[d][1];
        draw_chevron(cx + dx * 56, cy - 4 + dy * 34, dx, dy, c);
    }

    draw_tracked_transparent("STICK", (kScreenW - tracked_width("STICK", 2)) / 2,
                             118, 2, text);
    draw_tracked_right("ESC", kScreenW - 10, 8, 1, faint, base);
    // No confirm mark here. There is nothing to confirm: an arrow is an
    // impulse the host consumes, and this page reports it, it does not hold it.
}

// ---------------------------------------------------- composer control preview
// Codex Micro expresses composer controls through temporary light previews. The
// page is the instrument, not a label for it: the dial and the key are drawn as
// objects, the six agent lamps are mirrored where the six task cells were, and
// nothing is named. The same dial can own navigation, scrolling, reasoning or a
// custom action, and the device does not guess which.
void draw_composer_control_takeover()
{
    const float amount = motion::ease_in_out_cubic(
        motion::clamp01(composer_control_amount.x));
    if (amount <= 0.f) return;

    const int slot = std::clamp(model::state.selected, 0, kCellCount - 1);
    const model::Task* task = slot < model::state.task_count
        ? &model::state.tasks[slot] : nullptr;
    model::Task fallback;
    fallback.present = true;
    const uint16_t source = cell_fill(task ? *task : fallback);
    const uint16_t base = mix(source, kBlue, static_cast<uint8_t>(amount * 255.f));
    const int source_x = cell_x(slot);
    const int source_w = cell_w(slot);
    const int x0 = static_cast<int>(motion::lerp(source_x, 0.f, amount));
    const int x1 = static_cast<int>(motion::lerp(source_x + source_w,
                                                 static_cast<float>(kScreenW), amount));
    fill_status_surface(x0, 0, x1 - x0, kScreenH, base, 255, 0, 30);

    // The numeral retreats to the corner at resting scale: it is the anchor
    // saying which chat this dial is driving, not the subject of the page.
    const float resting_w = micro5_digits::kVisualWidth[0][slot];
    const float resting_h = micro5_digits::kVisualHeight[0][slot];
    const float resting_visual_x = source_x + (source_w - resting_w) / 2.f;
    const float resting_visual_y = kDigitSelectedBottom - resting_h;
    draw_micro5_digit(slot,
        motion::lerp(resting_visual_x - micro5_digits::kLeft[0][slot],
                     10.f - micro5_digits::kLeft[0][slot], amount),
        motion::lerp(resting_visual_y - micro5_digits::kTop[0][slot],
                     7.f - micro5_digits::kTop[0][slot], amount),
        1.f, kPaper);

    const float copy = motion::ease_out_cubic((amount - 0.55f) / 0.45f);
    if (copy <= 0.f) return;
    const uint16_t text  = mix(base, kPaper, static_cast<uint8_t>(copy * 255.f));
    const uint16_t faint = mix(base, kPaper, static_cast<uint8_t>(copy * 78.f));

    // The instrument. A detent leans the knob a little in the direction it
    // turned and lights that side's chevron for the length of the detent.
    const float step = motion::clamp01(1.f - composer_control_step_age / 0.34f);
    const int lean = static_cast<int>(composer_control_step_dir * step * 2.f);
    const int lit_lamp = composer_lamp_lit >= 0 ? composer_lamp_lit : -1;
    const uint16_t glow = (composer_lamp_valid && lit_lamp >= 0)
        ? composer_lamp_colour[lit_lamp] : kPaper;
    const float glow_level = (composer_lamp_valid && lit_lamp >= 0)
        ? composer_lamp_level[lit_lamp].x : 0.f;
    draw_knob(kScreenW / 2 + lean, 48, composer_knob_angle.x, base, glow, glow_level, copy);

    // The two detent directions stay chevrons. Drawn as keycaps they turned a
    // page with one control on it into a page with three, and the dial stopped
    // being the subject of it.
    for (int side = 0; side < 2; ++side) {
        const int dir = side == 0 ? -1 : 1;
        const bool hot = composer_control_step_dir == dir && step > 0.f;
        const uint16_t c = hot
            ? mix(base, kPaper, static_cast<uint8_t>(
                  copy * std::min(255.f, 70.f + step * 185.f)))
            : faint;
        draw_chevron(kScreenW / 2 + dir * 44, 58, dir, 0, c);
    }

    draw_tracked_transparent("TURN", (kScreenW - tracked_width("TURN", 2)) / 2, 100, 2, text);
    // The two keys that act on the dial, set as hints rather than as objects.
    // Drawing the confirm as a keycap made a second thing to look at on a page
    // whose whole subject is the dial. Esc is the way out of every other
    // surface on the device, so it has to be the way out of this one too, and
    // has to say so.
    draw_tracked_right("ESC", kScreenW - 10, 8, 1, faint, base);
    // Confirm is the return arrow itself rather than its name: at this size the
    // mark is shorter than the word and it is the same mark that is printed on
    // the key. It sits in the far corner, diagonally opposite Esc, so the two
    // ways out of the page bracket it instead of stacking into a list.
    {
        const int ax = kScreenW - 18, ay = kScreenH - 16;
        vline(ax + 8, ay, 6, faint);
        hline(ax, ay + 5, 9, faint);
        fill_rect(ax + 1, ay + 4, 1, 1, faint);
        fill_rect(ax + 1, ay + 6, 1, 1, faint);
        fill_rect(ax + 2, ay + 3, 1, 1, faint);
        fill_rect(ax + 2, ay + 7, 1, 1, faint);
    }

    // The host's lamps are not drawn. In this mode their pattern is the host's
    // own UI state, not six agent statuses, so a row of them under the
    // instrument said nothing a reader could use. What the frame is good for is
    // the colour, and that is where it goes: into the knob's base ring, which
    // is the object being turned.
}

// ---------------------------------------------------------------- announcement
// A status change is the one event worth interrupting for, so it takes the whole
// screen: the slot's colour floods out from its own cell, the numeral arrives at
// 48 px, and the word says what changed. Everything about it is spatial -- the
// flood starts where the cell is, so the panel never just cuts to a new image.
int   announce_slot_index = -1;
model::Status announce_status = model::Status::Idle;
bool announce_unseen = false;
bool announce_fade_to_viewed = false;
float announce_age = 99.f;
bool announce_audio_armed = false;

constexpr float kAnnounceColour = status_timing::colour;
constexpr float kAnnounceRailOut = status_timing::rail_out;
constexpr float kAnnounceExpand = status_timing::expand;
constexpr float kAnnounceCentre = status_timing::centre;
constexpr float kAnnounceHold = status_timing::hold;
constexpr float kAnnounceReturn = status_timing::returning;
constexpr float kAnnounceCollapse = status_timing::collapse;
constexpr float kAnnounceVisualLife = status_timing::visual_life;
constexpr float kAnnounceLife = status_timing::life;

uint16_t status_surface(model::Status status, bool unseen)
{
    switch (status) {
        case model::Status::NeedsInput: return kVerm;
        case model::Status::Error:      return kInk;
        case model::Status::Running:    return kBlue;
        case model::Status::Done:       return unseen ? kGreen : kViewed;
        default:                        return kGrey;
    }
}

uint16_t status_foreground(model::Status status)
{
    if (status == model::Status::Done || status == model::Status::Idle) return kInk;
    if (status == model::Status::Error) return kVerm;
    return kPaper;
}

bool announcing() { return announce_slot_index >= 0 && announce_age < kAnnounceLife; }

void cancel_status_announcements_internal()
{
    announce_slot_index = -1;
    announce_age = kAnnounceLife;
    announce_audio_armed = false;
    announce_fade_to_viewed = false;
    status_animation_active = false;
    release_status_freeze_after_frame = false;
    dirty = true;
}

audio::Cue status_cue(model::Status status)
{
    switch (status) {
        case model::Status::Running:    return audio::Cue::Running;
        case model::Status::NeedsInput: return audio::Cue::Attention;
        case model::Status::Done:       return audio::Cue::Done;
        case model::Status::Error:      return audio::Cue::Error;
        default:                        return audio::Cue::Idle;
    }
}

uint32_t announcement_token(const model::Announcement& event)
{
    return event.ready_at_ms ^ (static_cast<uint32_t>(event.slot + 1) << 24);
}

float announcement_selection_visibility(int index)
{
    return status_animation::selection_visibility(
        announcing(), announce_age, model::state.selected == index);
}

void start_announcement(const model::Announcement& event)
{
    status_animation_active = true;
    release_status_freeze_after_frame = false;
    announce_slot_index = event.slot;
    announce_status = event.status;
    announce_unseen = event.unseen;
    // Finalise every fresh completion after its animation from the latest lamp
    // colour, which may have changed while this event waited in the queue.
    announce_fade_to_viewed = event.status == model::Status::Done && event.unseen;
    // This is deliberately the sole status-sound trigger. Reaching here means
    // the event survived its configured debounce and has actually claimed
    // the animation panel; superseded intermediate states never make a sound.
    announce_audio_armed = audio::arm_status(status_cue(event.status),
                                             announcement_token(event));
    // Start the animation clock only after the prepared score is armed.
    announce_age = 0.f;
    last_frame_ms = lgfx::millis();
    wake();
    dirty = true;
}

void draw_announcement()
{
    if (!announcing()) return;
    // Keep both rail gestures visible on the bare deck. Previously the status
    // surface covered the same pixels from frame one, hiding the movement.
    if (announce_age < kAnnounceRailOut) return;
    const float age = announce_age - kAnnounceRailOut;
    if (age >= kAnnounceVisualLife) return;
    uint16_t to_colour = status_surface(announce_status, announce_unseen);
    const uint16_t to_fg = status_foreground(announce_status);

    const float expand_start = kAnnounceColour;
    const float centre_start = expand_start + kAnnounceExpand;
    const float hold_start = centre_start + kAnnounceCentre;
    const float return_start = hold_start + kAnnounceHold;
    const float collapse_start = return_start + kAnnounceReturn;
    // A completion already open on the Mac is new long enough to be noticed,
    // then becomes viewed. This last colour fade lands on the exact resting
    // grey used by the deck, so the takeover never ends in a snap.
    const model::Task* live_task = announce_slot_index >= 0
                                && announce_slot_index < model::state.task_count
        ? &model::state.tasks[announce_slot_index] : nullptr;
    const float read_progress = status_animation::viewed_fade_progress(
        announce_fade_to_viewed, live_task, age);
    if (read_progress > 0.f) {
        to_colour = mix(to_colour, kViewed,
            static_cast<uint8_t>(motion::clamp01(read_progress) * 255.f));
    }

    float spread = 0.f;
    if (age >= expand_start && age < centre_start) {
        spread = motion::ease_in_out_cubic((age - expand_start) / kAnnounceExpand);
    } else if (age >= centre_start && age < collapse_start) {
        spread = 1.f;
    } else if (age >= collapse_start) {
        spread = 1.f - motion::ease_in_out_cubic(
            (age - collapse_start) / kAnnounceCollapse);
    }

    float centre = 0.f;
    if (age >= centre_start && age < hold_start) {
        centre = motion::ease_in_out_cubic((age - centre_start) / kAnnounceCentre);
    } else if (age >= hold_start && age < return_start) {
        centre = 1.f;
    } else if (age >= return_start && age < collapse_start) {
        centre = 1.f - motion::ease_in_out_cubic(
            (age - return_start) / kAnnounceReturn);
    }

    // Debounce already exposed the new semantic colour on the deck. Returning
    // to the old colour for the first takeover frame made the surface and digit
    // flash. Keep that target colour continuous while geometry does the work.
    const uint16_t colour = to_colour;
    const uint16_t fg = to_fg;

    const float resting_scale = 1.f;
    const int target_w = micro5_digits::kVisualWidth[0][announce_slot_index];
    const int target_h = micro5_digits::kVisualHeight[0][announce_slot_index];
    const int target_x = cell_x(announce_slot_index) + (cell_w(announce_slot_index) - target_w) / 2;
    const float selected = motion::ease_out_cubic(
        motion::clamp01(selection_amount[announce_slot_index].x));
    const int target_bottom = static_cast<int>(motion::lerp(
        static_cast<float>(kDigitRestBottom),
        static_cast<float>(kDigitSelectedBottom), selected) + 0.5f);
    const int target_y = target_bottom - target_h;
    const int source_x = cell_x(announce_slot_index);
    const int source_w = cell_w(announce_slot_index);
    const int x0 = static_cast<int>(motion::lerp(static_cast<float>(source_x), 0.f, spread));
    const int x1 = static_cast<int>(motion::lerp(static_cast<float>(source_x + source_w),
                                                static_cast<float>(kScreenW), spread));
    const bool gradient = announce_status == model::Status::Running
                       || announce_status == model::Status::NeedsInput
                       || (announce_status == model::Status::Done && announce_unseen);
    const uint8_t gradient_tint = gradient
        ? static_cast<uint8_t>((1.f - read_progress) * 38.f) : 0;
    fill_status_surface(x0, 0, x1 - x0, kScreenH, colour, 255, 0, gradient_tint);

    // The same glyph moves and scales into its resting place; no font swap and
    // no cut between the announcement and the deck.
    const float digit_scale = resting_scale + centre;  // Micro5 1x -> exact 2x
    const float expanded_scale = 2.f;
    const float resting_source_x = target_x - micro5_digits::kLeft[0][announce_slot_index];
    const float resting_source_y = target_y - micro5_digits::kTop[0][announce_slot_index];
    const float centred_visual_x = (kScreenW
        - micro5_digits::kVisualWidth[0][announce_slot_index] * expanded_scale) / 2.f;
    const float centred_visual_y = (kScreenH
        - micro5_digits::kVisualHeight[0][announce_slot_index] * expanded_scale) / 2.f;
    const float centred_source_x = centred_visual_x
        - micro5_digits::kLeft[0][announce_slot_index] * expanded_scale;
    const float centred_source_y = centred_visual_y
        - micro5_digits::kTop[0][announce_slot_index] * expanded_scale;
    const float digit_x = motion::lerp(resting_source_x, centred_source_x, centre);
    const float digit_y = motion::lerp(resting_source_y, centred_source_y, centre);
    draw_micro5_digit(announce_slot_index, digit_x, digit_y, digit_scale, fg);

    canvas.setTextDatum(textdatum_t::top_left);
}

// The map, on screen. Command-slot meaning remains visible/configurable in
// Codex; this page only explains the Cardputer's physical equivalents.
void draw_help()
{
    fill_rect(0, 0, kScreenW, kScreenH, kPaper);
    draw_tracked("keys", kMargin, 12, 1, kDim, kPaper);
    hline(kMargin, 26, kScreenW - 2 * kMargin, kRule);

    struct Row { const char* key; const char* what; };
    static const Row left[5] = {
        {"1-6",   "pick slot"},
        {"enter", "open on mac"},
        {"del",   "stop turn"},
        {"tab",   "user menu"},
        {"opt+tab", "debug"},
    };
    static const Row right[5] = {
        {"[ ]", "dial"},
        {"\\",   "dial click"},
        {"t-p", "cmd 6-11"},
        {"-",   "mute"},
        {"a",   "combined mic"},
    };
    for (int i = 0; i < 5; ++i) {
        const int y = 34 + i * 14;
        draw_tracked(left[i].key,   kMargin,      y, 1, kInk, kPaper);
        draw_tracked(left[i].what,  kMargin + 44, y, 1, kDim, kPaper);
        draw_tracked(right[i].key,  138,          y, 1, kInk, kPaper);
        draw_tracked(right[i].what, 138 + 30,     y, 1, kDim, kPaper);
    }
    draw_tracked("g0", kMargin, 104, 1, kInk, kPaper);
    draw_tracked("voice, held", kMargin + 44, 104, 1, kDim, kPaper);

    hline(kMargin, kRuleY, kScreenW - 2 * kMargin, kRule);
    draw_tracked("` back", kMargin, kChromeY, 1, kDim, kPaper);
}

void draw_settings()
{
    auto& s = model::state;
    fill_rect(0, 0, kScreenW, kScreenH, kPaper);
    draw_tracked("LOCAL", kGutter, 7, 2, kInk, kPaper);
    draw_tracked_right("TAB CLOSE", kScreenW - kGutter, 7, 1, kInkSoft, kPaper);
    hline(kGutter, 22, kScreenW - 2 * kGutter, kRule);

    struct Row { const char* label; const char* value; };
    char volume[8];
    char ble_profile[8];
    // The meter states the unit; repeating "%" or "BLE" beside it would only
    // add words to a control that already shows its own range.
    std::snprintf(volume, sizeof(volume), "%u", static_cast<unsigned>(s.sound_volume));
    std::snprintf(ble_profile, sizeof(ble_profile), "%u",
                  static_cast<unsigned>(s.ble_profile + 1));
    const Row rows[kSettingsRows] = {
        {"HOST CHANNEL", ble_profile},
        {"VOLUME", volume},
        {"STARTUP CHIME", !s.startup_sound_on ? "OFF" : (s.sound_volume > 0 ? "ON" : "MUTED")},
        {"RETURN TO M5APPS", "ENTER"},
    };

    const int list_top = 30;
    const int marker_y = list_top + static_cast<int>(settings_marker.x + 0.5f);
    const int value_right = kScreenW - kGutter - 7;

    // Rows carry their own ordinal. Numbering the settings the way the six task
    // keys are numbered makes the whole device one counted system, and it gives
    // the eye a fixed left edge to return to while the plate travels.
    auto draw_rows = [&](bool inverted) {
        const uint16_t label = inverted ? kPaper : kInkSoft;
        const uint16_t value = inverted ? kPaper : kInk;
        const uint16_t ordinal = inverted ? kGrey : kOrdinal;
        const uint16_t seat = inverted ? kGrey : kRule;
        for (int r = 0; r < kSettingsRows; ++r) {
            const int y = list_top + r * kSettingsPitch;
            const int text_y = y + kSettingsRowH / 2 - 4;
            const int meter_y = y + (kSettingsRowH - kSegH) / 2;
            char index[4];
            std::snprintf(index, sizeof(index), "%02d", r + 1);
            draw_tracked_transparent(index, kGutter + 6, text_y, 1, ordinal);
            draw_tracked_transparent(rows[r].label, kGutter + 27, text_y, 2, label);
            if (r == static_cast<int>(SettingsRow::BleProfile)) {
                const int meter_left = value_right - segment_width(3);
                draw_segment_selector(value_right, meter_y, 3, s.ble_profile,
                                      value, seat);
                draw_tracked_transparent(
                    rows[r].value,
                    meter_left - 8 - tracked_width(rows[r].value, 1), text_y,
                    1, value);
            } else if (r == static_cast<int>(SettingsRow::Volume)) {
                const int meter_left = value_right - segment_width(10);
                draw_segment_meter(value_right, meter_y, 10, settings_level.x,
                                   value, seat);
                draw_tracked_transparent(
                    rows[r].value, meter_left - 8 - tracked_width(rows[r].value, 1),
                    text_y, 1, value);
            } else {
                draw_tracked_transparent(
                    rows[r].value, value_right - tracked_width(rows[r].value, 2),
                    text_y, 2, value);
            }
        }
    };
    draw_selection_plate(kGutter, marker_y, kScreenW - 2 * kGutter,
                         kSettingsRowH, draw_rows);

    draw_bottom_rail("TAB CLOSE", nullptr, nullptr, "ARROWS  ENTER");
}

void draw_debug_settings()
{
    fill_rect(0, 0, kScreenW, kScreenH, kPaper);
    draw_tracked("DEBUG", kGutter, 7, 2, kInk, kPaper);
    draw_tracked_right("OPT+TAB CLOSE", kScreenW - kGutter, 7, 1, kInkSoft, kPaper);
    hline(kGutter, 22, kScreenW - 2 * kGutter, kRule);

    struct Row { const char* label; const char* value; };
    const Row rows[kDebugSettingsRows] = {
        {"USB HID", model::state.usb_hid_enabled ? "ON" : "OFF"},
        {"SCREEN CHECKS", "ENTER"},
        {"CHIME LAB", audio::startup_chime_name(model::state.startup_chime)},
        {"STATUS DEBUG", "ENTER"},
        {"RESET BLE BONDS", "ENTER"},
    };
    constexpr int top = 27;
    const int marker_y = top + static_cast<int>(debug_settings_marker.x + 0.5f);
    // The row owns one continuous selection plate. Drawing glyphs with an
    // opaque text background creates a separate box around every character and
    // makes the typeface itself appear to change.
    auto draw_rows = [&](bool inverted) {
        const uint16_t label = inverted ? kPaper : kInkSoft;
        const uint16_t value = inverted ? kPaper : kDim;
        const uint16_t ordinal = inverted ? kGrey : kOrdinal;
        for (int row = 0; row < kDebugSettingsRows; ++row) {
            const int y = top + row * kDebugSettingsPitch + 4;
            char index[4];
            std::snprintf(index, sizeof(index), "%02d", row + 1);
            draw_tracked_transparent(index, kGutter + 6, y, 1, ordinal);
            draw_tracked_transparent(rows[row].label, kGutter + 27, y, 1, label);
            draw_tracked_transparent(
                rows[row].value,
                kScreenW - kGutter - 7 - tracked_width(rows[row].value, 1), y,
                1, value);
        }
    };
    draw_selection_plate(kGutter, marker_y, kScreenW - 2 * kGutter,
                         kDebugSettingsRowH, draw_rows);
    draw_bottom_rail("` BACK", nullptr, nullptr, "UP DOWN ENTER");
}

void draw_previews()
{
    fill_rect(0, 0, kScreenW, kScreenH, kPaper);
    draw_tracked("SCREEN CHECKS", kGutter, 7, 2, kInk, kPaper);
    draw_tracked_right("` BACK", kScreenW - kGutter, 7, 1, kInkSoft, kPaper);
    hline(kGutter, 22, kScreenW - 2 * kGutter, kRule);

    const char* labels[kPreviewsRows] = {"SPLASH", "PAIRING PIN", "DIAL", "STICK"};
    constexpr int top = 27;
    const int marker_y = top + static_cast<int>(previews_marker.x + 0.5f);
    auto draw_rows = [&](bool inverted) {
        const uint16_t label = inverted ? kPaper : kInkSoft;
        const uint16_t value = inverted ? kPaper : kDim;
        const uint16_t ordinal = inverted ? kGrey : kOrdinal;
        for (int row = 0; row < kPreviewsRows; ++row) {
            const int y = top + row * kDebugSettingsPitch + 4;
            char index[4];
            std::snprintf(index, sizeof(index), "%02d", row + 1);
            draw_tracked_transparent(index, kGutter + 6, y, 1, ordinal);
            draw_tracked_transparent(labels[row], kGutter + 27, y, 1, label);
            draw_tracked_transparent("ENTER",
                kScreenW - kGutter - 7 - tracked_width("ENTER", 1), y, 1, value);
        }
    };
    draw_selection_plate(kGutter, marker_y, kScreenW - 2 * kGutter,
                         kDebugSettingsRowH, draw_rows);
    draw_bottom_rail("` BACK", nullptr, nullptr, "UP DOWN ENTER");
}

void draw_chime_lab()
{
    fill_rect(0, 0, kScreenW, kScreenH, kPaper);
    draw_tracked("CHIME LAB", kGutter, 7, 2, kInk, kPaper);
    draw_tracked_right("ENTER PLAY", kScreenW - kGutter, 7, 1, kInkSoft, kPaper);
    hline(kGutter, 22, kScreenW - 2 * kGutter, kRule);

    // The plate travels the grid on two springs, so a diagonal move reads as
    // one object crossing the panel rather than two edges sliding apart.
    const int marker_x = kChimeLeft + static_cast<int>(chime_marker_x.x + 0.5f);
    const int marker_y = kChimeTop + static_cast<int>(chime_marker_y.x + 0.5f);
    auto draw_pads = [&](bool inverted) {
        for (int index = 0; index < audio::kStartupChimeCount; ++index) {
            const int x = kChimeLeft + (index % kChimeCols) * (kChimeCellW + kChimeGap);
            const int y = kChimeTop + (index / kChimeCols) * (kChimeCellH + kChimeGap);
            if (!inverted) canvas.drawRect(x + layer_dx, y, kChimeCellW, kChimeCellH, kRule);
            char number[4];
            std::snprintf(number, sizeof(number), "%02d", index + 1);
            draw_tracked_transparent(number, x + 5, y + 6, 2,
                                     inverted ? kPaper : kInkSoft);
            // The longest name is six characters and the pad is 44 px wide, so
            // the caption is set solid: tracked names ran under the next pad's
            // border and made the grid look misaligned.
            draw_tracked_transparent(audio::startup_chime_name(index), x + 5,
                                     y + 24, 0, inverted ? kPaper : kDim);
        }
    };
    draw_selection_plate(marker_x, marker_y, kChimeCellW, kChimeCellH, draw_pads);
    draw_bottom_rail("` BACK", "ARROWS", nullptr, "ENTER PLAY");
}

void draw_status_debug()
{
    static constexpr const char* labels[kDebugRows] = {
        "DEBOUNCE", "AUDIO OFFSET", "RUNNING", "INPUT", "DONE", "ERROR", "IDLE"
    };
    fill_rect(0, 0, kScreenW, kScreenH, kPaper);
    draw_tracked("STATUS DEBUG", kGutter, 7, 2, kInk, kPaper);
    draw_tracked_right("` BACK", kScreenW - kGutter, 7, 1, kInkSoft, kPaper);
    hline(kGutter, 22, kScreenW - 2 * kGutter, kRule);

    constexpr int top = 26;
    const int marker_y = top + static_cast<int>(debug_marker.x + 0.5f);
    auto draw_rows = [&](bool inverted) {
        const uint16_t label = inverted ? kPaper : kInkSoft;
        const uint16_t value_colour = inverted ? kPaper : kDim;
        const uint16_t ordinal = inverted ? kGrey : kOrdinal;
        for (int row = 0; row < kDebugRows; ++row) {
            const int y = top + row * kDebugPitch + 2;
            char value[16] = "ENTER";
            if (row == 0) {
                std::snprintf(value, sizeof(value), "%u MS",
                              static_cast<unsigned>(model::state.status_debounce_ms));
            } else if (row == 1) {
                std::snprintf(value, sizeof(value), "%+d MS",
                              static_cast<int>(model::state.status_audio_offset_ms));
            }
            char index[4];
            std::snprintf(index, sizeof(index), "%02d", row + 1);
            draw_tracked_transparent(index, kGutter + 6, y, 1, ordinal);
            draw_tracked_transparent(labels[row], kGutter + 27, y, 2, label);
            draw_tracked_transparent(
                value, kScreenW - kGutter - 7 - tracked_width(value, 1), y, 1,
                value_colour);
        }
    };
    draw_selection_plate(kGutter, marker_y, kScreenW - 2 * kGutter, kDebugRowH,
                         draw_rows);
    if (debug_row == 1) {
        char current[16];
        std::snprintf(current, sizeof(current), "%+d MS",
                      static_cast<int>(model::state.status_audio_offset_ms));
        draw_bottom_rail("-300", "<", current, ">  +300");
    } else {
        draw_bottom_rail("` BACK", nullptr, nullptr, "UP DOWN  ENTER");
    }
}

void draw_splash_field(float intensity, int origin_y)
{
    // Native pixel packets burst from the product mark on fixed lanes. Every
    // element is a hard 1x1 or 2x2 square: no antialiased curves, gradients or
    // fake progress. Different lane speeds keep the field computational rather
    // than decorative, while the low contrast leaves the wordmark dominant.
    const int cx = kScreenW / 2;
    for (int packet = 0; packet < 24; ++packet) {
        const float lane = packet * (6.2831853f / 24.f)
                         + (packet % 3) * 0.067f;
        const float speed = 0.145f + (packet % 5) * 0.014f;
        const float seed = std::fmod(packet * 0.381966f, 1.f);
        const float phase = std::fmod(clock_phase * speed + seed, 1.f);
        const float envelope = std::sin(phase * 3.14159265f);
        const float radius = 10.f + phase * 126.f;
        const int x = cx + static_cast<int>(std::cos(lane) * radius);
        const int y = origin_y + static_cast<int>(std::sin(lane) * radius * 0.43f);
        const uint8_t alpha = static_cast<uint8_t>(intensity * envelope * 118.f);
        const uint16_t packet_colour = packet % 6 == 0 ? kBlue : kDim;
        const uint16_t head = mix(kPaper, packet_colour, alpha);
        const int size = packet % 3 == 0 ? 3 : 2;
        fill_rect(x, y, size, size, head);
        if (phase > 0.08f) {
            for (int tail = 1; tail <= 2; ++tail) {
                const float tail_radius = radius - tail * 7.f;
                const int tx = cx + static_cast<int>(std::cos(lane) * tail_radius);
                const int ty = origin_y
                             + static_cast<int>(std::sin(lane) * tail_radius * 0.43f);
                fill_rect(tx, ty, 1, 1,
                          mix(kPaper, packet_colour, alpha / (tail + 1)));
            }
        }
    }
    const uint16_t registration = mix(kPaper, kDim,
        static_cast<uint8_t>(intensity * 58.f));
    hline(9, 12, 13, registration);  vline(9, 12, 8, registration);
    hline(kScreenW - 22, 12, 13, registration);
    vline(kScreenW - 10, 12, 8, registration);
    hline(9, 101, 13, registration); vline(9, 94, 8, registration);
    hline(kScreenW - 22, 101, 13, registration);
    vline(kScreenW - 10, 94, 8, registration);
}

void draw_boot()
{
    const float intro = motion::ease_out_cubic(boot_ramp.t);
    const int cx = kScreenW / 2;
    fill_rect(0, 0, kScreenW, kScreenH, kPaper);
    draw_splash_field(intro, 61);

    // One coherent pixel face, three restrained sizes. The wordmark sits on
    // the same baselines as the grid instead of floating in an empty canvas.
    const uint8_t mark_alpha = static_cast<uint8_t>(
        motion::clamp01(intro * 1.35f) * 255.f);
    const uint16_t mark = mix(kPaper, kGrey, mark_alpha);
    const int mark_width = tracked_width("CODEX", 4, 3) + 1;
    const int mark_x = cx - mark_width / 2;
    const int mark_y = static_cast<int>(motion::lerp(51.f, 44.f, intro));
    draw_tracked("CODEX", mark_x, mark_y, 4, mark, kPaper, 3);
    // A one-pixel overprint gives the stock pixel face a deliberate display
    // weight without substituting another font or introducing smoothing.
    draw_tracked_transparent("CODEX", mark_x + 1, mark_y, 4, mark, 3);

    const float sub_intro = motion::ease_out_cubic((intro - 0.34f) / 0.66f);
    const uint16_t sub = mix(kPaper, kDim,
        static_cast<uint8_t>(motion::clamp01(sub_intro) * 255.f));

    const char* product = "MICROPUTER ADV";
    draw_tracked(product, cx - tracked_width(product, 2) / 2,
                 80, 2, sub, kPaper);

    // The running build is reference, not identity: it belongs with the
    // registration marks in the trim, not on the centre line with the name.
    // So it goes back to the top right and is written the way the marks are
    // written -- right-aligned to the inner edge of the corner mark, on the
    // same rule, tracked tight, and mixed most of the way back to the paper.
    // The stock pixel face has one size, so "smaller" here is tighter and
    // fainter rather than a second font: it should be findable, not read.
    // The app descriptor allows 32 characters; the corner has room for a
    // release number, not for a git describe string, so it is clipped here.
    char build[13];
    std::snprintf(build, sizeof(build), "%.12s", firmware::version());
    const uint16_t stamp = mix(kPaper, kDim,
        static_cast<uint8_t>(motion::clamp01(intro) * 92.f));
    draw_tracked_right(build, kScreenW - 26, 12, 0, stamp, kPaper);

    // Restore the arcade cadence: the prompt is an action, so a crisp blink is
    // more legible than making the whole composition pulse.
    const bool prompt_on = std::fmod(clock_phase, 2.00f) < 1.55f;
    if (prompt_on && intro > 0.72f) {
        const char* prompt = "USB OR BLUETOOTH  OPEN CODEX";
        draw_tracked(prompt, cx - tracked_width(prompt, 1, 1) / 2,
                     115, 1, kInkSoft, kPaper, 1);
    }
    canvas.setTextDatum(textdatum_t::top_left);
}

void draw_toast()
{
    if (toast_title[0] == 0) return;
    const float t = toast_out ? 1.f - motion::ease_out_cubic(toast_in.t)
                              : motion::ease_out_cubic(toast_in.t);
    const int h = 30;
    const int y = static_cast<int>(motion::lerp(-static_cast<float>(h), 0.f, t));
    if (y <= -h) return;

    canvas.fillRect(0, y, kScreenW, h, toast_accent);
    canvas.drawFastHLine(0, y + h, kScreenW, kInk);
    const int saved = layer_dx;
    layer_dx = 0;
    draw_tracked(toast_title, kGutter, y + 6, 2, kPaper, toast_accent);
    layer_dx = saved;
    canvas.setFont(&fonts::efontCN_12);
    canvas.setClipRect(kGutter, y + 15, kScreenW - 2 * kGutter, 13);
    canvas.drawString(toast_detail, kGutter, y + 16);
    canvas.clearClipRect();
}

void draw_pairing_takeover()
{
    fill_rect(0, 0, kScreenW, kScreenH, kPaper);
    // Pairing is a state of the splash, not a foreign dialog. Reuse its quiet
    // packet field and registration marks while keeping the code dominant.
    draw_splash_field(0.48f, 63);
    const int cx = kScreenW / 2;
    const char* eyebrow = "BLUETOOTH  PAIRING";
    draw_tracked(eyebrow, cx - tracked_width(eyebrow, 1, 1) / 2,
                 18, 1, kBlue, kPaper, 1);

    char pin[7];
    std::snprintf(pin, sizeof(pin), "%06lu", static_cast<unsigned long>(pairing_pin));
    // Use exactly the same Micro5 bitmap glyphs as the six task towers. The
    // font asset now contains 0..9; no substitute text face or fake overprint.
    constexpr int gap = 7;
    int pin_width = gap * 5;
    for (char digit : pin) {
        if (!digit) break;
        pin_width += micro5_digits::kVisualWidth[0][micro5_pin_glyph(digit)];
    }
    int visual_x = cx - pin_width / 2;
    constexpr int visual_y = 49;
    for (char digit : pin) {
        if (!digit) break;
        const int glyph = micro5_pin_glyph(digit);
        draw_micro5_digit(glyph,
            visual_x - micro5_digits::kLeft[0][glyph],
            visual_y - micro5_digits::kTop[0][glyph], 1.f, kInk);
        visual_x += micro5_digits::kVisualWidth[0][glyph] + gap;
    }

    const char* hint = "ENTER PIN ON MAC";
    draw_tracked(hint, cx - tracked_width(hint, 1, 1) / 2,
                 112, 1, kInkSoft, kPaper, 1);
}

void draw_screen(Screen which)
{
    switch (which) {
        case Screen::Boot:     draw_boot();     break;
        case Screen::Deck:     draw_deck();     break;
        case Screen::Settings: draw_settings(); break;
        case Screen::DebugSettings: draw_debug_settings(); break;
        case Screen::Previews: draw_previews(); break;
        case Screen::StatusDebug: draw_status_debug(); break;
        case Screen::ChimeLab: draw_chime_lab(); break;
        case Screen::Help:     draw_help();     break;
    }
}

void render()
{
    if (!canvas_ready) return;
    if (developer_preview != DeveloperPreview::None) {
        layer_dx = 0;
        if (developer_preview == DeveloperPreview::Splash) {
            draw_boot();
        } else if (developer_preview == DeveloperPreview::Pairing) {
            draw_pairing_takeover();
        } else if (developer_preview == DeveloperPreview::Stick) {
            draw_deck();
            draw_stick_control_takeover();
        } else {
            draw_deck();
            draw_composer_control_takeover();
        }
        canvas.pushSprite(0, 0);
        return;
    }
    if (transition.running()) {
        // Both screens are drawn each frame; at 240x135 that is cheaper than
        // keeping a second full-screen sprite alive next to the BLE stack.
        const float t = motion::ease_in_out_cubic(transition.t);
        const int travel = static_cast<int>(t * kScreenW);
        layer_dx = -transition_dir * travel;
        draw_screen(previous);
        layer_dx = transition_dir * (kScreenW - travel);
        draw_screen(current);
        layer_dx = 0;
    } else {
        layer_dx = 0;
        draw_screen(current);
    }
    if (pairing_pin_active) {
        layer_dx = 0;
        draw_pairing_takeover();
        canvas.pushSprite(0, 0);
        return;
    }
    // An active microphone is the only mode louder than a status announcement:
    // losing it under completion feedback would make the user unsure whether
    // Codex is still listening.
    if (current != Screen::Boot) {
        if (voice_amount.x > 0.f) {
            draw_voice_takeover();
        } else if (composer_control_amount.x > 0.f) {
            draw_composer_control_takeover();
        } else if (stick_control_amount.x > 0.f) {
            draw_stick_control_takeover();
        } else {
            draw_announcement();
            if (!announcing()) draw_toast();
        }
    }
    // Low-brightness colour collapse belongs to the disconnected attract
    // screen only. Status colours and task contrast must remain literal while
    // the deck, menus, recording, or pairing UI is in use.
    if (current == Screen::Boot && !transition.running()) {
        display_fade::apply(canvas.getBuffer(), kScreenW * kScreenH, colour_level);
    }
    canvas.pushSprite(0, 0);
    // The LCD has accepted the first animation frame. Only now wake the speaker
    // task, so audio can never become audible before the visual response.
    if (announce_audio_armed && announce_age >= kAnnounceRailOut) {
        if (audio::play_prepared_status())
            announce_audio_armed = false;
    }
    // Present one complete resting deck frame while its colour clock is still
    // frozen. Releasing before pushSprite made the rail background jump on the
    // exact frame where the takeover layer disappeared.
    if (release_status_freeze_after_frame) {
        release_status_freeze_after_frame = false;
        status_animation_active = false;
    }
}

void capture_gesture(int direction)
{
    auto& s = model::state;
    std::snprintf(prev_index, sizeof(prev_index), "%s", curr_index);
    prev_status = curr_status;
    const model::Task* task = model::selected_task();
    std::snprintf(prev_title, sizeof(prev_title), "%s", task ? task->title : "");
    std::snprintf(curr_index, sizeof(curr_index), "%02d", (s.selected + 1) % 100);
    const model::Status fresh = task ? task->status : model::Status::Idle;
    if (fresh != curr_status) { prev_status = curr_status; since_status = 0.f; }
    else                      { prev_status = fresh; since_status = 99.f; }
    curr_status = fresh;
    select_dir = direction >= 0 ? 1 : -1;
    since_select = 0.f;
}

void retarget_deck(bool animate)
{
    marquee.snap(0.f);
    marquee_hold = 1.2f;
    marquee_dir = 1;
    if (!animate) since_select = 99.f;
    dirty = true;
}

}  // namespace

// ==================================================================== public
void cancel_status_announcements() { cancel_status_announcements_internal(); }

void init()
{
    canvas.setColorDepth(16);
    canvas_ready = canvas.createSprite(kScreenW, kScreenH) != nullptr;
    canvas.setTextWrap(false);
    M5.Display.setRotation(1);
    M5.Display.setBrightness(kBrightFull);
    last_frame_ms = lgfx::millis();
    last_activity_ms = last_frame_ms;
    for (int i = 0; i < kCellCount; ++i) selection_amount[i].snap(i == 0 ? 1.f : 0.f);
    boot_ramp.restart(1.1f);
    current = Screen::Boot;
    dirty = true;
}

void invalidate() { dirty = true; }

const uint16_t* capture_frame(const char* scene)
{
    if (!canvas_ready || !scene) return nullptr;
    if (std::strcmp(scene, "live") == 0)
        return static_cast<const uint16_t*>(canvas.getBuffer());

    // Capture scenes borrow the production renderer. Save every value they
    // touch so a screenshot cannot change the selected task or leave a modal
    // surface behind on the physical device.
    const model::State saved_state = model::state;
    const Screen saved_current = current;
    const Screen saved_previous = previous;
    const DeveloperPreview saved_preview = developer_preview;
    const bool saved_pairing_active = pairing_pin_active;
    const uint32_t saved_pairing_pin = pairing_pin;
    const motion::Spring saved_voice_amount = voice_amount;
    const bool saved_voice_target = voice_target;
    const int saved_voice_slot = voice_slot;
    const motion::Spring saved_composer_amount = composer_control_amount;
    const bool saved_composer_target = composer_control_target;
    const bool saved_lamp_valid = composer_lamp_valid;
    const int  saved_lamp_lit = composer_lamp_lit;
    const motion::Spring saved_knob = composer_knob_angle;
    const motion::Ramp saved_boot_ramp = boot_ramp;
    const motion::Ramp saved_transition = transition;
    motion::Spring saved_selection[kCellCount];
    std::memcpy(saved_selection, selection_amount, sizeof(saved_selection));
    const float saved_deck_entrance = deck_entrance;
    const int saved_layer_dx = layer_dx;
    // The menu plates and the volume meter are sprung, so a capture taken mid
    // travel would not be the representative frame this call promises.
    const motion::Spring saved_settings_marker = settings_marker;
    const motion::Spring saved_settings_level = settings_level;
    const motion::Spring saved_debug_settings_marker = debug_settings_marker;
    const motion::Spring saved_debug_marker = debug_marker;
    const motion::Spring saved_chime_marker_x = chime_marker_x;
    const motion::Spring saved_chime_marker_y = chime_marker_y;

    auto& state = model::state;
    state.link = model::Link::Usb;
    state.task_count = kCellCount;
    state.selected = 1;
    state.battery = 82;
    state.charging = true;
    state.sound_volume = 60;
    state.ble_signal_weak = false;
    static constexpr model::Status statuses[kCellCount] = {
        model::Status::Running, model::Status::NeedsInput, model::Status::Done,
        model::Status::Done, model::Status::Idle, model::Status::Running,
    };
    static constexpr const char* titles[kCellCount] = {
        "Build firmware", "Review API", "Publish release",
        "Read documentation", "Plan next task", "Run tests",
    };
    for (int i = 0; i < kCellCount; ++i) {
        state.tasks[i] = model::Task{};
        state.tasks[i].present = true;
        state.tasks[i].seen = true;
        state.tasks[i].status = statuses[i];
        state.tasks[i].unseen_done = (i == 2);
        std::snprintf(state.tasks[i].id, sizeof(state.tasks[i].id), "demo-%d", i + 1);
        std::snprintf(state.tasks[i].title, sizeof(state.tasks[i].title), "%s", titles[i]);
        selection_amount[i].snap(i == state.selected ? 1.f : 0.f);
    }
    deck_entrance = 9.f;
    layer_dx = 0;
    transition.finish();
    developer_preview = DeveloperPreview::None;
    pairing_pin_active = false;
    voice_target = false;
    voice_amount.snap(0.f);
    composer_control_target = false;
    composer_control_amount.snap(0.f);

    bool known = true;
    if (std::strcmp(scene, "splash") == 0) {
        current = Screen::Boot;
        boot_ramp.finish();
        draw_boot();
    } else if (std::strcmp(scene, "pairing") == 0) {
        current = Screen::Boot;
        boot_ramp.finish();
        pairing_pin = 428615;
        draw_pairing_takeover();
    } else if (std::strcmp(scene, "deck") == 0) {
        current = Screen::Deck;
        draw_deck();
    } else if (std::strcmp(scene, "recording") == 0) {
        current = Screen::Deck;
        voice_slot = state.selected;
        voice_target = true;
        voice_amount.snap(1.f);
        draw_deck();
        draw_voice_takeover();
    } else if (std::strcmp(scene, "composer") == 0) {
        current = Screen::Deck;
        composer_control_target = true;
        composer_control_amount.snap(1.f);
        {
            const uint32_t rgb[6] = {0x2f6bd8, 0x2f6bd8, 0x2f6bd8,
                                     0xffa733, 0x3a3a3a, 0x3a3a3a};
            const float level[6] = {0.35f, 0.35f, 0.35f, 1.f, 0.f, 0.f};
            note_composer_control_lamps(rgb, level);
            for (int i = 0; i < kLampCount; ++i)
                composer_lamp_level[i].snap(level[i]);
            composer_lamp_marker.snap(3.f);
            composer_knob_angle.snap(kDetentRadians * 0.5f);
            composer_control_step_dir = 1;
            composer_control_step_age = 0.08f;
        }
        draw_deck();
        draw_composer_control_takeover();
    } else if (std::strcmp(scene, "settings") == 0) {
        current = Screen::Settings;
        settings_marker.snap(static_cast<float>(settings_row * kSettingsPitch));
        settings_level.snap(state.sound_volume / 10.f);
        draw_settings();
    } else if (std::strcmp(scene, "debug") == 0) {
        current = Screen::DebugSettings;
        debug_settings_marker.snap(
            static_cast<float>(debug_settings_row * kDebugSettingsPitch));
        draw_debug_settings();
    } else if (std::strcmp(scene, "previews") == 0) {
        current = Screen::Previews;
        previews_marker.snap(static_cast<float>(previews_row * kDebugSettingsPitch));
        draw_previews();
    } else if (std::strcmp(scene, "stick") == 0) {
        current = Screen::Deck;
        stick_control_amount.snap(1.f);
        stick_control_target = true;
        stick_step_dir = 1;
        stick_step_age = 0.08f;
        stick_lean_x.snap(0.f);
        stick_lean_y.snap(0.9f);
        draw_deck();
        draw_stick_control_takeover();
        stick_control_target = false;
        stick_control_amount.snap(0.f);
    } else if (std::strcmp(scene, "signal") == 0) {
        // Both pieces of exceptional deck chrome at once, which is the only
        // arrangement where they can collide.
        current = Screen::Deck;
        state.link = model::Link::Ble;
        state.ble_signal_weak = true;
        state.battery = 12;
        draw_deck();
    } else if (std::strcmp(scene, "chime") == 0) {
        current = Screen::ChimeLab;
        chime_marker_x.snap(static_cast<float>(
            (state.startup_chime % kChimeCols) * (kChimeCellW + kChimeGap)));
        chime_marker_y.snap(static_cast<float>(
            (state.startup_chime / kChimeCols) * (kChimeCellH + kChimeGap)));
        draw_chime_lab();
    } else if (std::strcmp(scene, "status") == 0) {
        current = Screen::StatusDebug;
        debug_marker.snap(static_cast<float>(debug_row * kDebugPitch));
        draw_status_debug();
    } else {
        known = false;
    }

    const uint16_t* pixels = known
        ? static_cast<const uint16_t*>(canvas.getBuffer()) : nullptr;

    model::state = saved_state;
    current = saved_current;
    previous = saved_previous;
    developer_preview = saved_preview;
    pairing_pin_active = saved_pairing_active;
    pairing_pin = saved_pairing_pin;
    voice_amount = saved_voice_amount;
    voice_target = saved_voice_target;
    voice_slot = saved_voice_slot;
    composer_control_amount = saved_composer_amount;
    composer_control_target = saved_composer_target;
    composer_lamp_valid = saved_lamp_valid;
    composer_lamp_lit = saved_lamp_lit;
    composer_knob_angle = saved_knob;
    boot_ramp = saved_boot_ramp;
    transition = saved_transition;
    std::memcpy(selection_amount, saved_selection, sizeof(saved_selection));
    deck_entrance = saved_deck_entrance;
    layer_dx = saved_layer_dx;
    settings_marker = saved_settings_marker;
    settings_level = saved_settings_level;
    debug_settings_marker = saved_debug_settings_marker;
    debug_marker = saved_debug_marker;
    chime_marker_x = saved_chime_marker_x;
    chime_marker_y = saved_chime_marker_y;
    dirty = true;
    return pixels;
}

Screen screen() { return current; }

void go(Screen target)
{
    if (target == current) return;
    if (target != Screen::Deck && composer_control_target)
        set_composer_control_active(false);
    if (target == Screen::Boot) {
        // Losing the host is a state boundary, not navigation: never leave a
        // stale deck visible during a screen transition.
        current = previous = Screen::Boot;
        transition.finish();
        boot_ramp.restart(1.1f);
        dirty = true;
        return;
    }
    // Deck is the root: moving to it is a pop, moving away is a push.
    transition_dir = (target == Screen::Deck) ? -1 : 1;
    previous = current;
    current = target;
    transition.restart(4.5f);
    if (target == Screen::Deck) deck_entrance = 0.f;
    // A page arrives with its plate already in place; only moving within the
    // page is a gesture worth animating.
    if (target == Screen::Settings) {
        settings_marker.snap(static_cast<float>(settings_row * kSettingsPitch));
        settings_level.snap(model::state.sound_volume / 10.f);
    }
    if (target == Screen::DebugSettings)
        debug_settings_marker.snap(static_cast<float>(debug_settings_row * kDebugSettingsPitch));
    if (target == Screen::Previews)
        previews_marker.snap(static_cast<float>(previews_row * kDebugSettingsPitch));
    if (target == Screen::StatusDebug)
        debug_marker.snap(static_cast<float>(debug_row * kDebugPitch));
    if (target == Screen::ChimeLab) {
        chime_marker_x.snap(static_cast<float>(
            (model::state.startup_chime % kChimeCols) * (kChimeCellW + kChimeGap)));
        chime_marker_y.snap(static_cast<float>(
            (model::state.startup_chime / kChimeCols) * (kChimeCellH + kChimeGap)));
    }
    dirty = true;
}

void select(int index, bool animate)
{
    auto& s = model::state;
    if (s.task_count <= 0) { s.selected = 0; return; }
    if (index < 0) index = 0;
    if (index >= s.task_count) index = s.task_count - 1;
    // Replay the gesture only when the selection actually moved. The host
    // republishes the deck every couple of seconds, and animating on every
    // refresh made the panel loop instead of respond.
    // Only a real move animates. The host republishes the deck on its own
    // schedule, and replaying the gesture for an unchanged selection is what
    // made the panel look like it was being pressed again by itself.
    if (index == s.selected) { retarget_deck(animate); return; }
    const int direction = index > s.selected ? 1 : -1;
    prev_selected = s.selected;
    s.selected = index;
    for (int i = 0; i < kCellCount; ++i) {
        const float target = i == index ? 1.f : 0.f;
        if (animate) selection_amount[i].to(target); else selection_amount[i].snap(target);
    }
    capture_gesture(direction);
    retarget_deck(animate);
}

void notify_press(int slot)
{
    if (slot < 0 || slot >= kCellCount) return;
    cell_press[slot] = 0.f;
    dirty = true;
}

void set_voice_active(bool active, int slot)
{
    if (active && composer_control_target) set_composer_control_active(false);
    if (active) voice_slot = std::clamp(slot, 0, kCellCount - 1);
    voice_target = active;
    voice_amount.to(active ? 1.f : 0.f);
    if (active) wake();
    dirty = true;
}

void set_composer_control_active(bool active)
{
    composer_control_target = active;
    composer_control_amount.to(active ? 1.f : 0.f);
    composer_control_idle = active ? 0.f : 99.f;
    composer_control_engaged = false;
    composer_control_step_age = 99.f;
    composer_control_step_dir = 0;
    if (active) {
        composer_knob_angle.snap(0.f);
    } else {
        composer_control_closed_at_ms = lgfx::millis();
        composer_lamp_valid = false;
        composer_lamp_lit = -1;
        for (auto& level : composer_lamp_level) level.snap(0.f);
    }
    if (active) wake();
    dirty = true;
}

void set_pairing_pin(bool active, uint32_t passkey)
{
    pairing_pin_active = active;
    pairing_pin = passkey;
    if (active) wake();
    dirty = true;
}

bool composer_control_active() { return composer_control_target; }

void dismiss_composer_control_preview()
{
    set_composer_control_active(false);
    composer_control_suppressed_until_ms = lgfx::millis() + 5000;
}

void notify_composer_control_step(int direction)
{
    // Turning the dial is itself the request to see the surface. Waiting for a
    // press meant the first detent of every adjustment happened blind.
    if (!composer_control_target) {
        composer_control_suppressed_until_ms = 0;
        composer_control_closed_at_ms = 0;
        set_composer_control_active(true);
        composer_control_open_sound_pending = true;
    }
    composer_control_step_dir = direction < 0 ? -1 : 1;
    composer_control_step_age = 0.f;
    composer_control_idle = 0.f;
    composer_knob_angle.to(composer_knob_angle.target
                           + composer_control_step_dir * kDetentRadians);
    dirty = true;
}

void notify_composer_control_select()
{
    if (!composer_control_target) return;
    composer_control_idle = 0.f;
    // A click may have stepped into a submenu, and from the click alone the
    // device cannot tell. So it stops timing: from here the page waits for the
    // host or for Esc, however long the user takes.
    composer_control_engaged = true;
    invalidate();
}

void note_composer_control_lamps(const uint32_t* rgb, const float* level)
{
    int lit = -1;
    float best = 0.f;
    for (int i = 0; i < kLampCount; ++i) {
        const uint32_t c = rgb[i];
        composer_lamp_colour[i] = canvas.color565(
            static_cast<uint8_t>((c >> 16) & 0xff),
            static_cast<uint8_t>((c >> 8) & 0xff),
            static_cast<uint8_t>(c & 0xff));
        composer_lamp_level[i].to(level[i]);
        if (level[i] > best) { best = level[i]; lit = i; }
    }
    composer_lamp_valid = true;
    composer_lamp_age = 0.f;
    if (lit >= 0) {
        composer_lamp_lit = lit;
        composer_lamp_marker.to(static_cast<float>(lit));
        composer_control_engaged = true;
    }
    invalidate();
}

void notify_stick_step(int direction)
{
    if (direction < 0 || direction > 3) return;
    if (!stick_control_target) {
        stick_control_target = true;
        stick_control_amount.to(1.f);
        stick_lean_x.snap(0.f);
        stick_lean_y.snap(0.f);
        stick_open_sound_pending = true;
        wake();
    }
    stick_step_dir = direction;
    stick_step_age = 0.f;
    stick_control_idle = 0.f;
    // Push the stick over, then let it fall back to centre on its own spring.
    // The impulse the host receives is instantaneous; the object is not.
    static const int8_t dirs[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    // Screen y runs the same way the direction table does: down is +1 in
    // both. Negating it here sent every vertical throw the wrong way, so the
    // cap answered a Down key by hopping away from the chevron that lit.
    //
    // The cap springs out and then springs back; it is not teleported to full
    // deflection and released. Snapping it there was a jump on every press,
    // and on a held arrow the repeats made the whole thing stutter.
    stick_lean_x.to(dirs[direction][0] * 1.f);
    stick_lean_y.to(dirs[direction][1] * 1.f);
    stick_throw_hold = 0.f;
    invalidate();
}

bool stick_control_active() { return stick_control_target; }

void dismiss_stick_control()
{
    if (!stick_control_target) return;
    stick_control_target = false;
    stick_control_amount.to(0.f);
    stick_step_dir = -1;
    stick_throw_hold = -1.f;
    stick_lean_x.to(0.f);
    stick_lean_y.to(0.f);
    stick_control_idle = 99.f;
    invalidate();
}

void note_composer_control_closed()
{
    if (!composer_control_target) return;
    std::printf("CCP_UI|composer|closed_by_host\n");
    dismiss_composer_control_preview();
}

void note_composer_control_preview()
{
    // Host UI can open the picker without a Cardputer key. Join the same mode,
    // except during a short post-confirmation quarantine: the final light frame
    // often arrives after the encoder release and must not resurrect the view.
    const uint32_t now = lgfx::millis();
    if (composer_control_suppressed_until_ms != 0
        && static_cast<int32_t>(composer_control_suppressed_until_ms - now) > 0) return;
    if (composer_control_target) {
        // The host is still painting its lamps, so its surface did not close --
        // whatever the click did, it was not a confirmation that ended it. Call
        // off the pending exit: a click on Micro can just as easily step into a
        // submenu or cycle a value, and the device cannot tell which from the
        // click alone. It can tell from this.
        if (composer_control_engaged) composer_control_idle = 0.f;
        dirty = true;
        return;
    }
    if (composer_control_closed_at_ms != 0
        && now - composer_control_closed_at_ms < 1500) return;
    set_composer_control_active(true);
    // Protocol callbacks may run inside BLE. Defer speaker access to service(),
    // which owns UI/audio sequencing on the main task.
    composer_control_open_sound_pending = true;
}

void toast(const char* title, const char* detail, uint16_t accent)
{
    std::snprintf(toast_title, sizeof(toast_title), "%s", title ? title : "");
    std::snprintf(toast_detail, sizeof(toast_detail), "%s", detail ? detail : "");
    toast_accent = accent;
    toast_in.restart(5.f);
    toast_out = false;
    toast_life = 2.6f;
    dirty = true;
}

bool wake()
{
    // Any key wakes the panel, and that key does nothing else. Once the screen
    // has dimmed the user cannot be sure what they are aiming at, so the safe
    // reading of a keypress is "show me", never "act".
    const bool was_dimmed = power_state != 0;
    last_activity_ms = lgfx::millis();
    if (was_dimmed) {
        power_state = 0;
        dirty = true;
    }
    return was_dimmed;
}

bool asleep() { return power_state == 2; }

void service_power()
{
    const uint32_t now = lgfx::millis();
    static uint32_t seen_host_activity = 0;
    static uint32_t seen_lighting_serial = 0;
    static uint32_t lighting_changed_ms = 0;
    if (seen_host_activity != model::state.host_activity_serial) {
        seen_host_activity = model::state.host_activity_serial;
        last_activity_ms = now;
        power_state = 0;
        dirty = true;
    }
    if (seen_lighting_serial != model::state.host_lighting_serial) {
        seen_lighting_serial = model::state.host_lighting_serial;
        lighting_changed_ms = now;
    }

    const uint32_t idle = now - last_activity_ms;
    const uint8_t configured = model::state.host_lighting_seen
        ? static_cast<uint8_t>(std::clamp(model::state.host_brightness, 0.05f, 1.f) * kBrightFull)
        : kBrightFull;
    uint8_t target = configured;
    if (model::state.host_lighting_seen && model::state.link != model::Link::Offline) {
        const bool stable_host_off = !model::state.host_zones_enabled
            && !model::state.host_threads_enabled
            && now - lighting_changed_ms >= kHostOffDebounceMs
            && idle >= kHostOffDebounceMs;
        if (stable_host_off) {
            const bool dim_hold_complete = now - lighting_changed_ms >= kDimHoldMs;
            target = dim_hold_complete ? 0 : std::max<uint8_t>(1, configured / 10);
            power_state = dim_hold_complete ? 2 : 1;
        } else {
            target = configured;
            power_state = 0;
        }
    } else if (idle >= kDarkAfterMs) {
        target = 0; power_state = 2;
    } else if (idle >= kDimAfterMs) {
        target = std::max<uint8_t>(1, configured / 10); power_state = 1;
    } else {
        target = configured; power_state = 0;
    }
    static uint8_t reported_power_state = 0xff;
    if (reported_power_state != power_state) {
        reported_power_state = power_state;
        std::printf("CCP_POWER|%s|target=%u\n",
                    power_state == 2 ? "dark" : power_state == 1 ? "dim" : "awake",
                    static_cast<unsigned>(target));
    }

    // Slew toward the target instead of stepping onto it. Waking is the same
    // motion in reverse, so returning from dark is a rise, not a flash.
    static uint32_t last_slew_ms = 0;
    if (last_slew_ms == 0) last_slew_ms = now;
    const float dt = (now - last_slew_ms) / 1000.f;
    last_slew_ms = now;

    const float step = kBrightSlewPerSecond * (dt > 0.05f ? 0.05f : dt);
    if (brightness_now < target) brightness_now = std::min<float>(target, brightness_now + step);
    else if (brightness_now > target) brightness_now = std::max<float>(target, brightness_now - step);

    const uint8_t value = static_cast<uint8_t>(brightness_now + 0.5f);
    if (value != applied_brightness) {
        M5.Display.setBrightness(value);
        applied_brightness = value;
    }
    const uint8_t next_colour_level = configured == 0 ? 0 : static_cast<uint8_t>(
        std::clamp(brightness_now / configured, 0.f, 1.f) * 255.f);
    if (next_colour_level != colour_level) {
        colour_level = next_colour_level;
        dirty = true;
    }
}

void service()
{
    const uint32_t now = lgfx::millis();
    const uint32_t elapsed = now - last_frame_ms;
    if (elapsed < kFrameBudgetMs) return;
    last_frame_ms = now;
    const float dt = elapsed / 1000.f;
    if (composer_control_open_sound_pending) {
        composer_control_open_sound_pending = false;
        audio::play(audio::Cue::MenuOpen);
    }
    if (stick_open_sound_pending) {
        stick_open_sound_pending = false;
        audio::play(audio::Cue::MenuOpen);
    }
    if (!status_animation_active) clock_phase += dt;
    if (since_select < 9.f) since_select += dt;
    if (since_status < 9.f) since_status += dt;

    bool animating = false;
    // The gesture owns the frame until its slowest staged element has landed.
    if (since_select < motion::kStagTitle + motion::kBase) animating = true;
    if (since_status < motion::kFast) animating = true;

    if (current == Screen::Boot || developer_preview == DeveloperPreview::Splash) {
        boot_ramp.step(dt);
        animating = true;
    }

    if (transition.running()) { transition.step(dt); animating = true; }

    // One spring shape for every travelling plate on the local screens.
    if (!settings_marker.settled()) { settings_marker.step(dt, 24.f, 0.8f); animating = true; }
    if (!debug_settings_marker.settled()) { debug_settings_marker.step(dt, 24.f, 0.8f); animating = true; }
    if (!previews_marker.settled()) { previews_marker.step(dt, 24.f, 0.8f); animating = true; }
    if (!debug_marker.settled()) { debug_marker.step(dt, 24.f, 0.8f); animating = true; }
    if (!chime_marker_x.settled()) { chime_marker_x.step(dt, 24.f, 0.8f); animating = true; }
    if (!chime_marker_y.settled()) { chime_marker_y.step(dt, 24.f, 0.8f); animating = true; }
    // The volume meter follows the stored value rather than the keypress, so a
    // host-side or restored change lights the same blocks the same way.
    if (current == Screen::Settings) {
        settings_level.to(model::state.sound_volume / 10.f);
        if (!settings_level.settled()) { settings_level.step(dt, 26.f, 0.85f); animating = true; }
    }

    // A settled deck is fully static. Status does not request idle frames.
    for (int i = 0; i < kCellCount; ++i) {
        // Also catches host-side selection changes that bypass select().
        selection_amount[i].to(i == model::state.selected ? 1.f : 0.f);
        if (!selection_amount[i].settled()) {
            selection_amount[i].step(dt, 16.f, 0.96f);
            animating = true;
        }
    }

    if (!voice_amount.settled()) {
        voice_amount.step(dt, 15.f, 0.92f);
        animating = true;
    }
    // The breathing record dot needs frames only while Codex is listening.
    if (voice_target) animating = true;

    if (!composer_control_amount.settled()) {
        composer_control_amount.step(dt, 15.f, 0.92f);
        animating = true;
    }
    if (!stick_control_amount.settled()) {
        stick_control_amount.step(dt, 15.f, 0.92f);
        animating = true;
    }
    if (stick_control_target) {
        stick_control_idle += dt;
        stick_step_age += dt;
        animating = true;
        // Out fast, back loose: the throw has to be legible at a glance, and
        // a stiff spring in both directions made it a twitch nobody could see.
        if (stick_throw_hold >= 0.f) {
            stick_throw_hold += dt;
            if (stick_throw_hold >= 0.10f) {
                stick_lean_x.to(0.f);
                stick_lean_y.to(0.f);
                stick_throw_hold = -1.f;
            }
        }
        if (!stick_lean_x.settled()) stick_lean_x.step(dt, 30.f, 0.62f);
        if (!stick_lean_y.settled()) stick_lean_y.step(dt, 30.f, 0.62f);
        // Always. An arrow is an impulse: the host consumes it and reports
        // nothing back, so there is no state here for the page to wait on and
        // no reason for it to outlive the gesture.
        if (stick_control_idle >= kStickIdleClose) {
            std::printf("CCP_UI|stick|closed_idle\n");
            dismiss_stick_control();
        }
    }
    if (composer_control_target) {
        composer_control_idle += dt;
        composer_control_step_age += dt;
        composer_lamp_age += dt;
        if (!composer_knob_angle.settled()) {
            composer_knob_angle.step(dt, 26.f, 0.62f);
            animating = true;
        }
        if (!composer_lamp_marker.settled()) {
            composer_lamp_marker.step(dt, 24.f, 0.8f);
            animating = true;
        }
        for (auto& level : composer_lamp_level) {
            if (!level.settled()) { level.step(dt, 18.f, 0.9f); animating = true; }
        }
        animating = true;
        // Once the session is engaged, nothing here closes the page. Every
        // timer tried for that -- a fixed wait after the click, then a test of
        // the host's silence -- was the device guessing at a state only Codex
        // knows, and each one cut a selection short. Codex says it plainly: it
        // blanks or whites out all six lamps when a control surface closes.
        // That frame takes the page down; so does Esc.
        //
        // The one case a timer can read correctly is the one where there is no
        // state to guess at: the dial was nudged, nothing was clicked, the host
        // lit nothing in reply. That is a stray turn, not a session, and it
        // should not leave a page sitting on the screen.
        if (!composer_control_engaged && composer_control_idle >= 3.f) {
            std::printf("CCP_UI|composer|closed_idle_unengaged\n");
            dismiss_composer_control_preview();
        }
    }

    for (float& p : cell_press) {
        if (p < kPressTime) { p += dt; animating = true; }
    }
    if (deck_entrance < kEntranceTime + kStaggerStep * kCellCount) {
        deck_entrance += dt;
        animating = true;
    }

    // A new event for the active slot supersedes its stale animation. Other
    // slots wait in FIFO order and claim the panel only after it has returned.
    const bool interruptible = (current == Screen::Deck || current == Screen::StatusDebug)
                            && !voice_target && !composer_control_target
                            && voice_amount.x <= 0.f;
    model::Announcement next_event;
    const uint32_t announcement_now = lgfx::millis();
    bool announcement_started_this_frame = false;
    const model::Announcement* candidate = nullptr;
    if (interruptible && announcing()) {
        for (uint8_t i = 0; i < model::state.announcement_count; ++i) {
            if (model::state.announcements[i].slot == announce_slot_index) {
                candidate = &model::state.announcements[i];
                break;
            }
        }
    } else if (interruptible && model::state.announcement_count > 0) {
        candidate = &model::state.announcements[0];
    }
    if (candidate) {
        audio::request_status(status_cue(candidate->status),
                              announcement_token(*candidate));
    }
    const bool candidate_audio_ready = !candidate
        || audio::status_ready(status_cue(candidate->status),
                               announcement_token(*candidate));
    if (interruptible && candidate_audio_ready && announcing()
        && model::take_announcement_for_slot(announce_slot_index,
                                             announcement_now, next_event)) {
        start_announcement(next_event);
        announcement_started_this_frame = true;
    } else if (interruptible && candidate_audio_ready && !announcing()
               && model::take_next_announcement(announcement_now, next_event)) {
        start_announcement(next_event);
        announcement_started_this_frame = true;
    }
    if (!announcement_started_this_frame
        && !voice_target && voice_amount.x <= 0.f
        && announce_age < kAnnounceLife) {
        announce_age = std::min(kAnnounceLife, announce_age + dt);
        if (announce_age >= kAnnounceLife && announce_fade_to_viewed) {
            const int slot = announce_slot_index;
            if (slot >= 0 && slot < model::state.task_count
                && model::state.tasks[slot].status == model::Status::Done) {
                // The queued event may be older than the latest thstatus frame.
                // Finish on the lamp colour visible in Codex now, not on the
                // read state captured before debounce and the animation.
                model::state.tasks[slot].unseen_done =
                    model::state.tasks[slot].color == lamp::kDoneUnseen
                    && !model::state.tasks[slot].locally_viewed_done;
                model::state.tasks[slot].completion_hold = false;
            }
            announce_fade_to_viewed = false;
            dirty = true;
        }
        if (announce_age >= kAnnounceLife)
            release_status_freeze_after_frame = true;
        animating = true;
    }

    // Semantic read state must not depend on an animation reaching one exact
    // frame. If a selected completion was marked viewed but its takeover was
    // cancelled, replaced, or ended without its fade flag, settle it once no
    // current or queued animation still owns that slot.
    if (!announcing()) {
        const int slot = model::state.selected;
        if (slot >= 0 && slot < model::state.task_count) {
            auto& task = model::state.tasks[slot];
            if (model::settle_viewed_completion(
                    task, model::has_announcement_for_slot(slot))) {
                std::printf("CCP_NATIVE|selected_completion_settled|%d|failsafe\n", slot);
                dirty = true;
            }
        }
    }

    for (float& flash : setting_flash) {
        if (flash < motion::kFast) { flash += dt; animating = true; }
    }

    if (model::state.link == model::Link::Offline) animating = true;

    // Running slots have a deliberately slow, asynchronous gradient. Idle
    // decks remain fully static and therefore keep the existing render budget.
    for (int i = 0; i < std::min(model::state.task_count, kCellCount); ++i) {
        const auto& task = model::state.tasks[i];
        if (task.present && task.status == model::Status::Running) {
            animating = true;
            break;
        }
    }


    if (toast_title[0]) {
        toast_in.step(dt);
        if (!toast_out) {
            toast_life -= dt;
            if (toast_life <= 0.f) { toast_out = true; toast_in.restart(4.f); }
        } else if (!toast_in.running()) {
            toast_title[0] = 0;
        }
        animating = true;
    }

    if (!animating && !dirty) return;
    // A dark panel stops paying for pixels, but only once the backlight has
    // actually reached zero -- otherwise the ramp down shows a stale frame.
    if (power_state == 2 && applied_brightness == 0) { dirty = false; return; }
    dirty = false;
    render();
}

void relayout()
{
    auto& s = model::state;
    if (s.selected >= s.task_count) s.selected = std::max(0, s.task_count - 1);
    const model::Task* task = model::selected_task();
    // A status change on the selected task replays the flap; a plain refresh
    // must not, or the panel would twitch on every host poll.
    const model::Status fresh = task ? task->status : model::Status::Idle;
    if (fresh != curr_status) {
        prev_status = curr_status;
        curr_status = fresh;
        since_status = 0.f;    // repaint the band, nothing else
    }
    retarget_deck(true);
}

void settings_move(int delta)
{
    settings_row = (settings_row + delta + kSettingsRows) % kSettingsRows;
    settings_marker.to(static_cast<float>(settings_row * kSettingsPitch));
    dirty = true;
}

SettingsRow settings_focus() { return static_cast<SettingsRow>(settings_row); }

void debug_settings_move(int delta)
{
    debug_settings_row = (debug_settings_row + delta + kDebugSettingsRows)
                       % kDebugSettingsRows;
    debug_settings_marker.to(
        static_cast<float>(debug_settings_row * kDebugSettingsPitch));
    dirty = true;
}

void previews_move(int delta)
{
    previews_row = (previews_row + delta + kPreviewsRows) % kPreviewsRows;
    previews_marker.to(static_cast<float>(previews_row * kDebugSettingsPitch));
    invalidate();
}

PreviewsRow previews_focus()
{
    return static_cast<PreviewsRow>(previews_row);
}

DebugSettingsRow debug_settings_focus()
{
    return static_cast<DebugSettingsRow>(debug_settings_row);
}

void show_developer_preview(DeveloperPreview preview)
{
    developer_preview = preview;
    if (preview == DeveloperPreview::Splash) boot_ramp.restart(1.1f);
    if (preview == DeveloperPreview::Control) composer_control_amount.snap(1.f);
    if (preview == DeveloperPreview::Stick) {
        stick_control_amount.snap(1.f);
        stick_step_dir = 0;
        stick_step_age = 0.08f;
        stick_lean_x.snap(0.8f);
        stick_lean_y.snap(0.f);
    }
    dirty = true;
}

void close_developer_preview()
{
    developer_preview = DeveloperPreview::None;
    composer_control_amount.snap(0.f);
    dirty = true;
}

bool developer_preview_active() { return developer_preview != DeveloperPreview::None; }

void debug_move(int delta)
{
    debug_row = (debug_row + delta + kDebugRows) % kDebugRows;
    debug_marker.to(static_cast<float>(debug_row * kDebugPitch));
    dirty = true;
}

void chime_move(int dx, int dy)
{
    int index = model::state.startup_chime;
    const int row = index / kChimeCols;
    const int col = index % kChimeCols;
    const int next_row = (row + dy + 2) % 2;
    const int next_col = (col + dx + kChimeCols) % kChimeCols;
    model::state.startup_chime = static_cast<uint8_t>(next_row * kChimeCols + next_col);
    chime_marker_x.to(static_cast<float>(next_col * (kChimeCellW + kChimeGap)));
    chime_marker_y.to(static_cast<float>(next_row * (kChimeCellH + kChimeGap)));
    dirty = true;
}

uint8_t chime_focus() { return model::state.startup_chime; }

bool debug_adjust(int delta)
{
    if (debug_row == 0) {
        static constexpr uint16_t options[] = {100, 200, 300, 500};
        int index = 0;
        for (int i = 0; i < 4; ++i) {
            if (options[i] == model::state.status_debounce_ms) { index = i; break; }
        }
        index = (index + delta + 4) % 4;
        model::state.status_debounce_ms = options[index];
    } else if (debug_row == 1) {
        model::state.status_audio_offset_ms = static_cast<int16_t>(std::clamp(
            static_cast<int>(model::state.status_audio_offset_ms) + delta * 25,
            -300, 300));
        std::printf("CCP_SETTING|audio_offset_ms=%d|range=-300:300\n",
                    static_cast<int>(model::state.status_audio_offset_ms));
    } else {
        return false;
    }
    dirty = true;
    return true;
}

bool debug_run()
{
    if (debug_row <= 1) return debug_adjust(1);
    static constexpr model::Status statuses[kDebugRows - 2] = {
        model::Status::Running, model::Status::NeedsInput, model::Status::Done,
        model::Status::Error, model::Status::Idle
    };
    const int slot = std::clamp(model::state.selected, 0, 5);
    const model::Task* task = model::selected_task();
    const model::Status previous_status = task ? task->status : model::Status::Idle;
    const bool previous_unseen = task && task->unseen_done;
    const model::Status status = statuses[debug_row - 2];
    const bool event_unseen = status == model::Status::Done;
    model::queue_announcement(slot, status, previous_status,
                              event_unseen, event_unseen, previous_unseen,
                              lgfx::millis());
    wake();
    dirty = true;
    return false;
}

void flash_setting(int which)
{
    if (which < 0 || which > 2) return;
    setting_flash[which] = 0.f;
    dirty = true;
}

}  // namespace ui
