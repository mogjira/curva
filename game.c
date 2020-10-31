#include "game.h"
#include "render.h"
#include "common.h"
#include "tanto/m_math.h"
#include "tanto/t_utils.h"
#include <assert.h>
#include <string.h>
#include <tanto/t_def.h>
#include <tanto/i_input.h>


static Vec2  mousePos;
Parms parms; 
struct ShaderParms* pShaderParms;

static float t;

static VkDrawIndirectCommand* drawParms;

typedef struct {
    uint32_t pointCount;
    Vec3*    positions;
    Vec3*    colors;
} Curve;

Curve curve;

static void setSpiral(const uint32_t pointCount, Vec3* positions)
{
    for (int i = 0; i < pointCount; i++) 
    {
        float f = (float)i;
        f = f / 154.0;
        const float rad = (1.0 - f) * 0.7 + 0.25;
        f *= M_PI * 100;
        const float x = cos(f) * 0.8 * rad;
        const float y = sin(f) * rad;

        positions[i] = (Vec3){
            x, y, 0
        };
    }
}

static void setLine(const uint32_t pointCount, Vec3* pos)
{
    for (int i = 0; i < pointCount; i++) 
    {
        pos[i].x[0] = (float)i / pointCount;
        pos[i].x[1] = (float)i / pointCount;
    }
}

static void setColor(const Vec3 c, const uint32_t pointCount, Vec3* colors)
{
    for (int i = 0; i < pointCount; i++) 
    {
        colors[i] = c;
    }
}

void g_Init(void)
{
    parms.shouldRun = true;
    t = 0.0;
    drawParms = r_GetDrawParms();
    Tanto_R_Primitive* cprim = r_GetCurve();
    curve.pointCount = cprim->vertexCount;
    curve.positions  = (Vec3*)cprim->vertexRegion.hostData;
    curve.colors = (Vec3*)(cprim->vertexRegion.hostData + cprim->attrOffsets[1]);

    setColor((Vec3){0.1, 0.9, 0.3}, curve.pointCount, curve.colors);

    setSpiral(curve.pointCount, curve.positions);
}

void g_Responder(const Tanto_I_Event *event)
{
    switch (event->type) 
    {
        case TANTO_I_KEYDOWN: switch (event->data.keyCode)
        {
            case TANTO_KEY_ESC: parms.shouldRun = false; break;
            default: return;
        } break;
        case TANTO_I_KEYUP:   switch (event->data.keyCode)
        {
            default: return;
        } break;
        case TANTO_I_MOTION: 
        {
            mousePos.x = (float)event->data.mouseData.x / TANTO_WINDOW_WIDTH;
            mousePos.y = (float)event->data.mouseData.y / TANTO_WINDOW_HEIGHT;
        } break;
        case TANTO_I_MOUSEDOWN: 
        {
        } break;
        case TANTO_I_MOUSEUP:
        {
        } break;
        default: break;
    }
}

void g_Update(void)
{
    t += 0.5;
    printf("t: %f\n", t);
    const int i = (int)t;
    printf("i: %d\n", i);
    const int pc = curve.pointCount; 
    drawParms->vertexCount = i % (pc + 1);
    printf("i mod pc: %d\n", i % pc);
}

