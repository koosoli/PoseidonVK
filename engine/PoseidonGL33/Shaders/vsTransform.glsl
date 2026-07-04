#version 330 core
layout(std140) uniform VSConstants {
    mat4 proj;          // c0-c3
    mat4 view;          // c4-c7
    mat4 world;         // c8-c11
    vec4 sunDir;        // c12
    vec4 ambient;       // c13
    vec4 diffuse;       // c14
    vec4 emissive;      // c15
    vec4 fogParam;      // c16: {start, invRange, enabled, 0}
    vec4 camPos;        // c17
    vec4 specular;      // c18: rgb + power(w)
    vec4 specEn;        // c19: {enabled, 0, 0, 0}
    vec4 sunEn;         // c20: {enabled, 0, 0, 0}
    vec4 vpScale;       // c21: {2/width, 2/height, 0, 0} — VSScreen only, declared here for layout parity
    vec4 _pad22;
    vec4 _pad23;
    mat4 texMat0;       // c24-c27
    mat4 texMat1;       // c28-c31
    vec4 texCtrl;       // c32: {genTex0, genTex1, 0, 0}
    vec4 lightCount;        // c33: x = active local light count
    vec4 lightPos[8];       // c34-c41: xyz world pos, w = startAtten
    vec4 lightDiffuse[8];   // c42-c49: diffuse * nightEffect
    vec4 lightAmbient[8];   // c50-c57: ambient * nightEffect
    vec4 localLightDir[8];  // c58-c65: xyz beam dir (world), w = isSpot
    mat4 lightVP;           // c66-c69: shadow-map light view-projection (sampled per fragment)
};

layout(std140) uniform WorldInstances {
    mat4 worldArr[256];
};

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

out vec4 vColor;
out vec4 vSpecColor;
out vec2 vUV0;
out vec2 vUV1;
out float vFogTC;
out vec3 vWorldRel;

void main() {
    vec4 worldPos    = worldArr[gl_InstanceID] * vec4(pos, 1.0);
    vec3 worldNormal = normalize(mat3(worldArr[gl_InstanceID]) * normal);
    vec4 viewPos     = view * worldPos;
    gl_Position      = proj * viewPos;
    vWorldRel        = worldPos.xyz;

    float NdotL = max(0.0, dot(worldNormal, -sunDir.xyz));
    vec4 litColor;
    litColor.rgb = emissive.rgb + ambient.rgb * sunEn.x + diffuse.rgb * NdotL * sunEn.x;
    litColor.a   = emissive.a   + ambient.a   * sunEn.x + diffuse.a   * NdotL * sunEn.x;

    const float MIN_INSIDE2 = 0.95677279;
    const float MAX_INSIDE2 = 0.98063081;
    int nLights = int(lightCount.x);
    for (int i = 0; i < nLights; i++)
    {
        vec3 toLight = lightPos[i].xyz - worldPos.xyz;
        float size2 = dot(toLight, toLight);
        float startAtten2 = lightPos[i].w * lightPos[i].w;
        float endAtten2 = startAtten2 * 100.0;
        if (size2 >= endAtten2)
            continue;

        float cone = 1.0;
        if (localLightDir[i].w > 0.5)
        {
            float inside = -dot(toLight, localLightDir[i].xyz);
            if (inside <= 0.0)
                continue;
            float cos2 = (inside * inside) / size2;
            if (cos2 < MIN_INSIDE2)
                continue;
            cone = clamp((cos2 - MIN_INSIDE2) / (MAX_INSIDE2 - MIN_INSIDE2), 0.0, 1.0);
        }

        float atten = (size2 >= startAtten2) ? (startAtten2 / size2) : 1.0;
        float cosFi = dot(toLight, worldNormal);
        vec3 contrib;
        if (cosFi > 0.0)
        {
            cosFi *= inversesqrt(size2);
            contrib = (lightDiffuse[i].rgb * cosFi + lightAmbient[i].rgb) * (atten * cone);
        }
        else
        {
            contrib = lightAmbient[i].rgb * atten;
        }
        litColor.rgb += contrib;
    }

    vColor = clamp(litColor, 0.0, 1.0);

    vec3 spec = vec3(0.0);
    if (specEn.x > 0.5 && sunEn.x > 0.0) {
        vec3 viewDir = normalize(camPos.xyz - worldPos.xyz);
        vec3 halfVec = normalize(-sunDir.xyz + viewDir);
        float NdotH = max(0.0, dot(worldNormal, halfVec));
        float specPow = max(1.0, specular.w);
        spec = specular.rgb * pow(NdotH, specPow) * sunEn.x;
    }
    vSpecColor = vec4(clamp(spec, 0.0, 1.0), 0.0);

    float dist = length(worldPos.xyz - camPos.xyz);
    float fogFactor = clamp(1.0 - (dist - fogParam.x) * fogParam.y, 0.0, 1.0);
    vFogTC = (fogParam.z > 0.5) ? fogFactor : 1.0;

    vUV0 = (texCtrl.x > 0.5) ? (texMat0 * vec4(uv, 0, 1)).xy : uv;
    vUV1 = (texCtrl.y > 0.5) ? (texMat1 * vec4(uv, 0, 1)).xy : uv;
}