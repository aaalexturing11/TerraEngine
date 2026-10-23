#include "Inventory.h"

#include "ItemIcon.h"

#include "imgui.h"

#include <cmath>

static Block gSlot[kInventorySlots] = {};
static int gSelected = 0;
static Block gCursor = Block::Air;
static int gPress = -1;
static float gPressX = 0.0f;
static float gPressY = 0.0f;
static bool gDrag = false;
static float gSelVis = 0.0f;
static float gPop = 0.0f;
static float gSwap = 0.0f;
static float gFlash[kInventorySlots] = {};
static HudMode gHud = HudMode::Playing;

static void putSlot(int index, Block block) {
    if (index < 0 || index >= kInventorySlots) return;
    if (gSlot[index] == block) return;
    gSlot[index] = block;
    gFlash[index] = 1.0f;
}

static bool emptyOf(int index) {
    return index >= 0 && index < kInventorySlots && gSlot[index] == Block::Air;
}

// No hay entidades de objeto en el mundo. Hasta que existan, sacar un objeto
// del inventario lo descarta: el slot queda vacío y el objeto no se duplica.
static void discardCursor() {
    gCursor = Block::Air;
}

static void pickup(int index) {
    if (emptyOf(index)) return;
    gCursor = gSlot[index];
    putSlot(index, Block::Air);
}

static void placeOrSwap(int index) {
    if (index < 0 || index >= kInventorySlots || gCursor == Block::Air) return;
    Block there = gSlot[index];
    putSlot(index, gCursor);
    gCursor = there;
}

static void moveSlot(int from, int to) {
    if (from == to || from < 0 || to < 0 || from >= kInventorySlots || to >= kInventorySlots) return;
    Block a = gSlot[from];
    Block b = gSlot[to];
    putSlot(from, b);
    putSlot(to, a);
}

static bool shiftInto(int from, int begin, int count) {
    Block item = gSlot[from];
    if (item == Block::Air) return false;
    for (int i = 0; i < count; ++i) {
        int dst = begin + i;
        if (gSlot[dst] != Block::Air) continue;
        putSlot(dst, item);
        putSlot(from, Block::Air);
        return true;
    }
    return false;
}

static void shiftClick(int index) {
    if (gCursor != Block::Air || emptyOf(index)) return;
    if (index < kHotbarSlots)
        shiftInto(index, kHotbarSlots, kMainSlots);
    else if (index == kOffhandSlot) {
        if (!shiftInto(index, 0, kHotbarSlots))
            shiftInto(index, kHotbarSlots, kMainSlots);
    }
    else
        shiftInto(index, 0, kHotbarSlots);
}

struct SlotInit {
    Block block;
    int index;
};

void inventoryCancelGesture() {
    gPress = -1;
    gDrag = false;
}

void inventoryTick(float dt) {
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;
    static bool seeded = false;
    if (!seeded) {
        const SlotInit start[] = {
            { Block::Grass, 0 }, { Block::Water, 1 }, { Block::Torch, 2 },
            { Block::Lava, 3 }, { Block::Dirt, 4 }, { Block::Log, 5 },
            { Block::Leaves, 6 }, { Block::TallGrass, 7 }, { Block::Poppy, 8 },
            { Block::Triteleia, 9 }, { Block::TriteleiaRed, 10 },
            { Block::Stone, 11 }, { Block::Obsidian, 12 }
        };
        for (const SlotInit& item : start) gSlot[item.index] = item.block;
        seeded = true;
    }
    gSelVis += ((float)gSelected - gSelVis) * (1.0f - std::exp(-dt * 16.0f));
    if (gPop > 0.0f) {
        gPop -= dt * 3.2f;
        if (gPop < 0.0f) gPop = 0.0f;
    }
    if (gSwap > 0.0f) {
        gSwap -= dt * 3.4f;
        if (gSwap < 0.0f) gSwap = 0.0f;
    }
    for (float& flash : gFlash) {
        if (flash <= 0.0f) continue;
        flash -= dt * 4.5f;
        if (flash < 0.0f) flash = 0.0f;
    }
}

