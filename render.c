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
#include <tanto/v_command.h>
#include <vulkan/vulkan_core.h>

#define SPVDIR "/home/michaelb/dev/curva/shaders/spv"

static Tanto_R_FrameBuffer  offscreenFrameBuffer;

static Tanto_V_BufferRegion uniformBufferRegion;
static Tanto_V_BufferRegion drawCallParmsRegion;

static Tanto_R_Primitive points;
static Tanto_R_Primitive border;

typedef enum {
    R_PIPE_LINES,
    R_PIPE_POINTS,
    R_PIPE_POST
} R_PipelineId;

typedef enum {
    R_PIPE_LAYOUT_MAIN,
    R_PIPE_LAYOUT_POST
} R_PipelineLayoutId;

typedef enum {
    R_DESC_SET_MAIN,
    R_DESC_SET_POST
} R_DescriptorSetId;

// TODO: we should implement a way to specify the offscreen renderpass format at initialization
static void initOffscreenFrameBuffer(void)
{
    //initDepthAttachment();
    offscreenFrameBuffer.depthAttachment = tanto_v_CreateImage(
            TANTO_WINDOW_WIDTH, TANTO_WINDOW_HEIGHT,
            depthFormat,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT);

    offscreenFrameBuffer.colorAttachment = tanto_v_CreateImageAndSampler(
            TANTO_WINDOW_WIDTH, TANTO_WINDOW_HEIGHT, 
            offscreenColorFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | 
            VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_FILTER_NEAREST);
    //
    // seting render pass and depth attachment
    offscreenFrameBuffer.pRenderPass = &offscreenRenderPass;

    const VkImageView attachments[] = {offscreenFrameBuffer.colorAttachment.view, offscreenFrameBuffer.depthAttachment.view};

    VkFramebufferCreateInfo framebufferInfo = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .layers = 1,
        .height = TANTO_WINDOW_HEIGHT,
        .width  = TANTO_WINDOW_WIDTH,
        .renderPass = *offscreenFrameBuffer.pRenderPass,
        .attachmentCount = 2,
        .pAttachments = attachments
    };

    V_ASSERT( vkCreateFramebuffer(device, &framebufferInfo, NULL, &offscreenFrameBuffer.handle) );

    tanto_v_TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, &offscreenFrameBuffer.colorAttachment);
}

// descriptors that do only need to have update called once and can be updated on initialization
static void updateStaticDescriptors(void)
{
    uniformBufferRegion = tanto_v_RequestBufferRegion(sizeof(UniformBuffer), 
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, TANTO_V_MEMORY_HOST_GRAPHICS_TYPE);
    memset(uniformBufferRegion.hostData, 0, sizeof(Parms));
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

    VkDescriptorImageInfo imageInfo = {
        .imageLayout = offscreenFrameBuffer.colorAttachment.layout,
        .imageView   = offscreenFrameBuffer.colorAttachment.view,
        .sampler     = offscreenFrameBuffer.colorAttachment.sampler
    };

    VkWriteDescriptorSet writes[] = {{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstArrayElement = 0,
        .dstSet = descriptorSets[R_DESC_SET_MAIN],
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &uboInfo
    },{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstArrayElement = 0,
        .dstSet = descriptorSets[R_DESC_SET_POST],
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &imageInfo
    }};

    vkUpdateDescriptorSets(device, TANTO_ARRAY_SIZE(writes), writes, 0, NULL);
}

static void updateDynamicDescriptors(void)
{
}

