// ESP32_TextTransform_Scenes.ino
//
// Segmented tiled framebuffer demo with creative scenes and transitions.
// Uses PROGMEM tables produced by the Python generator (log2_table_q8, exp2_table_q8, sin_table_q15, cos_table_q15,
// perspective_scale_table_q8, GLYPH_BITMAPS, GLYPH_CHAR_LIST, GLYPH_WIDTH, GLYPH_HEIGHT, GLYPH_COUNT).
//
// Requirements:
//  - TFT_eSPI (configured for your display; enable DMA for best throughput).
//  - arduino_tables.h (+ arduino_tables.c if you generated with --emit-c) in sketch folder.
//  - Generator must have used LOG_Q = 8 and SIN_Q = 15 by default; if you changed that, update LOG_Q/SIN_Q below.
//
// This demo renders multiple scenes: scrolling, orbit, wave, rain, and an explode transition that scatters tiles.
//
// Tune top-level parameters as needed.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>

TFT_eSPI tft = TFT_eSPI();

// include generated tables & glyphs
#include "arduino_tables.h"

// ---------------------- Config ----------------------
#define LOG_Q 8
#define LOG_SCALE (1<<LOG_Q)
#define SIN_Q 15
#define SIN_SIZE 512

// Tile size (tune: 32, 48, 64, 96). 64 recommended.
const int TILE_W = 64;
const int TILE_H = 64;

const uint32_t BENCH_PRINT_FRAMES = 45; // how often to print bench stats

// Scenes durations (ms)
const uint32_t SCENE_DURATION_MS = 8000;
const uint32_t TRANSITION_MS = 900; // explosion transition

// Animation parameters
const float GLOBAL_ROT_SPEED = 0.02f;
const float SCROLL_SPEED = 0.8f; // pixels/ frame for text scroll

// ---------------------- Tables/external symbols (from generated header) ----------------------
extern const uint8_t msb_table[];         // size 256
extern const uint16_t log2_table_q8[];    // size 256
extern const uint16_t exp2_table_q8[];    // size 256
extern const int16_t sin_table_q15[];     // sin table (Q15)
extern const int16_t cos_table_q15[];     // cos table (Q15)
extern const uint16_t perspective_scale_table_q8[]; // 256 entries

extern const char GLYPH_CHAR_LIST[];      // char list
extern const uint8_t GLYPH_BITMAPS[];     // flattened glyph bitmaps (count * GW)
extern const uint8_t GLYPH_WIDTH;
extern const uint8_t GLYPH_HEIGHT;
extern const uint16_t GLYPH_COUNT;

// ---------------------- Fast log/exp multiply pipeline ----------------------
static inline uint8_t fast_msb16(uint16_t v) {
  if (v >> 8) return 8 + msb_table[v >> 8];
  return msb_table[v & 0xFF];
}

static inline void normalize_to_mant8(uint16_t v, uint8_t &mant8, int8_t &e_out) {
  if (v == 0) { mant8 = 0; e_out = -127; return; }
  uint8_t e = fast_msb16(v);
  uint32_t tmp = ((uint32_t)v << (15 - e)) >> 8;
  mant8 = (uint8_t)tmp;
  e_out = (int8_t)e;
}

static inline int32_t fast_log2_q8_8(uint16_t v) {
  if (v == 0) return INT32_MIN;
  uint8_t m; int8_t e;
  normalize_to_mant8(v, m, e);
  uint16_t logm = log2_table_q8[m];
  return (((int32_t)(e - 7) << LOG_Q) + (int32_t)logm);
}