void inventorySelect(int slot) {
    if (slot < 0) slot = 0;
    if (slot >= kHotbarSlots) slot = kHotbarSlots - 1;
    if (slot == gSelected) return;
    gSelected = slot;
    gPop = 1.0f;
}

int inventorySelected() { return gSelected; }

void inventorySetSlot(int index, Block block) {
    putSlot(index, block);
}

Block inventorySlot(int index) {
    if (index < 0 || index >= kInventorySlots) return Block::Air;
    return gSlot[index];
}

Block inventoryOffhand() { return gSlot[kOffhandSlot]; }

const char* inventoryBlockName(Block block) {
    switch (block) {
    case Block::Grass: return "Pasto";
    case Block::Dirt: return "Tierra";
    case Block::Stone: return "Piedra";
    case Block::Log: return "Tronco";
    case Block::Leaves: return "Hojas";
    case Block::Water: return "Agua";
    case Block::Lava: return "Lava";
    case Block::Obsidian: return "Obsidiana";
    case Block::Torch: return "Antorcha";
    case Block::TallGrass: return "Pasto alto";
    case Block::Poppy: return "Amapola";
    case Block::Triteleia: return "Flor";
    case Block::TriteleiaRed: return "Flor rosa";
    default: return "";
    }
}

bool inventorySwapOffhand() {
    Block main = gSlot[gSelected];
    Block off = gSlot[kOffhandSlot];
    if (main == Block::Air && off == Block::Air) return false;
    gSlot[gSelected] = off;
    gSlot[kOffhandSlot] = main;
    gFlash[gSelected] = 1.0f;
    gFlash[kOffhandSlot] = 1.0f;
    gSwap = 1.0f;
    return true;
}

float inventorySwapAmount() { return gSwap; }

HudMode inventoryHud() { return gHud; }

void inventoryCycleHud() {
    if (gHud == HudMode::Playing) gHud = HudMode::Hidden;
    else if (gHud == HudMode::Hidden) gHud = HudMode::Clean;
    else gHud = HudMode::Playing;
}

static bool hiding(int index) {
    return gDrag && gPress == index;
}

static void drawIcon(ImDrawList* dl, ImVec2 p, float size, Block block, float time, float pop) {
    if (block == Block::Air) return;
    float scale = 1.0f + 0.08f * pop;
    float icon = size * 0.74f * scale;
    float ox = (size - icon) * 0.5f;
    float oy = (size - icon) * 0.46f;
    ImVec2 a(p.x + ox, p.y + oy);
    ImVec2 b(a.x + icon, a.y + icon);
    unsigned tex = itemIcon(block, time);
    if (tex)
        dl->AddImage((ImTextureID)(intptr_t)tex, a, b, ImVec2(0, 1), ImVec2(1, 0));
    else
        dl->AddRectFilled(a, b, IM_COL32(90, 140, 70, 255));
}

static void drawWell(ImDrawList* dl, ImVec2 p, float size, bool selected, bool hovered, float flash) {
    ImU32 fill = hovered ? IM_COL32(62, 64, 74, 210) : IM_COL32(18, 18, 22, 168);
    if (selected) fill = IM_COL32(54, 44, 18, 210);
    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), fill);
    dl->AddRect(p, ImVec2(p.x + size, p.y + size), IM_COL32(6, 6, 8, 230), 0.0f, 0, selected ? 2.0f : 1.0f);
    dl->AddLine(ImVec2(p.x + 1.0f, p.y + 1.0f), ImVec2(p.x + size - 2.0f, p.y + 1.0f), IM_COL32(255, 255, 255, 40));
    dl->AddLine(ImVec2(p.x + 1.0f, p.y + 1.0f), ImVec2(p.x + 1.0f, p.y + size - 2.0f), IM_COL32(255, 255, 255, 28));
    dl->AddLine(ImVec2(p.x + 2.0f, p.y + size - 2.0f), ImVec2(p.x + size - 2.0f, p.y + size - 2.0f), IM_COL32(0, 0, 0, 90));
    if (flash > 0.0f)
        dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), IM_COL32(255, 244, 210, (int)(70.0f * flash)));
}

