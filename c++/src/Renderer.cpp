#include "Renderer.hpp"
#include "Simulation.hpp"
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// ---------------------------------------------------------------------------
// Palette
//
// One place, so the world and the UI stay the same battle. Team identity is
// carried by hue and nothing else: shape says unit type, brightness says
// health, and desaturation says the squad has broken. Overloading any one
// channel twice is what made the old view unreadable at a distance.
// ---------------------------------------------------------------------------
namespace pal {
constexpr Color kGroundDark = { 33,  39,  31, 255 };
constexpr Color kGroundLite = { 61,  69,  50, 255 };
constexpr Color kGroundDirt = { 74,  66,  48, 255 };
constexpr Color kBorder     = {128, 142, 160, 255 };

constexpr Color kTeamA      = { 92, 154, 236, 255 };  // steel blue
constexpr Color kTeamB      = {218,  96,  66, 255 };  // rust red

constexpr Color kArrow      = {244, 218, 156, 255 };
constexpr Color kBlood      = { 74,  20,  20, 255 };
constexpr Color kSpark      = {255, 214, 138, 255 };

constexpr Color kWallSide   = { 52,  46,  50, 255 };
constexpr Color kWallTop    = { 98,  76,  62, 255 };  // clay tile, warm against the turf
constexpr Color kWallEdge   = { 34,  32,  40, 255 };
constexpr Color kCanopy     = { 44,  74,  42, 255 };
constexpr Color kCanopyLit  = { 71, 105,  60, 255 };
constexpr Color kTrunk      = { 46,  36,  28, 255 };
}  // namespace pal

// The sun sits up and to the left, so every shadow in the scene falls down
// and to the right. Sizes differ, direction never does -- inconsistent shadow
// direction is what makes fake depth read as noise instead of as height.
static constexpr float kLightX = 1.0f;
static constexpr float kLightY = 1.35f;

// ---------------------------------------------------------------------------
// Small colour helpers. Written out rather than pulled from raylib so the
// blending is the same on every raylib version this has been built against.
// ---------------------------------------------------------------------------
static inline uint8_t u8(float v) {
    return (uint8_t)std::clamp(v, 0.0f, 255.0f);
}
static inline Color shade(Color c, float f) {
    return Color{ u8(c.r * f), u8(c.g * f), u8(c.b * f), c.a };
}
static inline Color mix(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return Color{ u8(a.r + (b.r - a.r) * t), u8(a.g + (b.g - a.g) * t),
                  u8(a.b + (b.b - a.b) * t), u8(a.a + (b.a - a.a) * t) };
}
static inline Color alpha(Color c, float a) {
    return Color{ c.r, c.g, c.b, u8(255.0f * std::clamp(a, 0.0f, 1.0f)) };
}
// Pulls a colour toward its own luminance. A broken squad loses its team
// hue without losing its silhouette, which is exactly what "these men are no
// longer fighting for anyone" should look like.
static inline Color desaturate(Color c, float t) {
    const float lum = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
    return mix(c, Color{ u8(lum), u8(lum), u8(lum), c.a }, t);
}

// ---------------------------------------------------------------------------
// Procedural ground
//
// Generated once into a small texture and stretched over the world with
// bilinear filtering. A flat fill made every distance judgement impossible
// (nothing to parallax against while panning) and made the field look like a
// whiteboard rather than ground an army is crossing.
// ---------------------------------------------------------------------------
static Texture2D gGround    = { 0 };
static Texture2D gGrain     = { 0 };
static bool      gHasGround = false;

// World pixels covered by one repeat of the grain texture. The macro texture
// is stretched over thousands of world pixels and turns to soup the moment you
// zoom past about 1x; this second layer is tiled at a fixed WORLD scale, so it
// gains detail as you zoom in instead of losing it.
static constexpr float kGrainTileWorld = 64.0f;

static float hash2(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xFFFFFFu) / (float)0xFFFFFFu;
}

static float valueNoise(float x, float y, uint32_t seed) {
    const int xi = (int)std::floor(x);
    const int yi = (int)std::floor(y);
    const float xf = x - (float)xi;
    const float yf = y - (float)yi;
    const float u = xf * xf * (3.0f - 2.0f * xf);
    const float v = yf * yf * (3.0f - 2.0f * yf);
    const float a = hash2(xi,     yi,     seed);
    const float b = hash2(xi + 1, yi,     seed);
    const float c = hash2(xi,     yi + 1, seed);
    const float d = hash2(xi + 1, yi + 1, seed);
    const float top = a + (b - a) * u;
    const float bot = c + (d - c) * u;
    return top + (bot - top) * v;
}