static inline uint32_t fast_exp2_from_q8_8(int32_t log_q8_8) {
  if (log_q8_8 <= -32768) return 0;
  int32_t integer = log_q8_8 >> LOG_Q;
  uint8_t frac = (uint8_t)(log_q8_8 & 0xFF);
  uint16_t exp_frac = exp2_table_q8[frac];
  if (integer >= 32) {
    return 0xFFFFFFFFUL;
  } else if (integer >= 8) {
    uint32_t val = (uint32_t)exp_frac << (integer - 8);
    return val;
  } else if (integer >= 0) {
    uint32_t val = (uint32_t)exp_frac >> (8 - integer);
    return val;
  } else {
    int shift = -integer;
    if (shift >= 24) return 0;
    uint32_t val = ((uint32_t)exp_frac) >> (8 + shift);
    return val;
  }
}

static inline uint32_t fast_log_mul_u16(uint16_t a, uint16_t b) {
  if (a == 0 || b == 0) return 0;
  int32_t la = fast_log2_q8_8(a);
  int32_t lb = fast_log2_q8_8(b);
  int32_t sum = la + lb;
  return fast_exp2_from_q8_8(sum);
}

// ---------------------- Sin/cos helpers ----------------------
static inline uint16_t angle_to_index(float angle) {
  while (angle < 0) angle += 2.0f * PI;
  while (angle >= 2.0f * PI) angle -= 2.0f * PI;
  return (uint16_t)((angle / (2.0f * PI)) * (float)SIN_SIZE);
}
static inline int16_t sin_q15(uint16_t idx) { return sin_table_q15[idx % SIN_SIZE]; }
static inline int16_t cos_q15(uint16_t idx) { return cos_table_q15[idx % SIN_SIZE]; }

// ---------------------- Glyph utilities ----------------------
static int glyph_index_for_char(char c) {
  for (uint16_t i = 0; i < GLYPH_COUNT; ++i) if (GLYPH_CHAR_LIST[i] == c) return i;
  return -1;
}

// ---------------------- Tile structures ----------------------
int tiles_x, tiles_y;
uint16_t *tile_buf = nullptr; // allocated in DMA-capable memory
bool *tile_dirty = nullptr;

struct TileState {
  float vx, vy;    // velocity for explosion
  float angVel;    // spin during explosion
  float offsetX, offsetY; // current offset applied to tile when animating explode
  bool active;
} ;
TileState *tile_state = nullptr;

// ---------------------- Scene manager ----------------------
enum Scene {
  SCENE_SCROLL,
  SCENE_ORBIT,
  SCENE_WAVE,
  SCENE_RAIN,
  SCENE_EXPLODE_TRANSITION, // special transition that scatters tiles then assembles
  SCENE_COUNT
};

Scene current_scene = SCENE_SCROLL;
Scene scene_before_transition = SCENE_SCROLL; // <<< FIX: Added to track state
uint32_t scene_start_ms = 0;
uint32_t last_bench_print = 0;

// helper to trigger scene change
void next_scene() {
  // if not currently exploding, start transition
  if (current_scene != SCENE_EXPLODE_TRANSITION) {
    scene_before_transition = current_scene; // <<< FIX: Store the scene we are leaving
    
    // initialize tile velocities for explosion
    for (int ty=0; ty<tiles_y; ++ty) for (int tx=0; tx<tiles_x; ++tx) {
      int idx = ty*tiles_x + tx;
      // choose velocity based on tile center relative to screen center
      float cx = (tx * TILE_W) + TILE_W*0.5f;
      float cy = (ty * TILE_H) + TILE_H*0.5f;
      float dx = cx - (tft.width()*0.5f);
      float dy = cy - (tft.height()*0.5f);
      float dist = sqrt(dx*dx + dy*dy) + 1e-6f;
      float normx = dx / dist;
      float normy = dy / dist;
      tile_state[idx].vx = normx * (50.0f + random(0,50)); // px/sec-ish (we'll scale by time)
      tile_state[idx].vy = normy * (50.0f + random(0,50));
      tile_state[idx].angVel = ((random(-100,100))/100.0f) * 4.0f;
      tile_state[idx].offsetX = 0;
      tile_state[idx].offsetY = 0;
      tile_state[idx].active = true;
    }
    current_scene = SCENE_EXPLODE_TRANSITION;
    scene_start_ms = millis();
  } else {
    // when transition already in progress, ignore
  }
}

