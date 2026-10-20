// SPDX-License-Identifier: CC0-1.0
// Standalone native-API fixture, NOT the game or a Dawn compatibility layer.
// deko3d API/lifetime reference: devkitPro switch-examples deko_basic as shipped
// in devkitpro/devkita64:20260219 (image digest recorded in docs/switch).
#include "dksh_layout.hpp"

#include <deko3d.h>
#include <switch.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
constexpr unsigned Width = 1280;
constexpr unsigned Height = 720;
constexpr unsigned FrameCount = 2;
constexpr uint32_t CodeBytes = 64 * 1024;
constexpr uint32_t CommandBytes = 16 * 1024;

struct Probe {
    DkDevice device{};
    DkMemBlock imagesMemory{}, codeMemory{}, commandMemory{};
    DkSwapchain swapchain{};
    DkCmdBuf commandBuffer{};
    DkQueue queue{};
    std::array<DkImage, FrameCount> images{};
    std::array<DkCmdList, FrameCount> bindImage{};
    DkCmdList render{};
    std::array<DkShader, 2> shaders{};
    uint32_t codeOffset = 0;

    ~Probe() {
        // No resource referenced by queued work is released before completion.
        if (queue) {
            dkQueueWaitIdle(queue);
            dkQueueDestroy(queue);
        }
        if (commandBuffer) dkCmdBufDestroy(commandBuffer);
        if (commandMemory) dkMemBlockDestroy(commandMemory);
        if (codeMemory) dkMemBlockDestroy(codeMemory);
        if (swapchain) dkSwapchainDestroy(swapchain);
        if (imagesMemory) dkMemBlockDestroy(imagesMemory);
        if (device) dkDeviceDestroy(device);
    }

    DkMemBlock allocate(uint32_t bytes, uint32_t flags) {
        DkMemBlockMaker maker;
        dkMemBlockMakerDefaults(&maker, device, bytes);
        maker.flags = flags;
        return dkMemBlockCreate(&maker);
    }

    bool load_shader(unsigned stage, const char* path) {
        FILE* file = std::fopen(path, "rb");
        if (!file) return false;
        if (std::fseek(file, 0, SEEK_END) != 0) {
            std::fclose(file);
            return false;
        }
        const long size = std::ftell(file);
        if (size <= 0 || size > CodeBytes - DK_SHADER_CODE_UNUSABLE_SIZE ||
            std::fseek(file, 0, SEEK_SET) != 0) {
            std::fclose(file);
            return false;
        }
        std::vector<std::byte> data(static_cast<size_t>(size));
        const bool read = std::fread(data.data(), 1, data.size(), file) == data.size();
        const bool closed = std::fclose(file) == 0;
        if (!read || !closed || !dusk::switch_probe::validate_dksh(data, stage)) return false;
        const uint32_t alignedSize = (data.size() + DK_SHADER_CODE_ALIGNMENT - 1) & ~(DK_SHADER_CODE_ALIGNMENT - 1);
        if (alignedSize > CodeBytes - DK_SHADER_CODE_UNUSABLE_SIZE - codeOffset) return false;
        // CPU-uncached backing is written once, before any GPU submission; it is
        // never reused/overwritten while queued shaders can reference it.
        auto* destination = static_cast<std::byte*>(dkMemBlockGetCpuAddr(codeMemory)) + codeOffset;
        std::memcpy(destination, data.data(), data.size());
        DkShaderMaker maker;
        dkShaderMakerDefaults(&maker, codeMemory, codeOffset);
        dkShaderInitialize(&shaders[stage], &maker);
        codeOffset += alignedSize;
        return dkShaderIsValid(&shaders[stage]);
    }