static void drawSlot(ImDrawList* dl, ImVec2 p, float size, int index, bool selected, bool hovered, float time, const char* key) {
    float pop = (index == gSelected) ? gPop : 0.0f;
    drawWell(dl, p, size, selected, hovered, gFlash[index]);
    if (!hiding(index))
        drawIcon(dl, p, size, gSlot[index], time, pop);
    if (key && key[0])
        dl->AddText(ImVec2(p.x + 3.0f, p.y + 1.0f), IM_COL32(255, 255, 255, 220), key);
}

static void drawCursorGhost(float time, float size) {
    Block block = gDrag && gPress >= 0 ? gSlot[gPress] : gCursor;
    if (block == Block::Air) return;
    ImVec2 m = ImGui::GetMousePos();
    float s = size * 0.82f;
    ImVec2 a(m.x - s * 0.35f, m.y - s * 0.45f);
    unsigned tex = itemIcon(block, time);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (tex)
        dl->AddImage((ImTextureID)(intptr_t)tex, a, ImVec2(a.x + s, a.y + s), ImVec2(0, 1), ImVec2(1, 0));
}

struct SlotHit {
    int index = -1;
    ImVec2 a = {};
    ImVec2 b = {};
};

static const int kMaxHits = 40;
static SlotHit gHits[kMaxHits];
static int gHitCount = 0;

static void remember(int index, ImVec2 p, float size) {
    if (gHitCount >= kMaxHits) return;
    gHits[gHitCount].index = index;
    gHits[gHitCount].a = p;
    gHits[gHitCount].b = ImVec2(p.x + size, p.y + size);
    ++gHitCount;
}

static int hitAt(ImVec2 m) {
    for (int i = 0; i < gHitCount; ++i) {
        const SlotHit& h = gHits[i];
        if (m.x >= h.a.x && m.y >= h.a.y && m.x < h.b.x && m.y < h.b.y)
            return h.index;
    }
    return -1;
}

static void consumePointer(ImVec2 winMin, ImVec2 winMax) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 m = io.MousePos;
    int hover = hitAt(m);
    bool inside = m.x >= winMin.x && m.y >= winMin.y && m.x < winMax.x && m.y < winMax.y;
    if (gPress >= 0) {
        float dx = m.x - gPressX;
        float dy = m.y - gPressY;
        if (dx * dx + dy * dy > 36.0f) gDrag = true;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hover >= 0) {
            if (io.KeyShift && gCursor == Block::Air && !gDrag) {
                shiftClick(hover);
                gPress = -1;
                gDrag = false;
            }
            else if (gCursor != Block::Air) {
                placeOrSwap(hover);
                gPress = -1;
                gDrag = false;
            }
            else if (gSlot[hover] != Block::Air) {
                gPress = hover;
                gPressX = m.x;
                gPressY = m.y;
                gDrag = false;
            }
        }
        else if (!inside && gCursor != Block::Air) {
            discardCursor();
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && gPress >= 0) {
        if (gDrag) {
            if (hover >= 0 && hover != gPress) moveSlot(gPress, hover);
            else if (!inside) putSlot(gPress, Block::Air);
        }
        else {
            pickup(gPress);
        }
        gPress = -1;
        gDrag = false;
    }
}

