// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include <NRI.h>
#include <Extensions/NRIDeviceCreation.h>
#include <Extensions/NRIRayTracing.h>
#include <meshoptimizer.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
constexpr uint32_t kSize = 1024;
constexpr uint32_t kLayers = 32;
constexpr uint32_t kRepeats = 8;
constexpr uint64_t kImageBytes = kSize * kSize * sizeof(uint32_t);

void Check(nri::Result result, const char* operation)
{
    if (result != nri::Result::SUCCESS)
        throw std::runtime_error(operation);
}

struct Probe
{
    nri::CoreInterface Core{};
    nri::RayTracingInterface RT{};
    nri::Device* Device = nullptr;
    nri::Queue* Queue = nullptr;
    nri::CommandAllocator* Allocator = nullptr;
    nri::CommandBuffer* Commands = nullptr;
    nri::Fence* Fence = nullptr;
    nri::Micromap* Micromap = nullptr;
    nri::PipelineLayout* Layout = nullptr;
    nri::Pipeline* Pipeline = nullptr;
    nri::DescriptorPool* Pool = nullptr;
    nri::QueryPool* Queries = nullptr;
    std::vector<nri::Buffer*> Buffers;
    std::vector<nri::AccelerationStructure*> Structures;
    std::vector<nri::Descriptor*> Descriptors;

    ~Probe()
    {
        if (!Device)
            return;
        if (Pool) Core.DestroyDescriptorPool(Pool);
        for (auto* descriptor : Descriptors) Core.DestroyDescriptor(descriptor);
        if (Pipeline) Core.DestroyPipeline(Pipeline);
        if (Layout) Core.DestroyPipelineLayout(Layout);
        if (Queries) Core.DestroyQueryPool(Queries);
        for (auto it = Structures.rbegin(); it != Structures.rend(); ++it) RT.DestroyAccelerationStructure(*it);
        if (Micromap) RT.DestroyMicromap(Micromap);
        for (auto* buffer : Buffers) Core.DestroyBuffer(buffer);
        if (Fence) Core.DestroyFence(Fence);
        if (Commands) Core.DestroyCommandBuffer(Commands);
        if (Allocator) Core.DestroyCommandAllocator(Allocator);
        nri::nriDestroyDevice(Device);
    }

    nri::Buffer* Buffer(uint64_t size, nri::BufferUsageBits usage,
        nri::MemoryLocation memory, const void* bytes = nullptr, uint32_t stride = 0)
    {
        nri::Buffer* buffer = nullptr;
        const nri::BufferDesc desc{ size, stride, usage };
        Check(Core.CreateCommittedBuffer(*Device, memory, 0.0f, desc, buffer), "CreateCommittedBuffer");
        Buffers.push_back(buffer);
        if (bytes)
        {
            auto* mapped = Core.MapBuffer(*buffer, 0, size);
            if (!mapped) throw std::runtime_error("Map input");
            std::memcpy(mapped, bytes, static_cast<size_t>(size));
            Core.UnmapBuffer(*buffer);
        }
        return buffer;
    }

    std::pair<nri::Buffer*, uint64_t> Scratch(uint64_t size)
    {
        uint64_t alignment = std::max<uint64_t>(256, Core.GetDeviceDesc(*Device).memoryAlignment.scratchBufferOffset);
        auto* buffer = Buffer(size + alignment, nri::BufferUsageBits::SCRATCH, nri::MemoryLocation::DEVICE);
        const uint64_t address = Core.GetBufferDeviceAddress(*buffer);
        return { buffer, (alignment - address % alignment) % alignment };
    }

    void Barrier(nri::AccessBits before, nri::StageBits previous, nri::AccessBits after, nri::StageBits next)
    {
        const nri::GlobalBarrierDesc global{ { before, previous }, { after, next } };
        nri::BarrierDesc barrier{};
        barrier.globals = &global;
        barrier.globalNum = 1;
        Core.CmdBarrier(*Commands, barrier);
    }