    bool initialize() {
        DkDeviceMaker deviceMaker;
        dkDeviceMakerDefaults(&deviceMaker);
        device = dkDeviceCreate(&deviceMaker);
        if (!device) return false;
        DkImageLayoutMaker layoutMaker;
        dkImageLayoutMakerDefaults(&layoutMaker, device);
        layoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent;
        layoutMaker.format = DkImageFormat_RGBA8_Unorm;
        layoutMaker.dimensions[0] = Width;
        layoutMaker.dimensions[1] = Height;
        DkImageLayout layout;
        dkImageLayoutInitialize(&layout, &layoutMaker);
        const uint32_t alignment = dkImageLayoutGetAlignment(&layout);
        const uint32_t stride = (dkImageLayoutGetSize(&layout) + alignment - 1) & ~(alignment - 1);
        imagesMemory = allocate(FrameCount * stride, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
        if (!imagesMemory) return false;
        std::array<const DkImage*, FrameCount> imagePointers{};
        for (unsigned i = 0; i < FrameCount; ++i) {
            dkImageInitialize(&images[i], &layout, imagesMemory, i * stride);
            imagePointers[i] = &images[i];
        }
        DkSwapchainMaker swapchainMaker;
        dkSwapchainMakerDefaults(&swapchainMaker, device, nwindowGetDefault(), imagePointers.data(), FrameCount);
        swapchain = dkSwapchainCreate(&swapchainMaker);
        if (!swapchain) return false;
        codeMemory = allocate(CodeBytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
        commandMemory = allocate(CommandBytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        if (!codeMemory || !commandMemory || !load_shader(0, "romfs:/shaders/triangle.vert.dksh") ||
            !load_shader(1, "romfs:/shaders/triangle.frag.dksh")) return false;
        DkCmdBufMaker commandMaker;
        dkCmdBufMakerDefaults(&commandMaker, device);
        commandBuffer = dkCmdBufCreate(&commandMaker);
        if (!commandBuffer) return false;
        dkCmdBufAddMemory(commandBuffer, commandMemory, 0, CommandBytes);
        for (unsigned i = 0; i < FrameCount; ++i) {
            DkImageView view;
            dkImageViewDefaults(&view, &images[i]);
            dkCmdBufBindRenderTarget(commandBuffer, &view, nullptr);
            bindImage[i] = dkCmdBufFinishList(commandBuffer);
        }
        const DkViewport viewport{0, 0, float(Width), float(Height), 0, 1};
        const DkScissor scissor{0, 0, Width, Height};
        DkRasterizerState rasterizer;
        dkRasterizerStateDefaults(&rasterizer);
        rasterizer.cullMode = DkFace_None;
        DkColorState color;
        dkColorStateDefaults(&color);
        DkColorWriteState write;
        dkColorWriteStateDefaults(&write);
        DkDepthStencilState depth;
        dkDepthStencilStateDefaults(&depth);
        // This fixture has no depth attachment; unlike color defaults, deko3d's
        // depth defaults enable both testing and writing.
        depth.depthTestEnable = false;
        depth.depthWriteEnable = false;
        const DkShader* shaderPointers[]{&shaders[0], &shaders[1]};
        dkCmdBufSetViewports(commandBuffer, 0, &viewport, 1);
        dkCmdBufSetScissors(commandBuffer, 0, &scissor, 1);
        dkCmdBufBindRasterizerState(commandBuffer, &rasterizer);
        dkCmdBufBindColorState(commandBuffer, &color);
        dkCmdBufBindColorWriteState(commandBuffer, &write);
        dkCmdBufBindDepthStencilState(commandBuffer, &depth);
        dkCmdBufClearColorFloat(commandBuffer, 0, DkColorMask_RGBA, 0.04f, 0.05f, 0.08f, 1);
        dkCmdBufBindShaders(commandBuffer, DkStageFlag_GraphicsMask, shaderPointers, 2);
        dkCmdBufDraw(commandBuffer, DkPrimitive_Triangles, 3, 1, 0, 0);
        render = dkCmdBufFinishList(commandBuffer);
        // Lists and command storage stay immutable for their entire GPU lifetime.
        // In particular this fixture never calls dkCmdBufClear.
        DkQueueMaker queueMaker;
        dkQueueMakerDefaults(&queueMaker, device);
        queueMaker.flags = DkQueueFlags_Graphics;
        queue = dkQueueCreate(&queueMaker);
        return queue != nullptr;
    }

    bool frame() {
        const int image = dkQueueAcquireImage(queue, swapchain);
        if (image < 0 || unsigned(image) >= FrameCount) return false;
        dkQueueSubmitCommands(queue, bindImage[image]);
        dkQueueSubmitCommands(queue, render);
        dkQueuePresentImage(queue, swapchain, image);
        return true;
    }
};
} // namespace

int main() {
    if (appletGetAppletType() != AppletType_Application) {
        std::fputs("This probe requires application-mode homebrew. No rendering attempted.\n", stderr);
        return 1;
    }
    if (R_FAILED(romfsInit())) return 1;
    int result = 0;
    {
        Probe probe;
        if (!probe.initialize()) {
            std::fputs("deko3d probe initialization failed.\n", stderr);
            result = 1;
        } else {
            padConfigureInput(1, HidNpadStyleSet_NpadStandard);
            PadState pad;
            padInitializeDefault(&pad);
            while (appletMainLoop()) {
                padUpdate(&pad);
                if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
                if (!probe.frame()) {
                    std::fputs("deko3d probe acquire failed.\n", stderr);
                    result = 1;
                    break;
                }
            }
        }
    }
    romfsExit();
    return result;
}