// ---------------------- Demo content ----------------------
const char* demo_rows[] = {
  "HELLO ESP32",
  "TABLE-DRIVEN TRANSFORMS",
  "TILED FRAMEBUFFER DEMO"
};
const uint8_t DEMO_ROWS = sizeof(demo_rows)/sizeof(demo_rows[0]);

// ---------------------- Benchmark accumulators ----------------------
uint64_t bench_total_time_us = 0;
uint32_t bench_frames = 0;
uint32_t bench_mul_samples = 0;
double bench_mul_error_rel_sum = 0.0;
uint32_t bench_mul_error_max = 0;

// record mul error relative
static inline void bench_record_mul(uint16_t a, uint16_t b) {
  uint32_t exact = (uint32_t)a * (uint32_t)b;
  uint32_t approx = fast_log_mul_u16(a,b);
  uint32_t abs_err = (approx > exact) ? (approx - exact) : (exact - approx);
  bench_mul_samples++;
  double rel = exact ? ((double)abs_err / (double)exact) : 0.0;
  bench_mul_error_rel_sum += rel;
  if (abs_err > bench_mul_error_max) bench_mul_error_max = abs_err;
}

// ---------------------- Small helpers ----------------------
static inline void clear_tile_buf(uint16_t color = 0x0000) {
  uint32_t n = (uint32_t)TILE_W * (uint32_t)TILE_H;
  for (uint32_t i = 0; i < n; ++i) tile_buf[i] = color;
}

static inline bool bbox_intersect(int16_t a_left, int16_t a_top, int16_t a_right, int16_t a_bottom,
                                  int16_t b_left, int16_t b_top, int16_t b_right, int16_t b_bottom) {
  return !(a_right < b_left || a_bottom < b_top || b_right < a_left || b_bottom < a_top);
}

