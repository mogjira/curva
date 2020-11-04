#include "render.h"
#include "tanto/m_math.h"
#include "tanto/v_image.h"
#include "tanto/v_memory.h"
#include <memory.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <tanto/r_render.h>
#include <tanto/v_video.h>
#include <tanto/t_def.h>
#include <tanto/t_utils.h>
#include <tanto/r_pipeline.h>
#include <tanto/r_raytrace.h>
#include <tanto/r_renderpass.h>
#include <tanto/v_command.h>
#include <vulkan/vulkan_core.h>

#define SPVDIR "/home/michaelb/dev/curva/shaders/spv"

static Tanto_R_Image renderTargetColor;
static Tanto_R_Image renderTargetDepth;

static VkRenderPass  myRenderPass;
static VkFramebuffer framebuffers[TANTO_FRAME_COUNT];

static Tanto_V_BufferRegion uniformBufferRegion;

static Tanto_V_BufferRegion drawCmdCurves;
static Tanto_V_BufferRegion drawCmdLines;
static Tanto_V_BufferRegion drawCmdPoints;

static Tanto_R_Primitive curve;
static Tanto_R_Primitive border;

static const VkSampleCountFlags SAMPLE_COUNT = VK_SAMPLE_COUNT_8_BIT;

static const uint32_t pointsPerPatch = 3;

typedef enum {
    R_PIPE_CURVES,
    R_PIPE_LINES,
    R_PIPE_POINTS,
} R_PipelineId;

typedef enum {
    R_PIPE_LAYOUT_MAIN,
} R_PipelineLayoutId;

typedef enum {
    R_DESC_SET_MAIN,
} R_DescriptorSetId;

// TODO: we should implement a way to specify the offscreen renderpass format at initialization
static void initOffscreenRenderTargets(void)
{
    //initDepthAttachment();
    renderTargetDepth = tanto_v_CreateImage(
            TANTO_WINDOW_WIDTH, TANTO_WINDOW_HEIGHT,
            depthFormat,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            SAMPLE_COUNT);

    renderTargetColor = tanto_v_CreateImageAndSampler(
            TANTO_WINDOW_WIDTH, TANTO_WINDOW_HEIGHT, 
            swapFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | 
            VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            SAMPLE_COUNT,
            VK_FILTER_NEAREST);

    tanto_v_TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, &renderTargetColor);
}