    void BuildMicromap(uint32_t triangleCount)
    {
        constexpr uint32_t subdivision = 4;
        constexpr uint32_t textureSize = 64;
        std::array<uint8_t, textureSize * textureSize> alpha{};
        for (uint32_t y = 8; y < 56; y++)
            for (uint32_t x = 8; x < 56; x++)
                alpha[y * textureSize + x] = 255;
        const auto entrySize = static_cast<uint32_t>(meshopt_opacityMapEntrySize(subdivision, 4));
        std::vector<uint8_t> data(entrySize * 2);
        const float uv[6][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        for (uint32_t i = 0; i < 2; i++)
            meshopt_opacityMapRasterize(data.data() + entrySize * i, subdivision, 4, uv[i * 3], uv[i * 3 + 1], uv[i * 3 + 2],
                alpha.data(), 1, textureSize, textureSize, textureSize);
        std::vector<nri::MicromapTriangle> triangles(triangleCount);
        for (uint32_t i = 0; i < triangleCount; i++)
            triangles[i] = { (i & 1u) * entrySize, subdivision, nri::MicromapFormat::OPACITY_4_STATE };
        auto* opacity = Buffer(data.size(), nri::BufferUsageBits::MICROMAP_BUILD_INPUT, nri::MemoryLocation::HOST_UPLOAD, data.data());
        auto* layout = Buffer(triangles.size() * sizeof(triangles.front()), nri::BufferUsageBits::MICROMAP_BUILD_INPUT,
            nri::MemoryLocation::HOST_UPLOAD, triangles.data());
        const nri::MicromapUsageDesc usage{ triangleCount, subdivision, nri::MicromapFormat::OPACITY_4_STATE };
        nri::MicromapDesc desc{};
        desc.usages = &usage;
        desc.usageNum = 1;
        desc.flags = nri::MicromapBits::PREFER_FAST_TRACE;
        Check(RT.CreateCommittedMicromap(*Device, nri::MemoryLocation::DEVICE, 0.0f, desc, Micromap), "CreateCommittedMicromap");
        auto [scratch, offset] = Scratch(RT.GetMicromapBuildScratchBufferSize(*Micromap));
        const nri::BuildMicromapDesc build{ Micromap, opacity, 0, layout, 0, scratch, offset };
        RT.CmdBuildMicromaps(*Commands, &build, 1);
        Barrier(nri::AccessBits::MICROMAP_WRITE, nri::StageBits::MICROMAP,
            nri::AccessBits::MICROMAP_READ, nri::StageBits::ACCELERATION_STRUCTURE);
    }

    nri::AccelerationStructure* NewAS(const nri::AccelerationStructureDesc& desc)
    {
        nri::AccelerationStructure* structure = nullptr;
        Check(RT.CreateCommittedAccelerationStructure(*Device, nri::MemoryLocation::DEVICE, 0.0f, desc, structure),
            "CreateCommittedAccelerationStructure");
        Structures.push_back(structure);
        return structure;
    }

    nri::AccelerationStructure* BuildScene(nri::Buffer* vertices, bool useMicromaps)
    {
        nri::BottomLevelTrianglesMicromapDesc attachment{};
        attachment.micromap = Micromap;
        if (useMicromaps)
        {
            std::array<uint32_t, kLayers * 2> indices{};
            for (uint32_t i = 0; i < indices.size(); i++)
                indices[i] = i;
            attachment.indexBuffer = Buffer(sizeof(indices), nri::BufferUsageBits::ACCELERATION_STRUCTURE_BUILD_INPUT,
                nri::MemoryLocation::HOST_UPLOAD, indices.data());
            attachment.indexType = nri::IndexType::UINT32;
        }
        nri::BottomLevelGeometryDesc geometry{};
        geometry.type = nri::BottomLevelGeometryType::TRIANGLES;
        geometry.triangles.vertexBuffer = vertices;
        geometry.triangles.vertexNum = kLayers * 6;
        geometry.triangles.vertexStride = 12;
        geometry.triangles.vertexFormat = nri::Format::RGB32_SFLOAT;
        geometry.triangles.micromap = useMicromaps ? &attachment : nullptr;
        const nri::AccelerationStructureDesc bottom{ 0, &geometry, 1, nri::AccelerationStructureBits::PREFER_FAST_TRACE,
            nri::AccelerationStructureType::BOTTOM_LEVEL };
        auto* blas = NewAS(bottom);
        auto [scratch, offset] = Scratch(RT.GetAccelerationStructureBuildScratchBufferSize(*blas));
        const nri::BuildBottomLevelAccelerationStructureDesc build{ blas, nullptr, &geometry, 1, scratch, offset };
        RT.CmdBuildBottomLevelAccelerationStructures(*Commands, &build, 1);
        Barrier(nri::AccessBits::ACCELERATION_STRUCTURE_WRITE, nri::StageBits::ACCELERATION_STRUCTURE,
            nri::AccessBits::ACCELERATION_STRUCTURE_READ, nri::StageBits::ACCELERATION_STRUCTURE);

        nri::TopLevelInstance instance{};
        instance.transform[0][0] = instance.transform[1][1] = instance.transform[2][2] = 1.0f;
        instance.mask = 0xff;
        instance.accelerationStructureHandle = RT.GetAccelerationStructureHandle(*blas);
        auto* upload = Buffer(sizeof(instance), nri::BufferUsageBits::ACCELERATION_STRUCTURE_BUILD_INPUT,
            nri::MemoryLocation::HOST_UPLOAD, &instance);
        const nri::AccelerationStructureDesc top{ 0, nullptr, 1, nri::AccelerationStructureBits::PREFER_FAST_TRACE,
            nri::AccelerationStructureType::TOP_LEVEL };
        auto* tlas = NewAS(top);
        auto [topScratch, topOffset] = Scratch(RT.GetAccelerationStructureBuildScratchBufferSize(*tlas));
        const nri::BuildTopLevelAccelerationStructureDesc buildTop{ tlas, nullptr, 1, upload, 0, topScratch, topOffset };
        RT.CmdBuildTopLevelAccelerationStructures(*Commands, &buildTop, 1);
        return tlas;
    }

    void CreatePipeline(const char* shader)
    {
        const std::array<nri::DescriptorRangeDesc, 2> ranges{{
            { 0, 1, nri::DescriptorType::ACCELERATION_STRUCTURE, nri::StageBits::COMPUTE_SHADER },
            { 1, 1, nri::DescriptorType::STORAGE_STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER },
        }};
        nri::DescriptorSetDesc set{};
        set.ranges = ranges.data();
        set.rangeNum = static_cast<uint32_t>(ranges.size());
        nri::PipelineLayoutDesc layout{};
        layout.descriptorSets = &set;
        layout.descriptorSetNum = 1;
        layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        layout.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
        Check(Core.CreatePipelineLayout(*Device, layout, Layout), "CreatePipelineLayout");

        std::ifstream file(shader, std::ios::binary | std::ios::ate);
        if (!file) throw std::runtime_error("Open shader");
        std::vector<uint32_t> words(static_cast<size_t>(file.tellg()) / sizeof(uint32_t));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
        nri::ComputePipelineDesc pipeline{};
        pipeline.pipelineLayout = Layout;
        pipeline.shader.bytecode = words.data();
        pipeline.shader.size = words.size() * sizeof(uint32_t);
        pipeline.shader.entryPointName = "main";
        pipeline.shader.stage = nri::StageBits::COMPUTE_SHADER;
        Check(Core.CreateComputePipeline(*Device, pipeline, Pipeline), "CreateComputePipeline");

        nri::DescriptorPoolDesc pool{};
        pool.descriptorSetMaxNum = 2;
        pool.accelerationStructureMaxNum = 2;
        pool.storageStructuredBufferMaxNum = 2;
        Check(Core.CreateDescriptorPool(*Device, pool, Pool), "CreateDescriptorPool");
        Check(Core.CreateQueryPool(*Device, { nri::QueryType::TIMESTAMP, 4 * kRepeats }, Queries), "CreateQueryPool");
    }

    nri::DescriptorSet* BindScene(nri::AccelerationStructure* scene, nri::Buffer* output)
    {
        nri::Descriptor* geometry = nullptr;
        nri::Descriptor* storage = nullptr;
        Check(RT.CreateAccelerationStructureDescriptor(*scene, geometry), "CreateAccelerationStructureDescriptor");
        Descriptors.push_back(geometry);
        Check(Core.CreateBufferView({ output, nri::BufferView::STORAGE_STRUCTURED_BUFFER, 0, kImageBytes }, storage), "CreateBufferView");
        Descriptors.push_back(storage);
        nri::DescriptorSet* set = nullptr;
        Check(Core.AllocateDescriptorSets(*Pool, *Layout, 0, &set, 1, 0), "AllocateDescriptorSets");
        const std::array<nri::UpdateDescriptorRangeDesc, 2> updates{{
            { set, 0, 0, &geometry, 1 }, { set, 1, 0, &storage, 1 },
        }};
        Core.UpdateDescriptorRanges(updates.data(), static_cast<uint32_t>(updates.size()));
        return set;
    }

    void Run(const char* shader)
    {
        nri::DeviceCreationDesc device{};
        device.graphicsAPI = nri::GraphicsAPI::VK;
        device.enableGraphicsAPIValidation = true;
        device.enableNRIValidation = true;
        Check(nri::nriCreateDevice(device, Device), "nriCreateDevice");
        Check(nri::nriGetInterface(*Device, NRI_INTERFACE(nri::CoreInterface), &Core), "CoreInterface");
        const auto& properties = Core.GetDeviceDesc(*Device);
        std::printf("%s, RT tier %u, max micromap subdivision %u\n", properties.adapterDesc.name,
            properties.tiers.rayTracing, properties.accelerationStructure.micromapSubdivisionMaxLevel);
        if (properties.tiers.rayTracing < 3) throw std::runtime_error("Opacity micromaps unavailable");
        Check(nri::nriGetInterface(*Device, NRI_INTERFACE(nri::RayTracingInterface), &RT), "RayTracingInterface");
        Check(Core.GetQueue(*Device, nri::QueueType::GRAPHICS, 0, Queue), "GetQueue");
        Check(Core.CreateCommandAllocator(*Queue, Allocator), "CreateCommandAllocator");
        Check(Core.CreateCommandBuffer(*Allocator, Commands), "CreateCommandBuffer");
        Check(Core.CreateFence(*Device, 0, Fence), "CreateFence");
        CreatePipeline(shader);
        Check(Core.BeginCommandBuffer(*Commands, Pool), "BeginCommandBuffer");

        std::vector<std::array<float, 3>> positions;
        for (uint32_t layer = 0; layer < kLayers; layer++)
        {
            const float z = -static_cast<float>(layer) * 0.025f;
            positions.insert(positions.end(), {{ 0, 0, z }, { 1, 0, z }, { 0, 1, z }, { 1, 0, z }, { 1, 1, z }, { 0, 1, z }});
        }
        auto* vertices = Buffer(positions.size() * sizeof(positions.front()), nri::BufferUsageBits::ACCELERATION_STRUCTURE_BUILD_INPUT,
            nri::MemoryLocation::HOST_UPLOAD, positions.data());
        BuildMicromap(kLayers * 2);
        std::array<nri::Buffer*, 2> outputs{};
        std::array<nri::DescriptorSet*, 2> sets{};
        for (uint32_t mode = 0; mode < 2; mode++)
        {
            auto* scene = BuildScene(vertices, mode == 1);
            outputs[mode] = Buffer(kImageBytes, nri::BufferUsageBits::SHADER_RESOURCE_STORAGE, nri::MemoryLocation::DEVICE, nullptr, sizeof(uint32_t));
            sets[mode] = BindScene(scene, outputs[mode]);
        }
        Barrier(nri::AccessBits::ACCELERATION_STRUCTURE_WRITE, nri::StageBits::ACCELERATION_STRUCTURE,
            nri::AccessBits::ACCELERATION_STRUCTURE_READ, nri::StageBits::COMPUTE_SHADER);
        Core.CmdResetQueries(*Commands, *Queries, 0, 4 * kRepeats);
        Core.CmdSetPipelineLayout(*Commands, nri::BindPoint::COMPUTE, *Layout);
        Core.CmdSetPipeline(*Commands, *Pipeline);
        for (uint32_t repeat = 0; repeat < kRepeats; repeat++)
            for (uint32_t mode = 0; mode < 2; mode++)
            {
                Barrier(nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::StageBits::COMPUTE_SHADER,
                    nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::StageBits::COMPUTE_SHADER);
                const uint32_t timestamp = 4 * repeat + 2 * mode;
                Core.CmdSetDescriptorSet(*Commands, { 0, sets[mode], nri::BindPoint::COMPUTE });
                Core.CmdEndQuery(*Commands, *Queries, timestamp);
                Core.CmdDispatch(*Commands, { kSize / 8, kSize / 8, 1 });
                Core.CmdEndQuery(*Commands, *Queries, timestamp + 1);
            }

        Barrier(nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::StageBits::COMPUTE_SHADER,
            nri::AccessBits::COPY_SOURCE, nri::StageBits::COPY);
        constexpr uint64_t queryBytes = 4 * kRepeats * sizeof(uint64_t);
        auto* readback = Buffer(2 * kImageBytes + queryBytes, nri::BufferUsageBits::NONE, nri::MemoryLocation::HOST_READBACK);
        Core.CmdCopyBuffer(*Commands, *readback, 0, *outputs[0], 0, kImageBytes);
        Core.CmdCopyBuffer(*Commands, *readback, kImageBytes, *outputs[1], 0, kImageBytes);
        Core.CmdCopyQueries(*Commands, *Queries, 0, 4 * kRepeats, *readback, 2 * kImageBytes);
        Check(Core.EndCommandBuffer(*Commands), "EndCommandBuffer");
        const nri::FenceSubmitDesc signal{ Fence, 1 };
        nri::QueueSubmitDesc submit{};
        submit.commandBuffers = &Commands;
        submit.commandBufferNum = 1;
        submit.signalFences = &signal;
        submit.signalFenceNum = 1;
        Check(Core.QueueSubmit(*Queue, submit), "QueueSubmit");
        Core.Wait(*Fence, 1);
        const auto* bytes = static_cast<const uint8_t*>(Core.MapBuffer(*readback, 0, nri::WHOLE_SIZE));
        if (!bytes) throw std::runtime_error("Map readback");
        const auto* baseline = reinterpret_cast<const uint32_t*>(bytes);
        const auto* micromapped = reinterpret_cast<const uint32_t*>(bytes + kImageBytes);
        const uint64_t hits = std::count(baseline, baseline + kSize * kSize, 1u);
        const bool same = std::equal(baseline, baseline + kSize * kSize, micromapped);
        const auto* times = reinterpret_cast<const uint64_t*>(bytes + 2 * kImageBytes);
        std::array<std::array<double, kRepeats - 1>, 2> durations{};
        for (uint32_t repeat = 1; repeat < kRepeats; repeat++)
            for (uint32_t mode = 0; mode < 2; mode++)
            {
                uint32_t index = 4 * repeat + 2 * mode;
                durations[mode][repeat - 1] = 1000.0 * static_cast<double>(times[index + 1] - times[index]) / properties.other.timestampFrequencyHz;
            }
        Core.UnmapBuffer(*readback);
        for (auto& values : durations) std::sort(values.begin(), values.end());
        const double original = durations[0][(kRepeats - 1) / 2], micromap = durations[1][(kRepeats - 1) / 2];
        std::printf("Visible %llu / %u pixels, matching image %s\n", static_cast<unsigned long long>(hits), kSize * kSize, same ? "yes" : "no");
        std::printf("Median trace time %.4f ms inline, %.4f ms with micromaps\n", original, micromap);
        if (!same || hits < kSize * kSize / 3 || hits > kSize * kSize * 2 / 3 || original <= 0 || micromap <= 0)
            throw std::runtime_error("Micromap visibility or timings did not match the test workload");
    }
};
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("Supply the compiled probe shader");
        Probe probe;
        probe.Run(argv[1]);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