static void InitPipelines(void)
{
    const Tanto_R_DescriptorSet descriptorSets[] = {{
        .id = R_DESC_SET_MAIN,
        .bindingCount = 1,
        .bindings = {{
            .descriptorCount = 1,
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
        }}
    },{
        .id = R_DESC_SET_POST,
        .bindingCount = 1,
        .bindings = {{
            .descriptorCount = 1,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
        }}
    }};

    const Tanto_R_PipelineLayout pipelayouts[] = {{
        .id = R_PIPE_LAYOUT_MAIN, 
        .descriptorSetCount = 1, 
        .descriptorSetIds = {R_DESC_SET_MAIN},
        .pushConstantCount = 0,
        .pushConstantsRanges = {}
    },{
        .id = R_PIPE_LAYOUT_POST,
        .descriptorSetCount = 1,
        .descriptorSetIds = {R_DESC_SET_POST},
        .pushConstantCount = 0,
        .pushConstantsRanges = {}
    }};

    const Tanto_R_PipelineInfo pipeInfos[] = {{
        .id       = R_PIPE_LINES,
        .type     = TANTO_R_PIPELINE_RASTER_TYPE,
        .layoutId = R_PIPE_LAYOUT_MAIN,
        .payload.rasterInfo = {
            .renderPassType = TANTO_R_RENDER_PASS_OFFSCREEN_TYPE, 
            .vertexDescription = tanto_r_GetVertexDescription3D_Simple(),
            .polygonMode = VK_POLYGON_MODE_LINE,
            .vertShader = SPVDIR"/template-vert.spv",
            .fragShader = SPVDIR"/template-frag.spv"
        }
    },{
        .id       = R_PIPE_POINTS,
        .type     = TANTO_R_PIPELINE_RASTER_TYPE,
        .layoutId = R_PIPE_LAYOUT_MAIN,
        .payload.rasterInfo = {
            .renderPassType = TANTO_R_RENDER_PASS_OFFSCREEN_TYPE, 
            .vertexDescription = tanto_r_GetVertexDescription3D_Simple(),
            .polygonMode = VK_POLYGON_MODE_POINT,
            .vertShader = SPVDIR"/template-vert.spv",
            .fragShader = SPVDIR"/template-frag.spv"
        }
    },{
        .id       = R_PIPE_POST,
        .type     = TANTO_R_PIPELINE_POSTPROC_TYPE,
        .layoutId = R_PIPE_LAYOUT_POST,
        .payload.rasterInfo = {
            .renderPassType = TANTO_R_RENDER_PASS_SWAPCHAIN_TYPE, 
            .fragShader = SPVDIR"/post-frag.spv"
        }
    }};

    tanto_r_InitDescriptorSets(descriptorSets, TANTO_ARRAY_SIZE(descriptorSets));
    tanto_r_InitPipelineLayouts(pipelayouts, TANTO_ARRAY_SIZE(pipelayouts));
    tanto_r_InitPipelines(pipeInfos, TANTO_ARRAY_SIZE(pipeInfos));
}

static void mainRender(const VkCommandBuffer* cmdBuf, const VkRenderPassBeginInfo* rpassInfo)
{
    vkCmdBindDescriptorSets(
        *cmdBuf, 
        VK_PIPELINE_BIND_POINT_GRAPHICS, 
        pipelineLayouts[R_PIPE_LAYOUT_MAIN], 
        0, 1, &descriptorSets[R_DESC_SET_MAIN],
        0, NULL);

    vkCmdBindPipeline(*cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[R_PIPE_LINES]);

    vkCmdBeginRenderPass(*cmdBuf, rpassInfo, VK_SUBPASS_CONTENTS_INLINE);

    const VkBuffer vertBuffersCurve[2] = {
        points.vertexRegion.buffer,
        points.vertexRegion.buffer
    };

    const VkDeviceSize attrOffsetsCurve[2] = {
        points.attrOffsets[0] + points.vertexRegion.offset,
        points.attrOffsets[1] + points.vertexRegion.offset,
    };

    const VkBuffer vertBuffersBorder[2] = {
        border.vertexRegion.buffer,
        border.vertexRegion.buffer
    };

    const VkDeviceSize attrOffsetsBorder[2] = {
        border.attrOffsets[0] + border.vertexRegion.offset,
        border.attrOffsets[1] + border.vertexRegion.offset,
    };

    // draw the curve as lines
    vkCmdBindVertexBuffers(*cmdBuf, 0, 2, vertBuffersCurve, attrOffsetsCurve);
    vkCmdDrawIndirect(*cmdBuf, drawCallParmsRegion.buffer, drawCallParmsRegion.offset, 1, sizeof(VkDrawIndexedIndirectCommand));

    // draw the curve as points 
    //vkCmdBindPipeline(*cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[R_PIPE_POINTS]);
    //vkCmdDrawIndirect(*cmdBuf, drawCallParmsRegion.buffer, drawCallParmsRegion.offset, 1, sizeof(VkDrawIndexedIndirectCommand));

    assert(border.vertexCount > 0);
    // draw the border
    vkCmdBindPipeline(*cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[R_PIPE_LINES]);
    vkCmdBindVertexBuffers(*cmdBuf, 0, 2, vertBuffersBorder, attrOffsetsBorder);
    vkCmdDraw(*cmdBuf, border.vertexCount, 1, 0, 0);

    vkCmdEndRenderPass(*cmdBuf);
}

