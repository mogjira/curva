#ifndef VIEWER_R_COMMANDS_H
#define VIEWER_R_COMMANDS_H

#include <tanto/r_geo.h>
#include "common.h"

typedef struct {
    int foo;
    int bar;
} PushConstants;

typedef struct {
    Mat4 matModel;
    Mat4 matView;
    Mat4 matProj;
} UniformBuffer;

typedef enum {
    CURVES_TYPE,
    LINES_TYPE,
    POINTS_TYPE,
} R_Draw_Cmd_Type;

void  r_InitRenderer(void);
void  r_UpdateRenderCommands(void);
void  r_LoadMesh(Tanto_R_Mesh mesh);
void  r_ClearMesh(void);
void  r_CleanUp(void);

VkDrawIndirectCommand* r_GetDrawCmd(const R_Draw_Cmd_Type);
VkDrawIndexedIndirectCommand* r_GetDrawIndexedCmd(const R_Draw_Cmd_Type type);
Tanto_R_Primitive* r_GetCurve(void);
UniformBuffer* r_GetUBO(void);

extern const uint32_t r_pointsPerPatch;
extern const uint32_t r_restartOffset;

#endif /* end of include guard: R_COMMANDS_H */