// Same noise, but the lattice wraps every `period` cells, which is what makes
// the grain texture tile without a seam. A non-wrapping noise tiled across the
// field draws a visible grid, and a visible grid is the one artefact a ground
// texture must not have.
static float valueNoiseTiled(float x, float y, int period, uint32_t seed) {
    const int xi = (int)std::floor(x);
    const int yi = (int)std::floor(y);
    const float xf = x - (float)xi;
    const float yf = y - (float)yi;
    const float u = xf * xf * (3.0f - 2.0f * xf);
    const float v = yf * yf * (3.0f - 2.0f * yf);
    auto wrap = [period](int i) { const int m = i % period; return m < 0 ? m + period : m; };
    const float a = hash2(wrap(xi),     wrap(yi),     seed);
    const float b = hash2(wrap(xi + 1), wrap(yi),     seed);
    const float c = hash2(wrap(xi),     wrap(yi + 1), seed);
    const float d = hash2(wrap(xi + 1), wrap(yi + 1), seed);
    const float top = a + (b - a) * u;
    const float bot = c + (d - c) * u;
    return top + (bot - top) * v;
}

static float fbmTiled(float x, float y, int basePeriod, uint32_t seed) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    int period = basePeriod;
    float freq = 1.0f;
    for (int o = 0; o < 3; ++o) {
        sum  += valueNoiseTiled(x * freq, y * freq, period,
                                seed + (uint32_t)o * 7919u) * amp;
        norm += amp;
        amp  *= 0.5f;
        freq *= 2.0f;   // must double with the period, or the wrap breaks
        period *= 2;
    }
    return sum / norm;
}

static float fbm(float x, float y, uint32_t seed) {
    float sum = 0.0f, amp = 0.5f, freq = 1.0f, norm = 0.0f;
    for (int o = 0; o < 4; ++o) {
        sum  += valueNoise(x * freq, y * freq, seed + (uint32_t)o * 7919u) * amp;
        norm += amp;
        amp  *= 0.5f;
        freq *= 2.07f;
    }
    return sum / norm;
}