static void postProc(const VkCommandBuffer* cmdBuf, const VkRenderPassBeginInfo* rpassInfo)
{
    vkCmdBindPipeline(*cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[R_PIPE_POST]);

    vkCmdBindDescriptorSets(
        *cmdBuf, 
        VK_PIPELINE_BIND_POINT_GRAPHICS, 
        pipelineLayouts[R_PIPE_LAYOUT_POST], 
        0, 1, &descriptorSets[R_DESC_SET_POST],
        0, NULL);

    vkCmdBeginRenderPass(*cmdBuf, rpassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdDraw(*cmdBuf, 3, 1, 0, 0);

    vkCmdEndRenderPass(*cmdBuf);
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
    points = tanto_r_CreatePoints(pointCount);
}

static void initIndirectCmdStorage(void)
{
    drawCallParmsRegion = tanto_v_RequestBufferRegion(sizeof(VkDrawIndirectCommand), 0, TANTO_V_MEMORY_HOST_GRAPHICS_TYPE);

    VkDrawIndirectCommand cmd = {
        .firstInstance = 0,
        .firstVertex = 0,
        .instanceCount = 1,
        .vertexCount = points.vertexCount,
    };

    memcpy(drawCallParmsRegion.hostData, &cmd, sizeof(VkDrawIndirectCommand));
}

VkDrawIndirectCommand* r_GetDrawParms(void)
{
    return (VkDrawIndirectCommand*)(drawCallParmsRegion.hostData);
}

Tanto_R_Primitive* r_GetCurve(void)
{
    return &points;
}

void r_InitRenderer()
{
    InitPipelines();

    initOffscreenFrameBuffer();

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

    VkClearValue clears[] = {clearValueColor, clearValueDepth};

    const VkRenderPassBeginInfo rpassOffscreen = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .clearValueCount = 2,
        .pClearValues = clears,
        .renderArea = {{0, 0}, {TANTO_WINDOW_WIDTH, TANTO_WINDOW_HEIGHT}},
        .renderPass =  *offscreenFrameBuffer.pRenderPass,
        .framebuffer = offscreenFrameBuffer.handle,
    };

    const VkRenderPassBeginInfo rpassSwap = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .clearValueCount = 1,
        .pClearValues = clears,
        .renderArea = {{0, 0}, {TANTO_WINDOW_WIDTH, TANTO_WINDOW_HEIGHT}},
        .renderPass =  *frame->renderPass,
        .framebuffer = frame->frameBuffer 
    };

    mainRender(&frame->commandBuffer, &rpassOffscreen);
    postProc(&frame->commandBuffer, &rpassSwap);

    V_ASSERT( vkEndCommandBuffer(frame->commandBuffer) );
}

void r_CleanUp(void)
{
    vkDestroyFramebuffer(device, offscreenFrameBuffer.handle, NULL);
    tanto_v_DestroyImage(offscreenFrameBuffer.colorAttachment);
    tanto_v_DestroyImage(offscreenFrameBuffer.depthAttachment);
}