static void initRenderPass(void)
{
    const VkAttachmentDescription attachmentColor = {
        .format = swapFormat,
        .samples = SAMPLE_COUNT, // TODO look into what this means
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
    };

    const VkAttachmentDescription attachmentDepth = {
        .format = depthFormat,
        .samples = SAMPLE_COUNT, // TODO look into what this means
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    const VkAttachmentDescription attachmentPresent = {
        .format = swapFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT, // TODO look into what this means
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentDescription attachments[] = {
        attachmentColor,
        attachmentDepth,
        attachmentPresent
    };

    const VkAttachmentReference referenceColor = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    const VkAttachmentReference referenceDepth = {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    const VkAttachmentReference referenceResolve = {
        .attachment = 2,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pResolveAttachments  = &referenceResolve,
        .pColorAttachments    = &referenceColor,
        .pDepthStencilAttachment = &referenceDepth,
        .inputAttachmentCount = 0,
        .preserveAttachmentCount = 0,
    };

    Tanto_R_RenderPassInfo rpi = {
        .subpassCount = 1,
        .attachmentCount = 3,
        .pSubpasses = &subpass,
        .pAttachments = attachments 
    };

    tanto_r_CreateRenderPass(&rpi, &myRenderPass);
}

static void initFrameBuffers(void)
{
    for (int i = 0; i < TANTO_FRAME_COUNT; i++) 
    {
        // being able to view the attachment order in the renderpass creation
        // is a good reason to pass that information to the renderpass creation
        // from this module. currently need to flip over to tanto/r_render.c to make sure
        // i get the order correct
        VkImageView attachments[3] = {
            renderTargetColor.view, renderTargetDepth.view, frames[i].swapImage.view
        };

        VkFramebufferCreateInfo ci = {
            .height = TANTO_WINDOW_HEIGHT,
            .width = TANTO_WINDOW_WIDTH,
            .renderPass = myRenderPass,
            .layers = 1,
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .attachmentCount = 3,
            .pAttachments = attachments,
        };

        V_ASSERT( vkCreateFramebuffer(device, &ci, NULL, &framebuffers[i]) );
    }
}

// descriptors that do only need to have update called once and can be updated on initialization
static void updateStaticDescriptors(void)
{
    uniformBufferRegion = tanto_v_RequestBufferRegion(sizeof(UniformBuffer), 
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, TANTO_V_MEMORY_HOST_GRAPHICS_TYPE);
    memset(uniformBufferRegion.hostData, 0, sizeof(UniformBuffer));
    UniformBuffer* uboData = (UniformBuffer*)(uniformBufferRegion.hostData);

    Mat4 view = m_Ident_Mat4();
    view = m_Translate_Mat4((Vec3){0, 0, 1}, &view);
    m_ScaleNonUniform_Mat4((Vec3){1, -1, 1}, &view);

    uboData->matModel = m_Ident_Mat4();
    uboData->matView  = view;
    uboData->matProj  = m_Ident_Mat4(); // lets just work in screenspace

    VkDescriptorBufferInfo uboInfo = {
        .buffer = uniformBufferRegion.buffer,
        .offset = uniformBufferRegion.offset,
        .range  = uniformBufferRegion.size
    };

    VkWriteDescriptorSet writes[] = {{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstArrayElement = 0,
        .dstSet = descriptorSets[R_DESC_SET_MAIN],
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &uboInfo
    }};

    vkUpdateDescriptorSets(device, TANTO_ARRAY_SIZE(writes), writes, 0, NULL);
}

static void initIndirectCmdStorage(void)
{
    drawCmdCurves = tanto_v_RequestBufferRegion(sizeof(VkDrawIndexedIndirectCommand), 0, TANTO_V_MEMORY_HOST_GRAPHICS_TYPE);
    drawCmdLines  = tanto_v_RequestBufferRegion(sizeof(VkDrawIndirectCommand), 0, TANTO_V_MEMORY_HOST_GRAPHICS_TYPE);
    drawCmdPoints = tanto_v_RequestBufferRegion(sizeof(VkDrawIndirectCommand), 0, TANTO_V_MEMORY_HOST_GRAPHICS_TYPE);

    VkDrawIndirectCommand draw = {
        .firstInstance = 0,
        .firstVertex = 0,
        .instanceCount = 1,
        .vertexCount = curve.vertexCount,
    };

    VkDrawIndexedIndirectCommand drawIndexed = {
        .firstIndex = 0,
        .indexCount = curve.indexCount,
        .vertexOffset = 0,
        .firstInstance = 0,
        .instanceCount = 1,
    };

    memcpy(drawCmdCurves.hostData, &drawIndexed, sizeof(VkDrawIndexedIndirectCommand));
    memcpy(drawCmdLines.hostData,  &draw, sizeof(VkDrawIndirectCommand));
    memcpy(drawCmdPoints.hostData, &draw, sizeof(VkDrawIndirectCommand));
}

static void initPipelines(void)
{
    const Tanto_R_DescriptorSet descriptorSets[] = {{
        .id = R_DESC_SET_MAIN,
        .bindingCount = 1,
        .bindings = {{
            .descriptorCount = 1,
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
        }}
    }};

    const Tanto_R_PipelineLayout pipelayouts[] = {{
        .id = R_PIPE_LAYOUT_MAIN, 
        .descriptorSetCount = 1, 
        .descriptorSetIds = {R_DESC_SET_MAIN},
        .pushConstantCount = 0,
        .pushConstantsRanges = {}
    }};

    const Tanto_R_PipelineInfo pipeInfos[] = {{
        .id       = R_PIPE_CURVES,
        .type     = TANTO_R_PIPELINE_RASTER_TYPE,
        .layoutId = R_PIPE_LAYOUT_MAIN,
        .payload.rasterInfo = {
            .renderPass = myRenderPass, 
            .vertexDescription = tanto_r_GetVertexDescription3D_Simple(),
            .polygonMode = VK_POLYGON_MODE_LINE,
            .sampleCount = SAMPLE_COUNT,
            .tesselationPatchPoints = pointsPerPatch,
            .vertShader = SPVDIR"/curve-vert.spv",
            .fragShader = SPVDIR"/color-frag.spv",
            .tessCtrlShader = SPVDIR"/passthrough-tesc.spv",
            .tessEvalShader = SPVDIR"/basic-tese.spv"
        }
    },{
        .id       = R_PIPE_LINES,
        .type     = TANTO_R_PIPELINE_RASTER_TYPE,
        .layoutId = R_PIPE_LAYOUT_MAIN,
        .payload.rasterInfo = {
            .renderPass = myRenderPass, 
            .vertexDescription = tanto_r_GetVertexDescription3D_Simple(),
            .polygonMode = VK_POLYGON_MODE_LINE,
            .sampleCount = SAMPLE_COUNT,
            .vertShader = SPVDIR"/points-vert.spv",
            .fragShader = SPVDIR"/color-frag.spv",
        }
    },{
        .id       = R_PIPE_POINTS,
        .type     = TANTO_R_PIPELINE_RASTER_TYPE,
        .layoutId = R_PIPE_LAYOUT_MAIN,
        .payload.rasterInfo = {
            .renderPass = myRenderPass, 
            .vertexDescription = tanto_r_GetVertexDescription3D_Simple(),
            .polygonMode = VK_POLYGON_MODE_POINT,
            .sampleCount = SAMPLE_COUNT,
            .vertShader = SPVDIR"/points-vert.spv",
            .fragShader = SPVDIR"/color-frag.spv",
        }
    }};

    tanto_r_InitDescriptorSets(descriptorSets, TANTO_ARRAY_SIZE(descriptorSets));
    tanto_r_InitPipelineLayouts(pipelayouts, TANTO_ARRAY_SIZE(pipelayouts));
    tanto_r_InitPipelines(pipeInfos, TANTO_ARRAY_SIZE(pipeInfos));
}

static void initPrimitives(void)
{
    //create border
    border = tanto_r_CreatePoints(5);
    Vec3* positions = (Vec3*)(border.vertexRegion.hostData);
    Vec3* colors    = (Vec3*)(border.vertexRegion.hostData + border.attrOffsets[1]);
    positions[4] = (Vec3){1.0, 1.0, 0.0};
    positions[0] = (Vec3){1.0, 1.0, 0.0};
    positions[1] = (Vec3){-1.0, 1.0, 0.0};
    positions[2] = (Vec3){-1.0, -1.0, 0.0};
    positions[3] = (Vec3){1.0, -1.0, 0.0};
    for (int i = 0; i < 5; i++) 
    {
        positions[i] = m_Scale_Vec3(0.95, &positions[i]);
        colors[i] = (Vec3){0.1, 0.4, 0.9};
    }

    //create curve
    const size_t pointCount = 400;
    curve = tanto_r_CreateCurve(pointCount, pointsPerPatch, 1);
}

static void mainRender(const VkCommandBuffer* cmdBuf, const VkRenderPassBeginInfo* rpassInfo)
{
    vkCmdBindDescriptorSets(
        *cmdBuf, 
        VK_PIPELINE_BIND_POINT_GRAPHICS, 
        pipelineLayouts[R_PIPE_LAYOUT_MAIN], 
        0, 1, &descriptorSets[R_DESC_SET_MAIN],
        0, NULL);

    vkCmdBeginRenderPass(*cmdBuf, rpassInfo, VK_SUBPASS_CONTENTS_INLINE);

    const VkBuffer vertBuffersCurve[2] = {
        curve.vertexRegion.buffer,
        curve.vertexRegion.buffer
    };

    const VkDeviceSize attrOffsetsCurve[2] = {
        curve.attrOffsets[0] + curve.vertexRegion.offset,
        curve.attrOffsets[1] + curve.vertexRegion.offset,
    };

    const VkBuffer vertBuffersBorder[2] = {
        border.vertexRegion.buffer,
        border.vertexRegion.buffer
    };

    const VkDeviceSize attrOffsetsBorder[2] = {
        border.attrOffsets[0] + border.vertexRegion.offset,
        border.attrOffsets[1] + border.vertexRegion.offset,
    };

    // draw the curves
    vkCmdBindPipeline(*cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[R_PIPE_CURVES]);
    vkCmdBindVertexBuffers(*cmdBuf, 0, 2, vertBuffersCurve, attrOffsetsCurve);
    vkCmdBindIndexBuffer(*cmdBuf, curve.indexRegion.buffer, curve.indexRegion.offset, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexedIndirect(*cmdBuf, drawCmdCurves.buffer, drawCmdCurves.offset, 1, sizeof(VkDrawIndexedIndirectCommand));

    // draw the points 
    vkCmdBindPipeline(*cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[R_PIPE_POINTS]);
    vkCmdDrawIndirect(*cmdBuf, drawCmdPoints.buffer, drawCmdPoints.offset, 1, sizeof(VkDrawIndirectCommand));

    assert(border.vertexCount > 0);

    // draw the lines
    vkCmdBindPipeline(*cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[R_PIPE_LINES]);
    vkCmdDrawIndirect(*cmdBuf, drawCmdLines.buffer, drawCmdLines.offset, 1, sizeof(VkDrawIndirectCommand));
    
    // draw the border
    vkCmdBindVertexBuffers(*cmdBuf, 0, 2, vertBuffersBorder, attrOffsetsBorder);
    vkCmdDraw(*cmdBuf, border.vertexCount, 1, 0, 0);

    vkCmdEndRenderPass(*cmdBuf);
}

VkDrawIndirectCommand* r_GetDrawCmd(const R_Draw_Cmd_Type type)
{
    switch (type) 
    {
        case LINES_TYPE:  return (VkDrawIndirectCommand*)(drawCmdLines.hostData);
        case POINTS_TYPE: return (VkDrawIndirectCommand*)(drawCmdPoints.hostData);
        default: assert(0);
    }
    return NULL;
}

VkDrawIndexedIndirectCommand* r_GetDrawIndexedCmd(const R_Draw_Cmd_Type type)
{
    switch (type) 
    {
        case CURVES_TYPE: return (VkDrawIndexedIndirectCommand*)(drawCmdCurves.hostData);
        default: assert(0);
    }
    return NULL;
}

Tanto_R_Primitive* r_GetCurve(void)
{
    return &curve;
}

UniformBuffer* r_GetUBO(void)
{
    return (UniformBuffer*)uniformBufferRegion.hostData;
}

void r_InitRenderer()
{
    initOffscreenRenderTargets();
    initRenderPass();
    initPipelines();
    initFrameBuffers();
    updateStaticDescriptors();
    initPrimitives();
    initIndirectCmdStorage();
}

void r_UpdateRenderCommands(void)
{
    Tanto_R_Frame* frame = &frames[curFrameIndex];
    vkResetCommandPool(device, frame->commandPool, 0);
    VkCommandBufferBeginInfo cbbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    V_ASSERT( vkBeginCommandBuffer(frame->commandBuffer, &cbbi) );

    VkClearValue clearValueColor = {0.002f, 0.002f, 0.004f, 1.0f};
    VkClearValue clearValueDepth = {1.0, 0};

    VkClearValue clears[] = {clearValueColor, clearValueDepth, clearValueColor};

    const VkRenderPassBeginInfo renderpassMain = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .clearValueCount = TANTO_ARRAY_SIZE(clears),
        .pClearValues = clears,
        .renderArea = {{0, 0}, {TANTO_WINDOW_WIDTH, TANTO_WINDOW_HEIGHT}},
        .renderPass =  myRenderPass,
        .framebuffer = framebuffers[curFrameIndex],
    };

    mainRender(&frame->commandBuffer, &renderpassMain);

    V_ASSERT( vkEndCommandBuffer(frame->commandBuffer) );
}

void r_CleanUp(void)
{
    for (int i = 0; i < TANTO_FRAME_COUNT; i++) 
    {
        vkDestroyFramebuffer(device, framebuffers[i], NULL);
    }
    tanto_v_DestroyImage(renderTargetColor);
    tanto_v_DestroyImage(renderTargetDepth);
}