void renderInit(const Simulation& sim) {
    renderShutdown();

    // Deliberately low resolution: it is stretched over thousands of world
    // pixels and only ever provides low-frequency variation, so a large
    // texture would cost startup time and memory for detail nobody sees.
    const int gw = 384;
    const int gh = std::max(16, (int)(384.0f * (float)sim.worldHeight / (float)sim.worldWidth));

    Image img = GenImageColor(gw, gh, pal::kGroundDark);
    const uint32_t seed = sim.getSeed();

    for (int y = 0; y < gh; ++y) {
        for (int x = 0; x < gw; ++x) {
            const float fx = (float)x / 33.0f;
            const float fy = (float)y / 33.0f;

            const float grass = fbm(fx, fy, seed);
            Color c = mix(pal::kGroundDark, pal::kGroundLite, grass);

            // A second, lower-frequency field decides where the turf gives way
            // to bare earth, so the patches are broad regions rather than
            // per-pixel speckle.
            const float dirt = fbm(fx * 0.35f + 31.7f, fy * 0.35f + 11.3f, seed ^ 0x9E3779B9u);
            if (dirt > 0.58f) {
                c = mix(c, pal::kGroundDirt, std::min(1.0f, (dirt - 0.58f) * 3.0f));
            }

            // Fine grain on top. Keeps flat areas from banding once the
            // texture is stretched by an order of magnitude.
            const float grain = hash2(x, y, seed + 5u) - 0.5f;
            c = shade(c, 1.0f + grain * 0.10f);

            ImageDrawPixel(&img, x, y, c);
        }
    }

    gGround = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(gGround, TEXTURE_FILTER_BILINEAR);

    // Grain layer: seamless, and deliberately zero-mean. Pixels above the
    // midpoint are lit blades, below it are shadowed ones, and the alpha is
    // the distance from the midpoint -- so tiling it over the macro texture
    // adds texture without shifting the field's overall tone.
    const int tw = 128;
    Image grain = GenImageColor(tw, tw, Color{ 0, 0, 0, 0 });
    for (int y = 0; y < tw; ++y) {
        for (int x = 0; x < tw; ++x) {
            const float n = fbmTiled((float)x * 16.0f / tw, (float)y * 16.0f / tw,
                                     16, seed ^ 0x51ED270Bu);
            const float d = n - 0.5f;
            const Color tint = (d > 0.0f) ? Color{ 116, 130,  86, 255 }
                                          : Color{  20,  26,  16, 255 };
            ImageDrawPixel(&grain, x, y,
                           Color{ tint.r, tint.g, tint.b, u8(std::fabs(d) * 2.0f * 62.0f) });
        }
    }
    gGrain = LoadTextureFromImage(grain);
    UnloadImage(grain);
    SetTextureFilter(gGrain, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(gGrain, TEXTURE_WRAP_REPEAT);

    gHasGround = true;
}

void renderShutdown() {
    if (gHasGround) {
        UnloadTexture(gGround);
        UnloadTexture(gGrain);
        gHasGround = false;
    }
}

// ---------------------------------------------------------------------------
// Transient effects
//
// Renderer-owned, never read by the simulation. Decals are what turn a
// finished battle into a readable record of where it was actually fought.
// ---------------------------------------------------------------------------
struct Decal {
    float x, y, radius, rotation;
    float age, life;
    Color color;
};

struct Flash {
    float x, y, age, life;
    Color color;
};

// Fixed capacity, overwritten oldest-first. An unbounded list would grow for
// as long as the window is open, and a long unattended run is exactly when
// nobody is watching it do so.
static constexpr size_t kMaxDecals = 3000;
static std::vector<Decal> gDecals;
static size_t gDecalCursor = 0;
static std::vector<Flash> gFlashes;
static uint32_t gEffectSeed = 1u;

void renderResetEffects() {
    gDecals.clear();
    gFlashes.clear();
    gDecalCursor = 0;
    gEffectSeed = 1u;
}

static float effectRand() {
    gEffectSeed = gEffectSeed * 1664525u + 1013904223u;
    return (float)((gEffectSeed >> 8) & 0xFFFFu) / 65535.0f;
}

static void pushDecal(const Decal& d) {
    if (gDecals.size() < kMaxDecals) {
        gDecals.push_back(d);
    } else {
        gDecals[gDecalCursor] = d;
        gDecalCursor = (gDecalCursor + 1) % kMaxDecals;
    }
}

void renderUpdateEffects(Simulation& sim, float dtSeconds) {
    for (const DeathEvent& e : sim.deathEvents()) {
        // Three overlapping splats rather than one circle: a single disc reads
        // as a token placed on the map, a cluster reads as a stain.
        const int splats = 2 + (int)(effectRand() * 2.0f);
        for (int s = 0; s < splats; ++s) {
            Decal d;
            d.x        = e.x + (effectRand() - 0.5f) * 10.0f;
            d.y        = e.y + (effectRand() - 0.5f) * 10.0f;
            d.radius   = 1.6f + effectRand() * 2.4f;
            d.rotation = effectRand() * 360.0f;
            d.age      = 0.0f;
            d.life     = 90.0f;
            // Tinted a little toward the team so a field tells you who died
            // where, without ever reading as anything but blood.
            d.color = mix(pal::kBlood,
                          e.team == Team::A ? pal::kTeamA : pal::kTeamB, 0.12f);
            pushDecal(d);
        }
        gFlashes.push_back(Flash{ e.x, e.y, 0.0f, 0.22f, pal::kSpark });
    }
    sim.clearDeathEvents();

    for (Decal& d : gDecals) d.age += dtSeconds;
    for (Flash& f : gFlashes) f.age += dtSeconds;
    gFlashes.erase(std::remove_if(gFlashes.begin(), gFlashes.end(),
                                  [](const Flash& f) { return f.age >= f.life; }),
                   gFlashes.end());
}

// ---------------------------------------------------------------------------
// World drawing
// ---------------------------------------------------------------------------

static void drawGround(int worldWidth, int worldHeight, float zoom) {
    if (gHasGround) {
        DrawTexturePro(gGround,
                       Rectangle{ 0, 0, (float)gGround.width, (float)gGround.height },
                       Rectangle{ 0, 0, (float)worldWidth, (float)worldHeight },
                       Vector2{ 0, 0 }, 0.0f, WHITE);

        // Faded in with zoom. Zoomed out, one grain texel is well under a
        // screen pixel and the layer is nothing but aliasing.
        const float fade = std::clamp((zoom - 0.5f) / 0.7f, 0.0f, 1.0f);
        if (fade > 0.01f) {
            const float rx = (float)worldWidth  / kGrainTileWorld * gGrain.width;
            const float ry = (float)worldHeight / kGrainTileWorld * gGrain.height;
            DrawTexturePro(gGrain, Rectangle{ 0, 0, rx, ry },
                           Rectangle{ 0, 0, (float)worldWidth, (float)worldHeight },
                           Vector2{ 0, 0 }, 0.0f,
                           Color{ 255, 255, 255, u8(fade * 255.0f) });
        }
    } else {
        DrawRectangle(0, 0, worldWidth, worldHeight, pal::kGroundDark);
    }

    // A soft rim inside the boundary. Reads as the field falling away rather
    // than as a hard line drawn on top of it, and does the same job the old
    // 3px blue box did without competing with the teams for attention.
    const float w = (float)worldWidth, h = (float)worldHeight;
    for (int i = 0; i < 5; ++i) {
        const float t  = (float)i;
        const float a  = 0.30f - t * 0.055f;
        DrawRectangleLinesEx(Rectangle{ t, t, w - t * 2.0f, h - t * 2.0f }, 1.0f,
                             alpha(pal::kBorder, a));
    }
}

static void drawGrid(int worldWidth, int worldHeight) {
    // Two densities: a fine mesh for local judgement and a coarse one that
    // survives being zoomed out. One uniform grid can only serve one of those.
    for (int x = 0; x <= worldWidth; x += 50) {
        const bool major = (x % 200) == 0;
        DrawLine(x, 0, x, worldHeight, alpha(pal::kBorder, major ? 0.22f : 0.09f));
    }
    for (int y = 0; y <= worldHeight; y += 50) {
        const bool major = (y % 200) == 0;
        DrawLine(0, y, worldWidth, y, alpha(pal::kBorder, major ? 0.22f : 0.09f));
    }
}

static void drawDecals() {
    for (const Decal& d : gDecals) {
        // Flat for most of its life, then fades over the last third. A decal
        // that starts fading immediately never looks like it stained anything.
        const float t = d.age / d.life;
        if (t >= 1.0f) continue;
        const float fade = t < 0.66f ? 1.0f : 1.0f - (t - 0.66f) / 0.34f;
        // Hexagons, not circles: raylib's circle is a 36-segment fan, and at
        // three thousand decals that is 100k triangles for shapes four pixels
        // across.
        DrawPoly(Vector2{ d.x, d.y }, 6, d.radius, d.rotation,
                 alpha(d.color, 0.40f * fade));
    }
}

static void drawTerrain(const TerrainField& terrain, bool shadows, float zoom) {
    if (shadows) {
        for (const auto& b : terrain.buildings) {
            DrawRectangle((int)(b.x + kLightX * 7.0f), (int)(b.y + kLightY * 7.0f),
                          (int)b.width, (int)b.height, Color{ 0, 0, 0, 105 });
        }
        for (const auto& t : terrain.trees) {
            DrawCircleV(Vector2{ t.x + kLightX * 5.0f, t.y + kLightY * 5.0f },
                        t.radius * 0.95f, Color{ 0, 0, 0, 95 });
        }
    }

    for (const auto& b : terrain.buildings) {
        // Walls are the collision footprint; the roof is drawn offset toward
        // the light. The few pixels of overhang are the whole trick -- without
        // an offset there is no parallax between the two faces and the
        // building is a grey rectangle again.
        DrawRectangle((int)b.x, (int)b.y, (int)b.width, (int)b.height, pal::kWallSide);

        const float rx = b.x - kLightX * 6.0f;
        const float ry = b.y - kLightY * 6.0f;
        const Rectangle roof{ rx, ry, b.width, b.height };
        // Gradient across the roof rather than a flat fill. A flat rectangle
        // in a field of mottled ground reads as a UI element sitting on top of
        // the world; a lit face reads as a thing standing in it.
        DrawRectangleGradientEx(roof, shade(pal::kWallTop, 1.22f),
                                      shade(pal::kWallTop, 0.86f),
                                      shade(pal::kWallTop, 1.02f),
                                      shade(pal::kWallTop, 0.70f));
        DrawRectangleLinesEx(roof, 1.0f, pal::kWallEdge);
        // Ridge line down the long axis: enough to say "roof" rather than
        // "slab", and it costs one line per building.
        if (b.width >= b.height) {
            const float my = ry + b.height * 0.5f;
            DrawLineEx(Vector2{ rx + 3.0f, my }, Vector2{ rx + b.width - 3.0f, my },
                       1.0f, alpha(shade(pal::kWallTop, 1.45f), 0.5f));
        } else {
            const float mx = rx + b.width * 0.5f;
            DrawLineEx(Vector2{ mx, ry + 3.0f }, Vector2{ mx, ry + b.height - 3.0f },
                       1.0f, alpha(shade(pal::kWallTop, 1.45f), 0.5f));
        }
        // Tile courses, only once they would resolve. Close up a bare gradient
        // is a brown slab; a few ruled lines are the difference between "a
        // rectangle" and "a roof".
        if (zoom >= 1.2f) {
            for (float ty = ry + 14.0f; ty < ry + b.height - 4.0f; ty += 14.0f) {
                DrawLineEx(Vector2{ rx + 2.0f, ty }, Vector2{ rx + b.width - 2.0f, ty },
                           1.0f, alpha(pal::kWallEdge, 0.28f));
            }
        }
        DrawRectangleLinesEx(Rectangle{ b.x, b.y, b.width, b.height }, 1.0f,
                             alpha(pal::kWallEdge, 0.7f));
    }

    for (const auto& t : terrain.trees) {
        DrawCircleV(Vector2{ t.x, t.y }, t.radius * 0.22f, pal::kTrunk);
        // Three overlapping lobes plus a lit cap. A single disc with a darker
        // inner disc read as a target ring, which is the last thing a tree
        // should look like on a battlefield full of them.
        DrawCircleV(Vector2{ t.x - t.radius * 0.28f, t.y + t.radius * 0.18f },
                    t.radius * 0.70f, shade(pal::kCanopy, 0.82f));
        DrawCircleV(Vector2{ t.x + t.radius * 0.30f, t.y + t.radius * 0.22f },
                    t.radius * 0.66f, shade(pal::kCanopy, 0.92f));
        DrawCircleV(Vector2{ t.x, t.y - t.radius * 0.12f }, t.radius * 0.78f, pal::kCanopy);
        DrawCircleV(Vector2{ t.x - t.radius * 0.22f, t.y - t.radius * 0.30f },
                    t.radius * 0.40f, pal::kCanopyLit);
    }
}

// One soldier's silhouette, drawn at an arbitrary offset so the same code
// produces the body and its shadow. Cavalry is a long narrow dart, infantry a
// short broad arrowhead, archers a square: the three have to differ in
// PROPORTION rather than size, because at four pixels apart they are the same
// blob.
static void drawSoldierShape(UnitType type, float x, float y, float dx, float dy,
                             float scale, Color color) {
    if (type == UnitType::Archer) {
        const float h = 2.3f * scale;
        DrawRectangleV(Vector2{ x - h, y - h }, Vector2{ h * 2.0f, h * 2.0f }, color);
        return;
    }

    const bool  cavalry   = (type == UnitType::Cavalry);
    const float size      = (cavalry ? 9.0f : 4.6f) * scale;
    const float halfWidth = size * (cavalry ? 0.28f : 0.62f);

    const float frontX = x + dx * size;
    const float frontY = y + dy * size;
    const float backX  = x - dx * size * 0.35f;
    const float backY  = y - dy * size * 0.35f;
    const float perpX  = -dy;
    const float perpY  =  dx;

    DrawTriangle(Vector2{ frontX, frontY },
                 Vector2{ backX - perpX * halfWidth, backY - perpY * halfWidth },
                 Vector2{ backX + perpX * halfWidth, backY + perpY * halfWidth },
                 color);
}

void drawSimulation(const Simulation& sim, float alpha_, const ViewSettings& view) {
    const float zoom = std::max(0.05f, view.zoom);

    // Level of detail. Everything below is a threshold on how many screen
    // pixels a world pixel is worth, not on an arbitrary "quality" setting:
    // outlines and pips are only drawn once they would actually resolve.
    // Terrain shadows are what give the world any depth at all, and there are
    // only a few dozen obstacles, so they stay on at every scale. Per-soldier
    // shadows are the ones that turn into mud when a man is two pixels wide.
    const bool  wantShadows = view.shadows;
    const bool  wantBodyShadows = view.shadows && zoom >= 0.34f;
    const bool  wantDetail  = zoom >= 1.1f;
    const bool  wantPips    = view.healthPips && zoom >= 2.2f;
    // Zoomed out, a four-pixel man is under two screen pixels and a hundred
    // agents disappear into the turf. Growing the marks keeps a small force
    // legible; the cap stops a large one from fusing into a single slab.
    const float markScale   = (zoom < 1.0f) ? std::min(1.0f / zoom, 1.7f) : 1.0f;

    drawGround(sim.worldWidth, sim.worldHeight, zoom);
    if (view.grid || sim.debugGrid) drawGrid(sim.worldWidth, sim.worldHeight);
    if (view.decals) drawDecals();
    drawTerrain(sim.terrain, wantShadows, zoom);

    // --- Squad tier ---------------------------------------------------------
    // Fades out as you zoom in: at the scale where you can read individual men
    // it is redundant, and at that scale a hundred translucent discs is haze.
    if (view.squadOverlay) {
        const float overlay = std::clamp((2.4f - zoom) / 1.6f, 0.0f, 1.0f);
        if (overlay > 0.01f) {
            for (size_t s = 0; s < sim.squads.count; ++s) {
                const uint32_t members = sim.squads.memberCount[s];
                if (members == 0) continue;

                const float cx = sim.squads.centroidX[s];
                const float cy = sim.squads.centroidY[s];
                const bool  teamA = sim.squads.team[s] == Team::A;
                const Color base  = teamA ? pal::kTeamA : pal::kTeamB;
                const float morale = std::clamp(sim.squads.morale[s], 0.0f, 1.0f);
                const bool  routing = sim.squads.order[s] == (uint8_t)SquadOrder::Rout;

                // Area, not radius, tracks head count -- a squad twice the size
                // should look twice the force, and radius-proportional discs
                // make a big squad look four times one half its size.
                const float r = 9.0f + std::sqrt((float)members) * 4.2f;
                DrawCircleV(Vector2{ cx, cy }, r, alpha(base, 0.14f * overlay));

                // Morale as a dial rather than a number: full green ring at
                // rest, eaten away counter-clockwise as the squad breaks.
                const Color mc = routing ? Color{ 200, 200, 205, 255 }
                                         : mix(Color{ 214, 82, 62, 255 },
                                               Color{ 122, 200, 120, 255 }, morale);
                DrawRing(Vector2{ cx, cy }, r + 1.0f, r + 3.0f,
                         -90.0f, -90.0f + 360.0f * morale, 24,
                         alpha(mc, 0.55f * overlay));

                if (sim.squads.contact[s]) {
                    DrawRing(Vector2{ cx, cy }, r + 4.0f, r + 5.5f, 0.0f, 360.0f, 24,
                             alpha(pal::kSpark, 0.45f * overlay));
                }

                // Where the squad is pointing. Short and inside the disc, so it
                // never reads as a movement order line.
                const float fx = sim.squads.facingX[s];
                const float fy = sim.squads.facingY[s];
                DrawLineEx(Vector2{ cx + fx * r * 0.45f, cy + fy * r * 0.45f },
                           Vector2{ cx + fx * (r + 6.0f), cy + fy * (r + 6.0f) },
                           1.5f, alpha(base, 0.75f * overlay));

                if (view.objectiveLines) {
                    DrawLineEx(Vector2{ cx, cy },
                               Vector2{ sim.squads.objectiveX[s], sim.squads.objectiveY[s] },
                               1.0f, alpha(base, 0.30f * overlay));
                }
            }
        }
    }

    // --- Soldiers -----------------------------------------------------------
    for (size_t i = 0; i < sim.soldiers.count; i++) {
        // clampToWorld only clamps and bounces (it has never wrapped a
        // position), so interpolating from the previous tick's position is
        // always safe here -- no large-delta special case needed.
        const float rx = sim.prevPosX[i] + (sim.soldiers.posX[i] - sim.prevPosX[i]) * alpha_;
        const float ry = sim.prevPosY[i] + (sim.soldiers.posY[i] - sim.prevPosY[i]) * alpha_;

        const UnitType type = sim.soldiers.unitType[i];
        const bool teamA = sim.soldiers.team[i] == Team::A;
        Color c = teamA ? pal::kTeamA : pal::kTeamB;

        // Unit type shifts value, never hue: archers lighter, cavalry deeper,
        // so type stays legible without either team drifting toward the other.
        if (type == UnitType::Archer)  c = mix(c, Color{ 255, 255, 255, 255 }, 0.26f);
        if (type == UnitType::Cavalry) c = shade(c, 0.68f);

        // Health reads as brightness, so a worn-down line is visible before it
        // breaks rather than only when it vanishes.
        const uint8_t maxHp = kUnitStats[(int)type].maxHealth;
        const float hpFrac = (maxHp > 1)
            ? (float)sim.soldiers.health[i] / (float)maxHp
            : 1.0f;
        if (maxHp > 1) c = shade(c, 0.50f + 0.50f * hpFrac);

        // A broken squad loses its colours. Silhouette and position are
        // unchanged, so you can still see the men -- they just stop reading as
        // part of an army, which is precisely what has happened to them.
        const uint16_t sq = sim.soldiers.squadId[i];
        const bool routing = sq < sim.squads.count &&
                             sim.squads.order[sq] == (uint8_t)SquadOrder::Rout;
        if (routing) c = mix(desaturate(c, 0.55f), Color{ 186, 182, 174, 255 }, 0.16f);

        const float dx = sim.soldiers.dirX[i];
        const float dy = sim.soldiers.dirY[i];

        if (wantBodyShadows) {
            drawSoldierShape(type, rx + kLightX * 2.4f, ry + kLightY * 2.4f,
                             dx, dy, markScale * 1.05f, Color{ 0, 0, 0, 115 });
        }
        // A dark body drawn a touch larger under the bright one. Separates
        // adjacent men in a packed rank, which is where a formation stops
        // being a formation and becomes a smear.
        if (wantDetail) {
            drawSoldierShape(type, rx, ry, dx, dy, markScale * 1.42f,
                             Color{ 14, 15, 20, 190 });
        }
        drawSoldierShape(type, rx, ry, dx, dy, markScale, c);

        if (wantPips && hpFrac < 0.999f) {
            const float w = 9.0f;
            DrawRectangleV(Vector2{ rx - w * 0.5f, ry - 9.0f }, Vector2{ w, 2.0f },
                           Color{ 12, 12, 16, 200 });
            DrawRectangleV(Vector2{ rx - w * 0.5f, ry - 9.0f },
                           Vector2{ w * hpFrac, 2.0f },
                           mix(Color{ 208, 76, 58, 255 }, Color{ 126, 206, 122, 255 }, hpFrac));
        }
    }

    // --- Arrows -------------------------------------------------------------
    // Drawn along their velocity and lifted off the ground by the arc they are
    // actually flying: liveAfter is kArrowArcFraction of the shot's length, so
    // the height curve here is the same trajectory the hit test uses, not a
    // decoration invented for the view.
    for (size_t i = 0; i < sim.projectiles.count; ++i) {
        const float vx = sim.projectiles.velX[i];
        const float vy = sim.projectiles.velY[i];
        const float len = std::sqrt(vx * vx + vy * vy);
        if (len < 1e-4f) continue;

        const float sx = sim.projectiles.posX[i];
        const float sy = sim.projectiles.posY[i];

        float height = 0.0f;
        const float flight = sim.projectiles.liveAfter[i] / kArrowArcFraction;
        if (flight > 1.0f) {
            const float t = std::clamp(sim.projectiles.traveled[i] / flight, 0.0f, 1.0f);
            height = std::sin(t * PI) * std::min(flight * 0.10f, 22.0f);
        }

        const float ux = vx / len, uy = vy / len;
        if (wantShadows && height > 0.5f) {
            DrawLineV(Vector2{ sx, sy }, Vector2{ sx - ux * 5.0f, sy - uy * 5.0f },
                      Color{ 0, 0, 0, 70 });
        }
        const float hx = sx - kLightX * height * 0.35f;
        const float hy = sy - height;
        DrawLineEx(Vector2{ hx, hy },
                   Vector2{ hx - ux * 5.5f, hy - uy * 5.5f },
                   1.2f, pal::kArrow);
        DrawLineEx(Vector2{ hx - ux * 5.5f, hy - uy * 5.5f },
                   Vector2{ hx - ux * 9.0f, hy - uy * 9.0f },
                   1.0f, alpha(pal::kArrow, 0.26f));
    }

    // --- Impact flashes -----------------------------------------------------
    // Additive, and short. This is the only thing on the field that moves
    // faster than the men, so it is the only thing that draws the eye to where
    // the fighting actually is.
    BeginBlendMode(BLEND_ADDITIVE);
    for (const Flash& f : gFlashes) {
        const float t = f.age / f.life;
        const float r = 2.0f + t * 5.0f;
        DrawCircleV(Vector2{ f.x, f.y }, r, alpha(f.color, (1.0f - t) * 0.34f));
    }
    EndBlendMode();
}

// ---------------------------------------------------------------------------
// Screen-space HUD
// ---------------------------------------------------------------------------

// raylib's default font is a 10px bitmap, so text drawn at exact multiples of
// 10 is pixel-crisp and everything else is mush. Every size below is 10 or 20
// on purpose.
static void panel(float x, float y, float w, float h) {
    DrawRectangleRounded(Rectangle{ x, y, w, h }, 0.18f, 6, Color{ 14, 16, 22, 205 });
    DrawRectangleRoundedLines(Rectangle{ x, y, w, h }, 0.18f, 6, Color{ 70, 80, 96, 120 });
}

static void drawVignette(int sw, int sh) {
    const int band = (int)(sh * 0.22f);
    const Color dark = Color{ 0, 0, 0, 110 };
    const Color none = Color{ 0, 0, 0, 0 };
    DrawRectangleGradientV(0, 0, sw, band, dark, none);
    DrawRectangleGradientV(0, sh - band, sw, band, none, dark);
    const int side = (int)(sw * 0.14f);
    DrawRectangleGradientH(0, 0, side, sh, dark, none);
    DrawRectangleGradientH(sw - side, 0, side, sh, none, dark);
}

void drawHud(const Simulation& sim, const ViewSettings& view,
             float viewportWidth, float timeScale, bool paused, float battleSeconds) {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    if (view.vignette) drawVignette(sw, sh);

    // --- Strength bar -------------------------------------------------------
    // One bar with a moving seam, not two bars side by side. The question a
    // viewer actually has is "who is winning", and a seam answers it at a
    // glance where two independent lengths require arithmetic.
    const size_t a = sim.getTeamCount(Team::A);
    const size_t b = sim.getTeamCount(Team::B);
    const float total = (float)std::max<size_t>(1, a + b);
    // Centred over the FIELD, not over the window: the panel rail on the right
    // is not part of the battlefield, and a bar centred on the window sits
    // visibly off-axis from the world it describes.
    const float vw = (viewportWidth > 1.0f) ? viewportWidth : (float)sw;
    const float barW = std::min(520.0f, vw * 0.62f);
    const float barX = (vw - barW) * 0.5f;
    const float barY = 16.0f;
    const float barH = 18.0f;

    panel(barX - 10.0f, barY - 10.0f, barW + 20.0f, barH + 40.0f);
    const float split = barW * ((float)a / total);
    DrawRectangleRec(Rectangle{ barX, barY, split, barH }, shade(pal::kTeamA, 0.92f));
    DrawRectangleRec(Rectangle{ barX + split, barY, barW - split, barH },
                     shade(pal::kTeamB, 0.92f));
    DrawRectangleRec(Rectangle{ barX + split - 1.0f, barY, 2.0f, barH },
                     Color{ 240, 240, 245, 220 });
    DrawRectangleLinesEx(Rectangle{ barX, barY, barW, barH }, 1.0f,
                         Color{ 20, 22, 28, 200 });

    char buf[96];
    std::snprintf(buf, sizeof(buf), "%zu", a);
    DrawText(buf, (int)(barX + 6.0f), (int)(barY + barH + 6.0f), 20, pal::kTeamA);
    std::snprintf(buf, sizeof(buf), "%zu", b);
    DrawText(buf, (int)(barX + barW - 6.0f - MeasureText(buf, 20)),
             (int)(barY + barH + 6.0f), 20, pal::kTeamB);

    const int mins = (int)battleSeconds / 60;
    const int secs = (int)battleSeconds % 60;
    std::snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);
    DrawText(buf, (int)(barX + (barW - MeasureText(buf, 20)) * 0.5f),
             (int)(barY + barH + 6.0f), 20, Color{ 176, 184, 198, 255 });

    // --- Run state and controls --------------------------------------------
    // Both in the bottom-left corner, stacked. They belong together (one says
    // what the clock is doing, the other says how to change it) and the top of
    // the screen is spoken for by the strength bar.
    const char* hints =
        "WHEEL zoom   RMB/LMB pan   WASD move   F fit   SPACE pause   [ ] speed   G grid   TAB overlay   F12 screenshot";
    const float hintW = (float)MeasureText(hints, 10) + 24.0f;
    panel(16.0f, sh - 42.0f, hintW, 26.0f);
    DrawText(hints, 28, sh - 34, 10, Color{ 150, 158, 172, 220 });

    const char* stateText = paused ? "PAUSED" : "RUNNING";
    const Color stateCol  = paused ? Color{ 236, 176, 76, 255 }
                                   : Color{ 122, 206, 128, 255 };
    std::snprintf(buf, sizeof(buf), "%s   %.2fx", stateText, (double)timeScale);
    const float chipW = (float)MeasureText(buf, 20) + 26.0f;
    const float chipY = sh - 84.0f;
    panel(16.0f, chipY, chipW, 32.0f);
    DrawRectangleRec(Rectangle{ 16.0f, chipY, 3.0f, 32.0f }, stateCol);
    DrawText(buf, 31, (int)chipY + 7, 20, Color{ 214, 220, 232, 255 });
}
