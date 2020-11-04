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
static bool  mouseDown;
static bool  showPoints;
static bool  showLines;

Parms parms; 
struct ShaderParms* pShaderParms;

UniformBuffer* ubo;

static float t;

static VkDrawIndexedIndirectCommand* drawCmdCurves;
static VkDrawIndirectCommand* drawCmdLines;
static VkDrawIndirectCommand* drawCmdPoints;

typedef struct {
    uint32_t totalPointCount;
    uint32_t activePointCount;
    uint32_t activeIndexCount;
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
    drawCmdCurves = r_GetDrawIndexedCmd(CURVES_TYPE);
    drawCmdLines =  r_GetDrawCmd(LINES_TYPE);
    drawCmdPoints = r_GetDrawCmd(POINTS_TYPE);
    Tanto_R_Primitive* cprim = r_GetCurve();
    curve.totalPointCount = cprim->vertexCount;
    curve.activePointCount = 0;
    curve.positions  = (Vec3*)cprim->vertexRegion.hostData;
    curve.colors = (Vec3*)(cprim->vertexRegion.hostData + cprim->attrOffsets[1]);

    showPoints = true;
    showLines  = true;
    ubo = r_GetUBO();

    setColor((Vec3){0.1, 0.9, 0.3}, curve.totalPointCount, curve.colors);
}

void g_Responder(const Tanto_I_Event *event)
{
    switch (event->type) 
    {
        case TANTO_I_KEYDOWN: switch (event->data.keyCode)
        {
            case TANTO_KEY_ESC: parms.shouldRun = false; break;
            case TANTO_KEY_E: showPoints = showPoints ? false : true; break;
            case TANTO_KEY_R: showLines  = showLines  ? false : true; break;
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
            mouseDown = true;
        } break;
        case TANTO_I_MOUSEUP:
        {
            mouseDown = false;
        } break;
        default: break;
    }
}

static void activatePoint(void)
{
    float x = mousePos.x * 2.0 - 1.0;
    float y = mousePos.y * 2.0 - 1.0;
    y *= -1;
    curve.positions[curve.activePointCount] = (Vec3){x, y, 0.0};
    curve.activePointCount++;
    curve.activeIndexCount++;
    if (curve.activePointCount % 3 == 0)
        curve.activeIndexCount++;
    if (curve.activePointCount > 3 && (curve.activeIndexCount - curve.activePointCount) % 3 == 0)
        curve.activeIndexCount++;
    printf("Vertex count: %d\n", curve.activePointCount);
    printf("Index  count: %d\n", curve.activeIndexCount);
}

void g_Update(void)
{
    t += 0.5;
    if (mouseDown)
    {
        activatePoint();
        mouseDown = false;
    }
    const int pc = curve.activePointCount; 
    const int ic = curve.activeIndexCount; 
    drawCmdCurves->indexCount = ic;
    drawCmdLines->vertexCount  = showLines  ? pc : 0;
    drawCmdPoints->vertexCount = showPoints ? pc : 0;
}