// ---------------------- Rasterization for a single glyph (writes into global tile_buf) ----------------------
void rasterize_glyph_to_tile(int gidx, int16_t glyph_cx, int16_t glyph_cy,
                             float scale_f, float angle_rad, int16_t tile_x0, int16_t tile_y0, uint16_t color=0xFFFF)
{
  // gidx must be valid
  if (gidx < 0 || gidx >= GLYPH_COUNT) return;
  const uint8_t gw = GLYPH_WIDTH;
  const uint8_t gh = GLYPH_HEIGHT;

  // combined scaling: scale_f (float) modulated by perspective table (index by glyph_cy)
  uint16_t scale_q8 = (uint16_t)round(scale_f * (float)(1<<LOG_Q));
  uint16_t persp_idx = (uint16_t)min(255, max(0, (int)round((glyph_cy / (float)tft.height()) * 255.0f)));
  uint16_t persp_q8 = perspective_scale_table_q8[persp_idx];
  uint32_t combined_q = fast_log_mul_u16(scale_q8, persp_q8);
  bench_record_mul(scale_q8, persp_q8);
  uint16_t combined_q8 = (uint16_t)(combined_q >> LOG_Q); // Q8.8 scale

  uint16_t aidx = angle_to_index(angle_rad);
  int16_t cosv = cos_q15(aidx);
  int16_t sinv = sin_q15(aidx);

  // iterate columns then rows
  for (uint8_t col = 0; col < gw; ++col) {
    uint8_t colbyte = GLYPH_BITMAPS[gidx * gw + col];
    if (colbyte == 0) continue;
    for (uint8_t row = 0; row < gh; ++row) {
      if (!(colbyte & (1 << row))) continue;

      int16_t sx = (int16_t)col - (gw/2);
      int16_t sy = (int16_t)row - (gh/2);

      // Q8.8 coordinates
      int32_t sx_q8 = ((int32_t)sx) << LOG_Q;
      int32_t sy_q8 = ((int32_t)sy) << LOG_Q;

      // absolute inputs for fast_log_mul (clamp to 65535)
      uint16_t asx = (uint16_t)min((uint32_t)abs(sx_q8), (uint32_t)65535);
      uint16_t asy = (uint16_t)min((uint32_t)abs(sy_q8), (uint32_t)65535);

      // fast_log_mul returns approx product of integers.
      // since both inputs are Q8.8, product is Q16.16. Shift >>8 to get Q16.8 result.
      uint32_t sx_scaled = fast_log_mul_u16(asx, combined_q8) >> LOG_Q;
      uint32_t sy_scaled = fast_log_mul_u16(asy, combined_q8) >> LOG_Q;
      bench_record_mul(asx, combined_q8);
      bench_record_mul(asy, combined_q8);

      int32_t sxs = (sx_q8 < 0) ? -(int32_t)sx_scaled : (int32_t)sx_scaled;
      int32_t sys = (sy_q8 < 0) ? -(int32_t)sy_scaled : (int32_t)sy_scaled;

      // rotation (Q8.8)
      int32_t rx_q8 = ( (sxs * (int32_t)cosv) - (sys * (int32_t)sinv) ) >> SIN_Q;
      int32_t ry_q8 = ( (sxs * (int32_t)sinv) + (sys * (int32_t)cosv) ) >> SIN_Q;

      int16_t fx = glyph_cx + (int16_t)(rx_q8 >> LOG_Q);
      int16_t fy = glyph_cy + (int16_t)(ry_q8 >> LOG_Q);

      // check intersection with tile
      if (fx >= tile_x0 && fx < tile_x0 + TILE_W && fy >= tile_y0 && fy < tile_y0 + TILE_H) {
        int tx = fx - tile_x0;
        int ty = fy - tile_y0;
        uint32_t index = (uint32_t)ty * TILE_W + (uint32_t)tx;
        tile_buf[index] = color;
        // <<< FIX: Set the global dirty flag for the tile this pixel belongs to
        tile_dirty[ (tile_y0 / TILE_H) * tiles_x + (tile_x0 / TILE_W) ] = true;
      }
    }
  }
}

// ---------------------- Pre-cull test: compute transformed glyph bounding box as float ----------------------
void glyph_transformed_bbox_float(int16_t gx, int16_t gy, float scale_f, float angle_rad,
                                  float &minx, float &miny, float &maxx, float &maxy)
{
  // compute four corners of glyph rectangle (centered) and transform
  const float hw = (GLYPH_WIDTH / 2.0f);
  const float hh = (GLYPH_HEIGHT / 2.0f);
  float cosA = cosf(angle_rad);
  float sinA = sinf(angle_rad);
  // corners relative to center
  float corners[4][2] = {
    {-hw, -hh},
    { hw, -hh},
    { hw,  hh},
    {-hw,  hh}
  };
  minx = 1e9; miny = 1e9; maxx = -1e9; maxy = -1e9;
  // perspective scale: approximate via perspective table (use center y)
  uint16_t pidx = (uint16_t)min(255, max(0, (int)round((gy / (float)tft.height()) * 255.0f)));
  float persp = perspective_scale_table_q8[pidx] / (float)(1<<LOG_Q);
  float s = scale_f * persp;
  for (int i=0;i<4;++i) {
    float x = corners[i][0] * s;
    float y = corners[i][1] * s;
    // rotate
    float xr = x * cosA - y * sinA;
    float yr = x * sinA + y * cosA;
    float wx = gx + xr;
    float wy = gy + yr;
    if (wx < minx) minx = wx;
    if (wy < miny) miny = wy;
    if (wx > maxx) maxx = wx;
    if (wy > maxy) maxy = wy;
  }
}

