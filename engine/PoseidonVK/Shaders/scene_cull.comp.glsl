#version 450

// The DrawParameters capability is enabled only for the GPU-scene tier.  The
// generated firstInstance is consumed as gl_BaseInstanceARB by scene.vert.
layout(local_size_x = 64) in;

layout(set = 0, binding = 0, std140) uniform SceneCullFrame
{
    mat4 view;
    mat4 projection;
} frame;

struct DrawConstants
{
    mat4 world;
    uint textureIds[4];
    uint meshId;
    uint indexBegin;
    uint indexCount;
    uint pass;
    uint state[16];
    vec4 tint;
};
layout(set = 0, binding = 1, std430) readonly buffer DrawConstantsBuffer { DrawConstants draws[]; } drawConstants;

struct SceneInstance
{
    vec4 localBoundsCenter;
    uint drawIndex;
    uint batchIndex;
    uint indirectOffset;
    uint batchCapacity;
    uint lodFirstIndex[4];
    uint lodIndexCount[4];
    float lodDistance[4];
};
layout(set = 0, binding = 2, std430) readonly buffer SceneInstances { SceneInstance instances[]; } scene;
layout(set = 0, binding = 3, std430) buffer BatchCounts { uint counts[]; } batchCounts;

struct IndirectCommand
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};
layout(set = 0, binding = 4, std430) buffer IndirectCommands { IndirectCommand commands[]; } indirect;

layout(push_constant) uniform CullDispatch
{
    uint instanceCount;
    uint batchCount;
} dispatchInfo;

bool visibleSphere(vec4 clip, float radius)
{
    // Transform a view-space sphere to conservative clip extents.  Each row
    // norm bounds that clip component for every vector on the sphere; the w
    // extent is included at every homogeneous frustum plane.
    float rx = radius * length(vec3(frame.projection[0][0], frame.projection[1][0], frame.projection[2][0]));
    float ry = radius * length(vec3(frame.projection[0][1], frame.projection[1][1], frame.projection[2][1]));
    float rz = radius * length(vec3(frame.projection[0][2], frame.projection[1][2], frame.projection[2][2]));
    float rw = radius * length(vec3(frame.projection[0][3], frame.projection[1][3], frame.projection[2][3]));
    return clip.x + rx >= -clip.w - rw && clip.x - rx <= clip.w + rw &&
           clip.y + ry >= -clip.w - rw && clip.y - ry <= clip.w + rw &&
           clip.z + rz >= -rw && clip.z - rz <= clip.w + rw;
}

void main()
{
    uint instanceIndex = gl_GlobalInvocationID.x;
    if (instanceIndex >= dispatchInfo.instanceCount)
        return;

    SceneInstance instance = scene.instances[instanceIndex];
    if (instance.drawIndex >= drawConstants.draws.length() || instance.batchIndex >= dispatchInfo.batchCount)
        return;

    mat4 world = drawConstants.draws[instance.drawIndex].world;
    vec4 worldCenter = world * vec4(instance.localBoundsCenter.xyz, 1.0);
    vec4 viewCenter = frame.view * worldCenter;
    vec4 clipCenter = frame.projection * viewCenter;
    float scale = max(length(world[0].xyz), max(length(world[1].xyz), length(world[2].xyz)));
    if (!visibleSphere(clipCenter, instance.localBoundsCenter.w * scale))
        return;

    float distanceToCamera = length(viewCenter.xyz);
    uint lod = 0u;
    for (uint candidate = 1u; candidate < 4u; ++candidate)
        if (instance.lodDistance[candidate] > 0.0 && distanceToCamera >= instance.lodDistance[candidate])
            lod = candidate;

    uint slot = atomicAdd(batchCounts.counts[instance.batchIndex], 1u);
    // The CPU reserves instanceCount commands for each batch; this guard makes
    // malformed input fail closed rather than writing the next batch.
    if (slot >= instance.batchCapacity)
        return;
    uint outputIndex = instance.indirectOffset + slot;
    indirect.commands[outputIndex].indexCount = instance.lodIndexCount[lod];
    indirect.commands[outputIndex].instanceCount = 1u;
    indirect.commands[outputIndex].firstIndex = instance.lodFirstIndex[lod];
    indirect.commands[outputIndex].vertexOffset = 0;
    indirect.commands[outputIndex].firstInstance = instance.drawIndex;
}
