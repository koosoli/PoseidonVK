#version 450

layout(set = 1, binding = 0, r8) uniform readonly image3D cloudDensity;
layout(set = 1, binding = 1, r8) uniform writeonly image3D cloudDistance;
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

void main()
{
    const ivec3 dimensions = ivec3(96, 48, 96);
    ivec3 voxel = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(voxel, dimensions))) return;
    if (imageLoad(cloudDensity, voxel).r > (12.0 / 255.0))
    {
        imageStore(cloudDistance, voxel, vec4(0.0));
        return;
    }

    // A bounded six-axis nearest-occupied-voxel transform is conservative for
    // empty-space skipping: the raymarch still takes its normal step whenever
    // no nearby cloud is found. Unlike the old CPU chamfer upload, every value
    // is generated and consumed on the GPU.
    const ivec3 axes[6] = ivec3[](ivec3(1,0,0), ivec3(-1,0,0), ivec3(0,1,0),
                                  ivec3(0,-1,0), ivec3(0,0,1), ivec3(0,0,-1));
    int nearest = 16;
    for (int step = 1; step <= 16; ++step)
    {
        for (int axis = 0; axis < 6; ++axis)
        {
            ivec3 candidate = voxel + axes[axis] * step;
            if (all(greaterThanEqual(candidate, ivec3(0))) && all(lessThan(candidate, dimensions)) &&
                imageLoad(cloudDensity, candidate).r > (12.0 / 255.0))
            {
                nearest = step;
                break;
            }
        }
        if (nearest != 16) break;
    }
    imageStore(cloudDistance, voxel, vec4(float(nearest) / 16.0));
}