// ---------------------- Render loop: tiled rendering with scene behavior ----------------------
void render_tiles_frame(uint32_t t_ms) {
  uint32_t t0 = micros();

  // clear dirty flags
  for (int i=0;i<tiles_x*tiles_y;++i) { tile_dirty[i] = false; }
  // clear tile buffers once per tile when used; but we'll clear on demand before rendering tile

  // compute scene-relative parameters
  uint32_t scene_elapsed = t_ms - scene_start_ms;
  bool in_transition = (current_scene == SCENE_EXPLODE_TRANSITION);
  float scene_progress = min(1.0f, scene_elapsed / (float)SCENE_DURATION_MS);

  // global animation (used by multiple scenes)
  static float global_angle = 0.0f;
  global_angle += GLOBAL_ROT_SPEED;

  // per-row base scales & angles function
  auto per_char_scale = [&](int row, int idx, uint32_t frame_ms)->float {
    // different behaviors per scene
    float t = frame_ms * 0.001f;
    switch (current_scene) {
      case SCENE_SCROLL:
        return 0.9f + 0.4f * sinf( (float)idx*0.4f + t*1.2f);
      case SCENE_ORBIT:
        return 0.8f + 0.6f * (0.5f + 0.5f * sinf(t + idx*0.2f));
      case SCENE_WAVE:
        return 0.9f + 0.9f * sinf(t*1.6f + idx*0.6f);
      case SCENE_RAIN:
        return 0.9f + 0.2f * sinf(t + idx*0.1f);
      default:
        return 1.0f;
    }
  };

  auto per_char_angle = [&](int row, int idx, uint32_t frame_ms)->float {
    float t = frame_ms * 0.001f;
    switch (current_scene) {
      case SCENE_SCROLL:
        return 0.1f * sinf(t*0.5f + idx*0.3f);
      case SCENE_ORBIT:
        return global_angle + (idx * 0.12f);
      case SCENE_WAVE:
        return 0.4f * sinf(t + idx*0.2f);
      case SCENE_RAIN:
        return 0.2f * cosf(t*2.0f + idx*0.1f);
      default:
        return 0.0f;
    }
  };

  // loop tiles
  for (int ty = 0; ty < tiles_y; ++ty) {
    int16_t ty0 = ty * TILE_H;
    for (int tx = 0; tx < tiles_x; ++tx) {
      int16_t tx0 = tx * TILE_W;
      int tile_index = ty * tiles_x + tx;

      // clear tile buffer and dirty flag; will set dirty when any pixel written
      // bool local_dirty = false; // <<< FIX: Removed, not needed
      // Clear the tile buffer (must be in DMA memory)
      clear_tile_buf(0x0000);
      // tile_dirty[tile_index] = false; // <<< FIX: Removed, already cleared in outer loop

      // If we are in explode transition, compute offsets for this tile
      float tile_offset_x = 0.0f, tile_offset_y = 0.0f;
      if (in_transition) {
        // compute progress in transition (0..1)
        float tt = (millis() - scene_start_ms) / (float)TRANSITION_MS;
        if (tt > 1.0f) tt = 1.0f;
        TileState &ts = tile_state[tile_index];
        // explode outward quickly, then come back. Use easing: out-quad for explode, in-quad for reassemble
        // if first half of transition, move out; else move in
        if (tt <= 0.6f) {
          float e = tt / 0.6f;
          tile_offset_x = ts.vx * (e*e) * 0.1f * TRANSITION_MS*0.001f; // scaled
          tile_offset_y = ts.vy * (e*e) * 0.1f * TRANSITION_MS*0.001f;
        } else {
          float e = (tt - 0.6f) / 0.4f;
          float inv = 1.0f - e;
          tile_offset_x = ts.vx * (inv*inv) * 0.1f * TRANSITION_MS*0.001f;
          tile_offset_y = ts.vy * (inv*inv) * 0.1f * TRANSITION_MS*0.001f;
        }
      }

      // For each row & each character, determine if glyph intersects tile (fast float bbox), then rasterize if so
      for (int r = 0; r < DEMO_ROWS; ++r) {
        const char* s = demo_rows[r];
        int len = strlen(s);

        // compute baseline for row (centered)
        int16_t baseline_x = tft.width()/2;
        int16_t baseline_y = 30 + r * 86;

        for (int ci = 0; ci < len; ++ci) {
          char ch = s[ci];
          int gidx = glyph_index_for_char(ch);
          if (gidx < 0) continue;

          // compute glyph center
          int16_t glyph_cx = baseline_x - (len * (GLYPH_WIDTH + 1))/2 + ci * (GLYPH_WIDTH + 1) + GLYPH_WIDTH/2;
          int16_t glyph_cy = baseline_y;

          // modify glyph center based on current scene movement (example: scroll or orbit)
          if (current_scene == SCENE_SCROLL) {
            // horizontally scroll entire row
            float scroll_x = fmod((float)millis() * (SCROLL_SPEED * 0.05f) + (ci * 6.0f), (float)(tft.width()+200));
            glyph_cx = (int16_t)( (glyph_cx + (int)scroll_x) - tft.width()/2 );
          } else if (current_scene == SCENE_ORBIT) {
            float t = (millis() - scene_start_ms) * 0.001f;
            float radius = 40.0f + 4.0f * ci;
            glyph_cx += (int16_t)( cosf(t*0.9f + ci*0.25f) * radius );
            glyph_cy += (int16_t)( sinf(t*1.1f + ci*0.2f) * radius );
          } else if (current_scene == SCENE_RAIN) {
            float t = (millis() - scene_start_ms) * 0.001f;
            glyph_cy += (int16_t)( 150.0f * fmod( (t*0.6f + ci*0.01f), 1.0f) );
          }

          // get scale and angle
          float sc = per_char_scale(r, ci, millis());
          float ang = per_char_angle(r, ci, millis());

          // <<< FIX: CULLING LOGIC >>>
          // 1. Calculate the glyph's *final* offset position
          float glyph_offset_cx = (float)glyph_cx + tile_offset_x;
          float glyph_offset_cy = (float)glyph_cy + tile_offset_y;

          // 2. compute transformed bbox *at the offset position*
          float minx, miny, maxx, maxy;
          glyph_transformed_bbox_float(glyph_offset_cx, glyph_offset_cy, sc, ang, minx, miny, maxx, maxy);

          // expand bbox a little
          minx -= 1.0f; miny -= 1.0f; maxx += 1.0f; maxy += 1.0f;

          // 3. Test intersection of the offset glyph bbox against the *stationary* tile
          if (!bbox_intersect((int16_t)floor(minx), (int16_t)floor(miny), (int16_t)ceil(maxx), (int16_t)ceil(maxy),
                              tx0, ty0, (int16_t)(tx0 + TILE_W), (int16_t)(ty0 + TILE_H))) {
            continue; // definitely outside tile
          }
          // <<< END CULLING FIX >>>


          // If intersecting, rasterize glyph into this tile (apply the tile_offset to push pixels visually)
          // We rasterize at glyph center offset by tile_offset and by tile origin
          rasterize_glyph_to_tile(gidx,
                                  (int16_t)round(glyph_offset_cx), // <<< FIX: Pass offset coords
                                  (int16_t)round(glyph_offset_cy), // <<< FIX: Pass offset coords
                                  sc, ang, tx0, ty0, 0xFFFF);
          // local_dirty = local_dirty || tile_dirty[tile_index]; // <<< FIX: Removed
        } // chars
      } // rows

      // Push tile only if dirty (optimizes for many empty tiles)
      if (tile_dirty[tile_index]) { // <<< FIX: Check global flag directly
        // pushImage can accept buffer in DMA memory
        // compute actual tile width/height for edges
        int16_t push_w = min((int)TILE_W, tft.width() - tx0);
        int16_t push_h = min((int)TILE_H, tft.height() - ty0);
        tft.pushImage(tx0, ty0, push_w, push_h, tile_buf);
      } else {
        // optionally push background for completeness:
        // tft.fillRect(tx0, ty0, min(TILE_W, tft.width()-tx0), min(TILE_H, tft.height()-ty0), TFT_BLACK);
      }
    } // tx
  } // ty

  uint32_t t1 = micros();
  uint32_t dt = t1 - t0;
  bench_total_time_us += dt;
  bench_frames++;

  // handle scene timing and transitions
  if (!in_transition && (millis() - scene_start_ms) > SCENE_DURATION_MS) {
    // start transition
    next_scene(); // this will set current_scene to EXPLODE_TRANSITION
  } else if (in_transition) {
    if ((millis() - scene_start_ms) > TRANSITION_MS) {
      // complete transition: advance to next logical scene

      // <<< FIX: Advance from the scene *before* the transition started
      current_scene = (Scene)(((int)scene_before_transition + 1) % SCENE_COUNT);
      if (current_scene == SCENE_EXPLODE_TRANSITION) {
        // skip the transition scene itself
        current_scene = (Scene)(((int)current_scene + 1) % SCENE_COUNT);
      }
      scene_start_ms = millis();
      
      // reset tile_state active flags
      for (int i=0;i<tiles_x*tiles_y;++i) tile_state[i].active = false;
    }
  }

  // bench printing
  if ((millis() - last_bench_print) > 1000) {
    last_bench_print = millis();
    float avg_frame_ms = (bench_total_time_us / (float)max(1u,bench_frames)) / 1000.0f;
    float avg_rel_err = bench_mul_samples ? (bench_mul_error_rel_sum / (double)bench_mul_samples) : 0.0;
    Serial.printf("Scene %d frame=%u avg_ms=%.2f fps=%.1f free_heap=%u mul_samples=%u avg_rel_err=%.6f max_abs_err=%u\n",
                  (int)current_scene, bench_frames, avg_frame_ms, 1000.0f/avg_frame_ms,
                  ESP.getFreeHeap(), bench_mul_samples, (float)avg_rel_err, bench_mul_error_max);
    // reset some accumulators slowly (not mandatory)
    bench_total_time_us = 0;
    bench_frames = 0;
    bench_mul_samples = 0;
    bench_mul_error_rel_sum = 0.0;
    bench_mul_error_max = 0;
  }
}