void inventoryDrawHotbar(int width, int height, float time) {
    float slot = (float)height * 0.056f;
    if (slot < 38.0f) slot = 38.0f;
    if (slot > 54.0f) slot = 54.0f;
    const float gap = 3.0f;
    const float pad = 7.0f;
    const float offGap = 16.0f;
    float bar = kHotbarSlots * slot + (kHotbarSlots - 1) * gap;
    float total = slot + offGap + bar;
    float x = ((float)width - total) * 0.5f;
    float y = (float)height - slot - pad - (float)height * 0.028f;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 plate(x - pad, y - pad - 16.0f);
    ImVec2 plateEnd(x + total + pad, y + slot + pad);
    dl->AddRectFilled(plate, plateEnd, IM_COL32(8, 8, 10, 120), 2.0f);
    dl->AddRect(plate, plateEnd, IM_COL32(0, 0, 0, 180), 2.0f, 0, 1.0f);

    const char* name = inventoryBlockName(gSlot[gSelected]);
    if (name[0]) {
        ImVec2 ts = ImGui::CalcTextSize(name);
        dl->AddText(ImVec2(x + slot + offGap + (bar - ts.x) * 0.5f, y - 16.0f), IM_COL32(255, 255, 255, 230), name);
    }

    ImVec2 mouse = ImGui::GetMousePos();
    auto hot = [&](ImVec2 p) {
        return mouse.x >= p.x && mouse.y >= p.y && mouse.x < p.x + slot && mouse.y < p.y + slot;
    };
    drawSlot(dl, ImVec2(x, y), slot, kOffhandSlot, false, hot(ImVec2(x, y)), time, "F");
    float hx = x + slot + offGap;
    float mark = hx + gSelVis * (slot + gap);
    dl->AddRect(ImVec2(mark - 2.0f, y - 2.0f), ImVec2(mark + slot + 2.0f, y + slot + 2.0f), IM_COL32(255, 214, 90, 255), 0.0f, 0, 2.0f);
    for (int i = 0; i < kHotbarSlots; ++i) {
        ImVec2 p(hx + i * (slot + gap), y);
        char key[2] = { (char)('1' + i), 0 };
        drawSlot(dl, p, slot, i, false, hot(p), time, key);
    }
}

void inventoryDrawScreen(int width, int height, float time) {
    float slot = (float)height * 0.048f;
    if (slot < 34.0f) slot = 34.0f;
    if (slot > 48.0f) slot = 48.0f;
    const float gap = 4.0f;
    gHitCount = 0;

    ImGui::SetNextWindowPos(ImVec2(width * 0.5f, height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.07f, 0.08f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.02f, 0.02f, 0.02f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
    ImGui::Begin("Inventario", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mouse = ImGui::GetMousePos();
    auto cell = [&](int index, const char* key) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(slot, slot));
        bool hovered = mouse.x >= p.x && mouse.y >= p.y && mouse.x < p.x + slot && mouse.y < p.y + slot;
        bool selected = index == gSelected;
        drawSlot(dl, p, slot, index, selected && index < kHotbarSlots, hovered, time, key);
        remember(index, p, slot);
    };

    ImGui::TextUnformatted("Objetos");
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < kHotbarSlots; ++col) {
            if (col) ImGui::SameLine(0.0f, gap);
            cell(kHotbarSlots + row * kHotbarSlots + col, nullptr);
        }
    }
    ImGui::Dummy(ImVec2(1.0f, 8.0f));
    ImGui::TextUnformatted("Hotbar");
    for (int i = 0; i < kHotbarSlots; ++i) {
        if (i) ImGui::SameLine(0.0f, gap);
        char key[2] = { (char)('1' + i), 0 };
        cell(i, key);
    }
    ImGui::SameLine(0.0f, 18.0f);
    cell(kOffhandSlot, "F");
    ImGui::TextUnformatted("Mano izquierda");

    ImVec2 winMin = ImGui::GetWindowPos();
    ImVec2 winSize = ImGui::GetWindowSize();
    consumePointer(winMin, ImVec2(winMin.x + winSize.x, winMin.y + winSize.y));
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    drawCursorGhost(time, slot);
}