// ---------------------- Setup ----------------------
void setup() {
  Serial.begin(115200);
  delay(50);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // tiles
  tiles_x = (tft.width() + TILE_W - 1) / TILE_W;
  tiles_y = (tft.height() + TILE_H - 1) / TILE_H;

  // allocate tile buffer in DMA-capable memory
  size_t bufBytes = (size_t)TILE_W * TILE_H * sizeof(uint16_t);
  tile_buf = (uint16_t*)heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  if (!tile_buf) {
    Serial.printf("Failed to alloc tile_buf (%u bytes)\n", (unsigned)bufBytes);
    while (1) delay(1000);
  }

  tile_dirty = (bool*)malloc(sizeof(bool) * tiles_x * tiles_y);
  tile_state = (TileState*)malloc(sizeof(TileState) * tiles_x * tiles_y);
  if (!tile_dirty || !tile_state) {
    Serial.println("Failed to alloc tile arrays");
    while (1) delay(1000);
  }
  // init tile states
  for (int i=0;i<tiles_x*tiles_y;++i) {
    tile_dirty[i] = false;
    tile_state[i].active = false;
    tile_state[i].vx = tile_state[i].vy = tile_state[i].offsetX = tile_state[i].offsetY = 0;
    tile_state[i].angVel = 0;
  }

  scene_start_ms = millis();
  last_bench_print = millis();
  Serial.printf("Display %dx%d tiles %dx%d tile_bytes=%u\n", tft.width(), tft.height(), tiles_x, tiles_y, (unsigned)bufBytes);
}

// ---------------------- Main loop ----------------------
void loop() {
  uint32_t now = millis();
  render_tiles_frame(now);
  // small sleep to yield; adjust for higher/lower frame rate
  delay(8);
}
